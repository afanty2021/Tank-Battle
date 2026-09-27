#pragma once
// 联机协议: 消息 <-> 字节流 的纯编解码, 无 socket 可独立单测。
// 字节布局(见 spec §6): 小端; 公共头 [u8 msgId][u8 version];
// 坐标=舞台单位×16(i16), 角度=度×16(u16, 编码前折叠到 [0,360))
#include "InputState.hpp"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace proto {

inline constexpr std::uint8_t kVersion = 1;
inline constexpr std::uint8_t kErrVersion = 1; // 对端版本不符
inline constexpr std::uint8_t kErrBusy = 2;    // 对局已满/进行中
inline constexpr std::size_t kMaxName = 31;    // 名字字节上限(不含长度字节)
inline constexpr std::size_t kMaxMissiles = 64;// SNAP 导弹数解析上限

enum class Id : std::uint8_t {
    Disc = 1, Host = 2, Err = 3, Join = 4, Inp = 5, Snap = 6, Bye = 7, Keep = 8
};

// ---- 量化 ----
inline std::uint16_t encAngle(float deg) {
    const float folded = std::fmod(std::fmod(deg, 360.f) + 360.f, 360.f);
    const auto u = static_cast<std::uint32_t>(folded * 16.f + 0.5f);
    return static_cast<std::uint16_t>(u % 5760u); // 360*16
}
inline float decAngle(std::uint16_t v) { return static_cast<float>(v) / 16.f; }
inline std::int16_t encPos(float stageUnit) {
    return static_cast<std::int16_t>(std::lround(stageUnit * 16.f));
}
inline float decPos(std::int16_t v) { return static_cast<float>(v) / 16.f; }

// 快照接受规则(spec §6.2): > 应用 / = 重复跳过 / < 旧包丢弃
enum class Accept : std::uint8_t { Apply, Skip, Discard };
inline Accept acceptVerdict(std::uint32_t newTick, std::uint32_t lastTick) {
    if (newTick > lastTick) return Accept::Apply;
    if (newTick == lastTick) return Accept::Skip;
    return Accept::Discard;
}

// ---- 小端读写(双端同为 x86 Windows; 移植大端平台需重写) ----
inline void putU16(std::vector<std::uint8_t>& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}
inline void putU32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    putU16(b, static_cast<std::uint16_t>(v & 0xFFFFu));
    putU16(b, static_cast<std::uint16_t>(v >> 16));
}
struct Reader {
    const std::uint8_t* p; std::size_t n; std::size_t i = 0; bool ok = true;
    std::uint8_t u8() {
        if (i < n) return p[i++]; // 三目写法会提升为 int 触发 -Wconversion
        ok = false;
        return 0;
    }
    std::uint16_t u16() {
        if (i + 2 > n) { ok = false; return 0; }
        const std::uint16_t v = static_cast<std::uint16_t>(p[i] | (p[i + 1] << 8));
        i += 2; return v;
    }
    std::uint32_t u32() {
        return static_cast<std::uint32_t>(u16()) |
               (static_cast<std::uint32_t>(u16()) << 16);
    }
    std::string str() { // [len u8][bytes], 超长判坏
        const std::uint8_t len = u8();
        if (!ok || len > kMaxName || i + len > n) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p + i), len);
        i += len; return s;
    }
};

// ---- 消息结构 ----
struct HostMsg { std::string name; };                 // 主机对 DISC 的回应
struct ErrMsg { std::uint8_t reason = 0, peerVersion = 0; };
struct JoinMsg { std::string name; bool ready = false; };
struct InpMsg { InputState input; };
enum class Phase : std::uint8_t { Countdown = 0, Battle = 1, Over = 2, Paused = 3 };
struct TankSnap {
    float x = 0.f, y = 0.f, dir = 0.f, turret = 0.f;
    std::uint8_t state = 0, animFrame = 0; // state: 0正常 1爆炸中 2无敌
};
struct MissileSnap { float x = 0.f, y = 0.f, dir = 0.f; std::uint8_t owner = 0; };
struct EventBits { bool fire[2] = {false, false}, hit[2] = {false, false},
                      die[2] = {false, false}; };

