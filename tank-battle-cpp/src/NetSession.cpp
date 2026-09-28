#include "NetSession.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>

namespace {
using Clock = std::chrono::steady_clock;
inline float secsSince(Clock::time_point t) {
    return std::chrono::duration<float>(Clock::now() - t).count();
}
} // namespace

NetSession::~NetSession() { close(false); }

// ---------------- 会话建立 ----------------

bool NetSession::startHost(const std::string& roomName) {
    stopKeepalive(); // 心跳线程只读 peerIp_/peerPort_, 重开前必须停旧线程
    sock_.setBlocking(false);
    if (sock_.bind(Port) != sf::Socket::Status::Done) return false;
    myName_ = roomName;
    peerReady_ = false;
    remoteInput_ = {};
    connected_ = false;
    disconnectedFired_ = false;
    role_ = Role::HostWaiting;
    return true;
}

void NetSession::startScan(const std::string& playerName) {
    sock_.setBlocking(false); // 不预绑定: 首次 send 自动分配临时端口
    myName_ = playerName;
    hosts_.clear();
    role_ = Role::Scan;
    scanStart_ = Clock::now();
    scanTimer_ = 0.f; // 首个 DISC 在下一次 poll 立即发出
}

void NetSession::startJoin(sf::IpAddress host, const std::string& playerName) {
    stopKeepalive(); // 心跳线程只读 peerIp_/peerPort_, 改写前必须停旧线程
    sock_.setBlocking(false);
    myName_ = playerName;
    peerIp_ = host;
    peerPort_ = Port; // 主机主端口
    // 清上一场基准: 新主机的 outTick_ 可能低于旧 lastSnapTick_, 不清会把
    // 全部快照当旧包丢弃; snap_ 同理(旧对局 phase 会误挡大厅期 JOIN 重发)
    snap_.reset();
    lastSnapTick_ = 0;
    peerReady_ = false;
    remoteInput_ = {};
    connected_ = false;
    disconnectedFired_ = false;
    role_ = Role::Joining;
    joinTimer_ = 0.f; // 首个 JOIN 在下一次 poll 立即发出
}

