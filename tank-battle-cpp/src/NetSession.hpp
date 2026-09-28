#pragma once
// NetSession: 联机会话 socket 层 —— 发现(DISC/HOST 广播)、进房(JOIN/ERR/忙碌)、
// 快照/输入收发(幂等现状广播, 无 ack)、源地址过滤(spec §6.3)、1Hz 心跳线程。
// 纯 socket 编排不进单测(编解码/玩法语义在 Protocol/Battle, 端到端由冒烟覆盖)。
// 线程模型: sock_ 仅主线程; keepSock_ 仅心跳线程; peerIp_/peerPort_ 在
// startKeepalive() 之前写定、会话期恒定, 心跳线程只读 —— 无数据竞争。
#include "Battle.hpp"

#include <SFML/Network.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <vector>

struct FoundHost {
    sf::IpAddress addr;
    std::string name;
};
struct NetEvent {
    enum class Kind { Connected, Disconnected, Busy, VersionMismatch, ScanDone };
    Kind kind;
};

class NetSession {
public:
    static constexpr unsigned short Port = 52021; // 避开语音 52017/518
    ~NetSession();                                 // 停心跳线程

    bool startHost(const std::string& roomName);   // false=52021 被占
    void startScan(const std::string& playerName); // 客户端: 广播 DISC 2 秒
    void startJoin(sf::IpAddress host, const std::string& playerName);

    void poll(float dt);      // 每逻辑帧驱动(非阻塞收包+过滤+状态机+周期发送)
    void close(bool sendBye); // BYE(尽力) + 停线程; Game 析构/Esc 时调用

    // 主机侧
    bool clientJoined() const;        // 等待页 -> 倒计时的条件
    const InputState& remoteInput() const;
    bool remoteReady() const;         // 双 R 重开
    void resetPeerReady();            // 进结算沿/双 R 重开点清对端 ready 位:
                                      // 对局期无 JOIN, 不清则上局的 true 冻结到
                                      // 下一局结算(终审#1 的单边重开窗口)
    bool remoteInputStale() const;    // 暂停判定: >1s 无 INP 且 KEEP 活
    void sendSnap(const proto::SnapMsg& s); // 内部 ++outTick(跨局单调)再编码发出

    // 客户端侧
    const std::vector<FoundHost>& foundHosts() const;
    const std::optional<proto::SnapMsg>& snap() const; // 最新已接受快照(>才更新)
    bool peerRecent() const;      // 对端 KEEP/任一包 5 秒内仍在(客户端兜底标签用)
    void sendInput(const InputState& in); // 倒计时/战斗/暂停期 30Hz(空闲也发全零)
    void setReady(bool r);                // R 状态随 JOIN 5Hz 上报

    std::vector<NetEvent> takeEvents();
    const std::string& peerName() const; // 对端显示名(主机侧=客户端 JOIN 上报名)

private:
    enum class Role {
        None, HostWaiting, HostSession, Scan, ScanDone, Joining, ClientSession
    };

    void handlePacket(const std::uint8_t* p, std::size_t n,
                      sf::IpAddress ip, unsigned short port);
    void sendJoin(); // JOIN(携 myReady_ 位) 发往对端主端口, poll 5Hz 分支调用
    void startKeepalive();
    void stopKeepalive(); // 防御: 重开主/客前停掉旧心跳(约定路径 close 已停)
    void replyErr(sf::IpAddress ip, unsigned short port,
                  std::uint8_t reason, std::uint8_t peerVersion);

    Role role_ = Role::None;
    sf::UdpSocket sock_;      // 主收发(非阻塞; 客户端首次 send 自动绑随机端口)
    sf::UdpSocket keepSock_;  // 心跳专用(仅后台线程 send; SFML socket 非线程安全)
    std::jthread keepThread_; // 1Hz KEEP -> 对端主端口(语音 voicePingThread 同款)
    sf::IpAddress peerIp_ = sf::IpAddress::Any; // SFML3 无默认构造, 必须显式初始化
    unsigned short peerPort_ = 0; // 对端主端口(同上)
    std::string myName_, peerName_;
    bool myReady_ = false;    // 本机 ready 位, 随 JOIN 夹带上报(setReady 写)
    bool peerReady_ = false;  // 对端 ready 位(会话内 JOIN 更新)
    InputState remoteInput_;  // 主机侧: 客户端最近上报的输入
    std::chrono::steady_clock::time_point lastAny_, lastInp_, scanStart_;
    std::uint32_t outTick_ = 0;       // 已发快照计数(跨局单调, 永不重置——C1)
    std::uint32_t lastSnapTick_ = 0;  // 最近已接受快照的 tick(客户端侧)
    std::optional<proto::SnapMsg> snap_; // 客户端侧: 最新已接受快照
    std::vector<FoundHost> hosts_;
    float joinTimer_ = 0.f, scanTimer_ = 0.f; // 5Hz 重发计时
    bool connected_ = false, disconnectedFired_ = false;
    std::vector<NetEvent> events_;
    std::uint8_t buf_[1024];
};