struct SnapMsg {
    std::uint32_t tick = 0;
    Phase phase = Phase::Countdown;
    std::uint8_t hp[2] = {3, 3};
    TankSnap tanks[2];
    std::vector<MissileSnap> missiles;
    EventBits events;
};

inline std::vector<std::uint8_t> header(Id id) {
    return {static_cast<std::uint8_t>(id), kVersion};
}
inline bool idIs(const std::uint8_t* p, std::size_t n, Id id) {
    return n >= 2 && p[0] == static_cast<std::uint8_t>(id); // n>=2: 头占 2 字节
}

// ---- DISC/KEEP/BYE: 纯公共头, 无载荷 ----
inline std::vector<std::uint8_t> encodeDisc() { return header(Id::Disc); }
inline std::vector<std::uint8_t> encodeKeep() { return header(Id::Keep); }
inline std::vector<std::uint8_t> encodeBye() { return header(Id::Bye); }

// ---- HOST ----
inline std::vector<std::uint8_t> encodeHost(const std::string& name) {
    std::vector<std::uint8_t> b = header(Id::Host);
    if (name.size() > kMaxName) { b.clear(); return b; }
    b.push_back(static_cast<std::uint8_t>(name.size()));
    b.insert(b.end(), name.begin(), name.end());
    return b;
}
inline std::optional<HostMsg> decodeHost(const std::uint8_t* p, std::size_t n) {
    if (!idIs(p, n, Id::Host)) return std::nullopt;
    Reader r{p + 2, n - 2}; // 跳过 [id][ver] 两字节头
    HostMsg m; m.name = r.str();
    if (r.ok) return m;
    return std::nullopt;
}

// ---- ERR ----
inline std::vector<std::uint8_t> encodeErr(std::uint8_t reason, std::uint8_t peerVersion) {
    std::vector<std::uint8_t> b = header(Id::Err);
    b.push_back(reason); b.push_back(peerVersion);
    return b;
}
inline std::optional<ErrMsg> decodeErr(const std::uint8_t* p, std::size_t n) {
    if (!idIs(p, n, Id::Err)) return std::nullopt;
    Reader r{p + 2, n - 2}; // 跳过 [id][ver] 两字节头
    ErrMsg m; m.reason = r.u8(); m.peerVersion = r.u8();
    if (!r.ok || m.reason > kErrBusy) return std::nullopt; // reason 只定义 1/2
    return m;
}

// ---- JOIN ----
inline std::vector<std::uint8_t> encodeJoin(const std::string& name, bool ready) {
    std::vector<std::uint8_t> b = header(Id::Join);
    if (name.size() > kMaxName) { b.clear(); return b; }
    b.push_back(static_cast<std::uint8_t>(name.size()));
    b.insert(b.end(), name.begin(), name.end());
    b.push_back(ready ? 1 : 0);
    return b;
}
inline std::optional<JoinMsg> decodeJoin(const std::uint8_t* p, std::size_t n) {
    if (!idIs(p, n, Id::Join)) return std::nullopt;
    Reader r{p + 2, n - 2}; // 跳过 [id][ver] 两字节头
    JoinMsg m; m.name = r.str(); m.ready = r.u8() != 0;
    if (r.ok) return m;
    return std::nullopt;
}

// ---- INP ----
inline std::vector<std::uint8_t> encodeInp(const InputState& in) {
    std::vector<std::uint8_t> b = header(Id::Inp);
    b.push_back(in.moveBits);
    putU16(b, encAngle(in.aim));
    b.push_back(in.fire ? 1 : 0);
    return b;
}
inline std::optional<InpMsg> decodeInp(const std::uint8_t* p, std::size_t n) {
    if (!idIs(p, n, Id::Inp)) return std::nullopt;
    Reader r{p + 2, n - 2}; // 跳过 [id][ver] 两字节头
    InpMsg m;
    m.input.moveBits = r.u8();
    m.input.aim = decAngle(r.u16());
    m.input.fire = r.u8() != 0;
    if (r.ok) return m;
    return std::nullopt;
}