void NetSession::startKeepalive() {
    // peerIp_/peerPort_ 此后不再被写(会话期恒定), 线程只读 -> 无竞态。
    // 主线程卡在窗口拖动/缩放的模态循环时心跳照发(语音 voicePingThread 同款)
    keepThread_ = std::jthread([this](std::stop_token st) {
        while (!st.stop_requested()) {
            const auto k = proto::encodeKeep();
            (void)keepSock_.send(k.data(), k.size(), peerIp_, peerPort_);
            for (int i = 0; i < 10 && !st.stop_requested(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}

void NetSession::stopKeepalive() {
    keepThread_.request_stop();
    if (keepThread_.joinable()) keepThread_.join();
}

// ---------------- 主循环驱动 ----------------

void NetSession::poll(float dt) {
    // --- 周期发送 ---
    if (role_ == Role::Scan || role_ == Role::Joining) {
        scanTimer_ -= dt;
        if (role_ == Role::Scan && scanTimer_ <= 0.f) { // DISC 5Hz 广播
            const auto d = proto::encodeDisc();
            (void)sock_.send(d.data(), d.size(), sf::IpAddress::Broadcast, Port);
            scanTimer_ = 0.2f;
            if (secsSince(scanStart_) >= 2.f) {
                role_ = Role::ScanDone; // 扫描窗口结束(结果在 foundHosts_)
                events_.push_back({NetEvent::Kind::ScanDone});
            }
        }
        if (role_ == Role::Joining) { // JOIN 5Hz(连接成功前)
            joinTimer_ -= dt;
            if (joinTimer_ <= 0.f) { sendJoin(); joinTimer_ = 0.2f; }
        }
    } else if (role_ == Role::ClientSession) {
        // 会话期: 结算/大厅态(最近 snap 非 Battle/Paused/Countdown)由 poll
        // 以 5Hz 重发 JOIN(携 ready 位); 对局态不发(INP 由 sendInput 30Hz)
        joinTimer_ -= dt;
        const bool inGame = snap_ && (snap_->phase == proto::Phase::Countdown ||
                                      snap_->phase == proto::Phase::Battle ||
                                      snap_->phase == proto::Phase::Paused);
        if (joinTimer_ <= 0.f && !inGame) { sendJoin(); joinTimer_ = 0.2f; }
    }
    // --- 收包(每次 poll 收尽) ---
    for (int i = 0; i < 64; ++i) {
        std::size_t got = 0;
        std::optional<sf::IpAddress> from;
        unsigned short fport = 0;
        if (sock_.receive(buf_, sizeof(buf_), got, from, fport) !=
            sf::Socket::Status::Done)
            break;
        handlePacket(buf_, got, from.value_or(sf::IpAddress::Any), fport);
    }
    // --- 掉线检测(5s 无任何包, 含 KEEP) ---
    if (connected_ && !disconnectedFired_ && secsSince(lastAny_) > 5.f) {
        disconnectedFired_ = true;
        events_.push_back({NetEvent::Kind::Disconnected});
    }
}

void NetSession::handlePacket(const std::uint8_t* p, std::size_t n,
                              sf::IpAddress ip, unsigned short port) {
    if (n < 2) return;
    const std::uint8_t ver = p[1];
    // 源过滤(spec §6.3 rev4): JOIN 不受拦截——否则忙碌拒绝永远看不到陌生
    // JOIN、对端大厅期的 ready 上报也会被拦死
    if (connected_ && ip != peerIp_ && p[0] != static_cast<std::uint8_t>(proto::Id::Join))
        return;
    if (connected_ && ip == peerIp_ &&
        p[0] != static_cast<std::uint8_t>(proto::Id::Keep) &&
        p[0] != static_cast<std::uint8_t>(proto::Id::Join) &&
        port != peerPort_)
        return;

    switch (static_cast<proto::Id>(p[0])) {
    case proto::Id::Disc: // 仅主机等待期回应(战斗中对局域网不可见); 版本不符回 ERR
        if (role_ == Role::HostWaiting) {
            if (ver != proto::kVersion) { replyErr(ip, port, proto::kErrVersion, ver); break; }
            const auto h = proto::encodeHost(myName_);
            if (!h.empty()) // 名字超长编码失败(Game 侧已限长, 兜底不发空包)
                (void)sock_.send(h.data(), h.size(), ip, port);
        }
        break;
    case proto::Id::Join: {
        auto j = proto::decodeJoin(p, n);
        if (!j) return;
        if (role_ == Role::HostWaiting) {
            if (ver != proto::kVersion) { replyErr(ip, port, proto::kErrVersion, ver); return; }
            peerIp_ = ip; peerPort_ = port; peerName_ = j->name;
            connected_ = true; lastAny_ = lastInp_ = Clock::now();
            role_ = Role::HostSession; startKeepalive();
            events_.push_back({NetEvent::Kind::Connected});
        } else if (connected_ && ip == peerIp_ && port == peerPort_) {
            // 会话内 JOIN = 大厅/结算期 ready 位上报通道(spec §6.4), 不重建会话
            peerReady_ = j->ready;
        } else if (connected_) {
            // 第三方: 对每份到达的 JOIN 回 ERR{忙}(随对方 5Hz 重发天然可靠)
            replyErr(ip, port, proto::kErrBusy, proto::kVersion);
        }
        break;
    }
    case proto::Id::Inp:
        if (role_ == Role::HostSession) {
            if (auto m = proto::decodeInp(p, n)) {
                remoteInput_ = m->input; lastInp_ = Clock::now();
            }
        }
        break;
    case proto::Id::Snap:
        // Joining 也在接受范围(spec §4): 客户端连接成功 = 第一个被接受的
        // SNAP. brief 片段外层只写 ClientSession 会让下面 !connected_ 分支
        // 永不可达(ClientSession 无其他入口)——依 spec 修正为两者皆收
        if ((role_ == Role::Joining || role_ == Role::ClientSession) &&
            ver == proto::kVersion) {
            if (auto m = proto::decodeSnap(p, n)) {
                if (proto::acceptVerdict(m->tick, lastSnapTick_) ==
                    proto::Accept::Apply) {
                    lastSnapTick_ = m->tick; snap_ = std::move(m);
                    if (!connected_) { // 第一个 SNAP = 连接成功(spec §4)
                        connected_ = true; role_ = Role::ClientSession;
                        // 心跳双向都要发(spec §6.3/§7: 客户端拖窗时主机凭
                        // KEEP 区分"该暂停"与"已掉线")
                        startKeepalive();
                        events_.push_back({NetEvent::Kind::Connected});
                    }
                }
            } else if (auto e = proto::decodeErr(p, n); !e && ver != proto::kVersion) {
                events_.push_back({NetEvent::Kind::VersionMismatch});
            }
        }
        break;
    case proto::Id::Host:
        if (role_ == Role::Scan) {
            if (ver != proto::kVersion) {
                events_.push_back({NetEvent::Kind::VersionMismatch}); return;
            }
            if (auto m = proto::decodeHost(p, n))
                if (std::none_of(hosts_.begin(), hosts_.end(),
                                 [&](const FoundHost& h) { return h.addr == ip; }))
                    hosts_.push_back({ip, m->name});
        }
        break;
    case proto::Id::Err:
        if (auto e = proto::decodeErr(p, n)) {
            if (e->reason == proto::kErrBusy)
                events_.push_back({NetEvent::Kind::Busy});
            else
                events_.push_back({NetEvent::Kind::VersionMismatch});
        }
        break;
    case proto::Id::Bye:
        if (connected_) {
            events_.push_back({NetEvent::Kind::Disconnected});
            disconnectedFired_ = true;
        }
        break;
    case proto::Id::Keep:
        break; // lastAny_ 统一在函数末尾更新
    default:
        break; // 未知 msgId 丢弃
    }
    if (connected_ && ip == peerIp_) lastAny_ = Clock::now();
}

// ---------------- 收发辅助 ----------------

void NetSession::sendJoin() {
    const auto j = proto::encodeJoin(myName_, myReady_);
    if (j.empty()) return; // 名字超长编码失败(Game 侧已限长, 兜底)
    (void)sock_.send(j.data(), j.size(), peerIp_, peerPort_);
}

void NetSession::replyErr(sf::IpAddress ip, unsigned short port,
                          std::uint8_t reason, std::uint8_t peerVersion) {
    const auto e = proto::encodeErr(reason, peerVersion);
    (void)sock_.send(e.data(), e.size(), ip, port);
}

void NetSession::sendSnap(const proto::SnapMsg& s) {
    proto::SnapMsg m = s; // 先拷贝再填 tick, 不改调用方的 BattleState
    m.tick = ++outTick_;  // 跨局单调永不重置(C1): 重开后 tick 只增, 客户端
                          // 接受规则(> 才应用)无需跨局特判
    const auto b = proto::encodeSnap(m);
    if (b.empty()) return; // 导弹数超上限: 放弃本帧快照(下一帧照常)
    (void)sock_.send(b.data(), b.size(), peerIp_, peerPort_);
}

void NetSession::sendInput(const InputState& in) {
    const auto b = proto::encodeInp(in);
    (void)sock_.send(b.data(), b.size(), peerIp_, peerPort_);
}

// ---------------- 查询与收尾 ----------------

bool NetSession::isHost() const {
    return role_ == Role::HostWaiting || role_ == Role::HostSession;
}

bool NetSession::clientJoined() const { return role_ == Role::HostSession; }

const InputState& NetSession::remoteInput() const { return remoteInput_; }

bool NetSession::remoteReady() const { return peerReady_; }

bool NetSession::remoteInputStale() const {
    // >1s 无 INP 且 KEEP 仍在(<=5s)才判"对端活着但主循环停摆"; 阶段闸在
    // 调用方: Game 仅 Battle 相位询问(倒计时/结算期无输入预期, 不算停滞)
    return connected_ && secsSince(lastInp_) > 1.f && secsSince(lastAny_) <= 5.f;
}

const std::vector<FoundHost>& NetSession::foundHosts() const { return hosts_; }

const std::optional<proto::SnapMsg>& NetSession::snap() const { return snap_; }

bool NetSession::peerRecent() const {
    return connected_ && secsSince(lastAny_) <= 5.f;
}

void NetSession::setReady(bool r) { myReady_ = r; }

std::vector<NetEvent> NetSession::takeEvents() {
    std::vector<NetEvent> out;
    out.swap(events_);
    return out;
}

sf::IpAddress NetSession::peerAddress() const { return peerIp_; }

const std::string& NetSession::peerName() const { return peerName_; }

void NetSession::close(bool sendBye) {
    if (sendBye && connected_) { // 尽力而为: UDP 无重传, 丢了靠对端 5s 超时兜底
        const auto b = proto::encodeBye();
        (void)sock_.send(b.data(), b.size(), peerIp_, peerPort_);
    }
    stopKeepalive();
    sock_.unbind();
    keepSock_.unbind();
    role_ = Role::None;
    connected_ = false;
}