// ---- SNAP ----（以下实现已干跑验证：0 警告、round-trip/坏包测试全绿）
inline std::vector<std::uint8_t> encodeSnap(const SnapMsg& s) {
    std::vector<std::uint8_t> b = header(Id::Snap);
    putU32(b, s.tick);
    b.push_back(static_cast<std::uint8_t>(s.phase));
    b.push_back(s.hp[0]); b.push_back(s.hp[1]);
    for (int i = 0; i < 2; ++i) {
        const TankSnap& t = s.tanks[i];
        putU16(b, static_cast<std::uint16_t>(proto::encPos(t.x)));
        putU16(b, static_cast<std::uint16_t>(proto::encPos(t.y)));
        putU16(b, encAngle(t.dir));
        putU16(b, encAngle(t.turret));
        b.push_back(t.state); b.push_back(t.animFrame);
    }
    if (s.missiles.size() > kMaxMissiles) { b.clear(); return b; }
    b.push_back(static_cast<std::uint8_t>(s.missiles.size()));
    for (const MissileSnap& m : s.missiles) {
        putU16(b, static_cast<std::uint16_t>(proto::encPos(m.x)));
        putU16(b, static_cast<std::uint16_t>(proto::encPos(m.y)));
        putU16(b, encAngle(m.dir));
        b.push_back(m.owner);
    }
    std::uint8_t ev = 0;
    for (int i = 0; i < 2; ++i) {
        if (s.events.fire[i]) ev |= static_cast<std::uint8_t>(1u << i);
        if (s.events.hit[i])  ev |= static_cast<std::uint8_t>(1u << (i + 2));
        if (s.events.die[i])  ev |= static_cast<std::uint8_t>(1u << (i + 4));
    }
    b.push_back(ev);
    return b;
}

inline std::optional<SnapMsg> decodeSnap(const std::uint8_t* p, std::size_t n) {
    if (!idIs(p, n, Id::Snap)) return std::nullopt;
    Reader r{p + 2, n - 2}; // 跳过 [id][ver] 两字节头
    SnapMsg s;
    s.tick = r.u32();
    const std::uint8_t phase = r.u8();
    s.hp[0] = r.u8(); s.hp[1] = r.u8();
    for (int i = 0; i < 2; ++i) {
        TankSnap& t = s.tanks[i];
        t.x = decPos(static_cast<std::int16_t>(r.u16()));
        t.y = decPos(static_cast<std::int16_t>(r.u16()));
        t.dir = decAngle(r.u16());
        t.turret = decAngle(r.u16());
        t.state = r.u8(); t.animFrame = r.u8();
    }
    const std::uint8_t count = r.u8();
    if (!r.ok || count > kMaxMissiles) return std::nullopt;
    for (std::uint8_t k = 0; k < count; ++k) {
        MissileSnap m;
        m.x = decPos(static_cast<std::int16_t>(r.u16()));
        m.y = decPos(static_cast<std::int16_t>(r.u16()));
        m.dir = decAngle(r.u16());
        m.owner = r.u8();
        s.missiles.push_back(m);
    }
    const std::uint8_t ev = r.u8();
    if (!r.ok || r.i != r.n) return std::nullopt; // 尾部多字节=坏包
    for (int i = 0; i < 2; ++i) {
        s.events.fire[i] = (ev & (1u << i)) != 0;
        s.events.hit[i]  = (ev & (1u << (i + 2))) != 0;
        s.events.die[i]  = (ev & (1u << (i + 4))) != 0;
    }
    // 解析校验(spec §6.2): 任一违反整包丢弃, 绝不越界
    if (static_cast<std::uint8_t>(phase) > 3) return std::nullopt;
    s.phase = static_cast<Phase>(phase);
    if (s.hp[0] > 3 || s.hp[1] > 3) return std::nullopt;
    for (int i = 0; i < 2; ++i) {
        if (s.tanks[i].state > 2) return std::nullopt;
        if (s.tanks[i].state == 1 && s.tanks[i].animFrame > 5) return std::nullopt;
    }
    for (const MissileSnap& m : s.missiles)
        if (m.owner > 1) return std::nullopt;
    return s;
}

} // namespace proto
