# 局域网 1v1 对战 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 tank-battle-cpp 新增局域网 1v1 坦克对战（主机权威 + 快照同步，UDP 52021），单人模式零改动。

**Architecture:** 四个新模块单向分层：InputState（输入归一化）→ Protocol（纯编解码，头文件）→ Battle（纯逻辑 PvP 模拟，BattleDefs 数值注入）→ NetSession（socket/心跳/过滤）；最后接进 Game/main。全 UDP 无 ack 层，消息=幂等现状广播（大厅 5Hz / 战斗 30Hz）。

**Tech Stack:** C++20 / SFML 3（network 已在链路）/ MSYS2 UCRT64 g++ / Python 3 冒烟脚本。

**Spec:** `docs/superpowers/specs/2026-09-27-lan-multiplayer-design.md`（rev3，已冻结）。计划与 spec 冲突时以 spec 为准。

## Global Constraints

- 编译器：`D:\Scoop\apps\msys2\current\ucrt64` 的 g++，`-std=c++20 -Wall -Wextra -Wshadow -Wconversion` **0 警告**（所有收窄转换显式 `static_cast`）。
- 唯一验证路线：`cd tank-battle-cpp && build.bat` 四步全绿。本机安全软件破坏 PATH 搜索，g++ 单独调用时用绝对路径 `"/d/Scoop/apps/msys2/current/ucrt64/bin/g++.exe"` 或加 `-B`。
- **凡改 `src/Game.*` 或 `src/main.cpp` 的任务收尾必跑单人冒烟**：
  `powershell.exe -ExecutionPolicy Bypass -File tools/run_and_shoot.ps1 -Phase playing`
- **所有 .bat 编辑后必须恢复 CRLF**（cmd 解析括号块会错乱），校验命令见 Task 2。
- Python 用 `F:/program files/python313/python.exe`；Git Bash 的 coreutils 损坏，别用 `ls`/`cat`。
- 端口：联机 UDP **52021**（语音 52017/518 不动）。
- 纯度约束：`InputState.hpp` 不 include SFML；`Protocol.hpp` 不 include SFML；`Battle.hpp/cpp` 只允许 include `common.hpp`（其仅依赖 `<SFML/System.hpp>`）与 `Protocol.hpp`。**三者都不得 include Assets.hpp / SFML Graphics/Audio/Network。**
- 单人模式行为零改动：net 分支一律以 `mode != Mode::Solo` 为闸；PvP 新数值只进 `BattleDefs`，不进 `Game.hpp` 单人常量区。
- `common.hpp`、`src/Assets.*`、`assets/`、`lessons/` 一律不改。
- 两台机器跑同一份构建产物（拷目录），协议 version=1。
- 提交信息用中文一行式（见 git log 现有风格）。

## Review Focus

spec 隐含但易咬人的输入类/失效模式，逐条钉进 owning task 的测试：

1. **坏包/敌意包**（截断、未知 msgId、phase=4、state=3、state==1 且 animFrame=6、owner=2、hp=4、missileCount=65、名字>31B、非对端 IP）→ 整包丢弃，绝不越界/崩溃 → Task 1 测试 + Task 3 过滤。
2. **重复/乱序/丢失快照与跨局 tick**：`>` 应用、`=` 跳过（事件不重放）、`<` 丢弃；重开后 tick 单调不回退 → Task 1（accept 规则纯函数）+ Task 2（重置不改 tick 的调用契约）。
3. **暂停可见性（N1）**：暂停期 tick 照增，phase=3 快照必须被应用并显示 → Task 1（accept(T+1)=Apply）+ Task 4（冒烟停发 INP 1.5s 断言收到 phase=3）。
4. **导弹属主与同 tick 双亡**：出生帧不自伤；双亡=平局(winner=2) → Task 2。
5. **单人回归**：任何 Game/main 改动后单人行为不变 → Task 5/6/8 冒烟。

---

### Task 0: 工作区清场（执行前置，5 分钟）

**Files:** 无代码改动；只保证后续提交不混入无关工作流。

- [ ] **Step 1: 检查并隔离无关改动**

Run: `cd /d/Berton/Tank-Battle && git status --short`
Expected: 除 docs/ 下计划/spec 外为空。**若存在 `tank-battle-cpp/lessons`、字体等未提交改动（教学线工作流），必须先隔离**——它们不属于联机提交，且 Task 7/8 的 git add 会误吞：

Run: `git stash push -m "lessons WIP (非联机改动, 联机执行期间暂存)" -- tank-battle-cpp/lessons tank-battle-cpp/assets/fonts && git status --short`
Expected: 只剩 docs/。记下 stash（Task 7 的字体重生成已含教学字形全集——subset_font.py 的 TEXT 本就包含课例文字——pop 回来冲突时以重新生成版为准）。

- [ ] **Step 2: 确认分支与基线**

Run: `git branch --show-current && git log --oneline -1`
Expected: `feature/lan-multiplayer`，HEAD 为本计划提交。

---

### Task 1: InputState.hpp + Protocol.hpp（纯编解码，TDD）

**Files:**
- Create: `tank-battle-cpp/src/InputState.hpp`
- Create: `tank-battle-cpp/src/Protocol.hpp`
- Modify: `tank-battle-cpp/tests/unit_tests.cpp`（文末追加测试函数 + main() 里调用）

**Interfaces:**
- Consumes: 无。
- Produces（后续任务依赖的精确签名）:
  - `struct InputState { std::uint8_t moveBits; float aim; bool fire; }`（bit0=W bit1=S bit2=A bit3=D）
  - `namespace proto`：`kVersion=1`；`enum class Id:std::uint8_t{Disc=1,Host=2,Err=3,Join=4,Inp=5,Snap=6,Bye=7,Keep=8}`；`kErrVersion=1, kErrBusy=2, kMaxName=31, kMaxMissiles=64`
  - 量化：`uint16_t encAngle(float)`（先折叠 [0,360) 再 ×16）、`float decAngle(uint16_t)`、`int16_t encPos(float)`、`float decPos(int16_t)`
  - `enum class Accept:std::uint8_t{Apply,Skip,Discard}` + `Accept acceptVerdict(uint32_t newTick, uint32_t lastTick)`
  - 消息结构与编解码（全部 `std::vector<std::uint8_t> encodeX(...)` / `std::optional<X> decodeX(const std::uint8_t*, std::size_t)`）：
    `HostMsg{std::string name}`、`ErrMsg{uint8_t reason,peerVersion}`、`JoinMsg{std::string name;bool ready}`、`InpMsg{InputState input}`、`TankSnap{float x,y,dir,turret;uint8_t state,animFrame}`、`MissileSnap{float x,y,dir;uint8_t owner}`、`enum class Phase:std::uint8_t{Countdown=0,Battle=1,Over=2,Paused=3}`、`EventBits{bool fire[2],hit[2],die[2]}`、`SnapMsg{uint32_t tick;Phase phase;uint8_t hp[2];TankSnap tanks[2];std::vector<MissileSnap> missiles;EventBits events}`
  - SNAP 解析校验（任一违反返回 `std::nullopt`）：phase>3、state>2、state==1 且 animFrame>5、owner>1、hp>3、missile 数>kMaxMissiles、截断。

- [ ] **Step 1: 写失败测试**（`tests/unit_tests.cpp` 文末追加；沿用 CHECK/near 模式）

```cpp
// ---------------- 联机协议(Protocol.hpp) ----------------
#include "../src/Protocol.hpp"

static void testProtoQuantize() {
    // 角度: 折叠到 [0,360) 后 ×16, 往返误差 <= 1/16 度
    for (float a : {-90.f, 0.f, 359.99f, 360.f, 720.f, -0.01f}) {
        const float back = proto::decAngle(proto::encAngle(a));
        CHECK(back >= 0.f && back < 360.f);
        float folded = std::fmod(std::fmod(a, 360.f) + 360.f, 360.f);
        if (folded >= 360.f - 1.f / 16.f) folded = 0.f; // encAngle 四舍五入可到 360*16->取模归 0
        CHECK(std::abs(back - folded) <= 1.f / 16.f + 1e-4f);
    }
    CHECK(proto::encAngle(-90.f) == proto::encAngle(270.f)); // 负角折叠
    CHECK(proto::encAngle(359.99f) < 5760);                  // 不越 360*16
    // 坐标: 往返误差 <= 1/16 舞台单位
    for (float v : {-240.f, -211.f, 0.f, 137.5f, 240.f})
        CHECK(std::abs(proto::decPos(proto::encPos(v)) - v) <= 1.f / 16.f + 1e-4f);
}

static void testProtoRoundTrip() {
    const proto::HostMsg h{"room-01"};
    auto eh = proto::encodeHost(h.name);
    auto dh = proto::decodeHost(eh.data(), eh.size());
    CHECK(dh && dh->name == "room-01");

    const proto::ErrMsg e{proto::kErrBusy, 7};
    auto ee = proto::encodeErr(e.reason, e.peerVersion);
    auto de = proto::decodeErr(ee.data(), ee.size());
    CHECK(de && de->reason == proto::kErrBusy && de->peerVersion == 7);

    const proto::JoinMsg j{"player-2", true};
    auto ej = proto::encodeJoin(j.name, j.ready);
    auto dj = proto::decodeJoin(ej.data(), ej.size());
    CHECK(dj && dj->name == "player-2" && dj->ready);

    const InputState in{0x0B, 123.4f, true};
    auto ei = proto::encodeInp(in);
    auto di = proto::decodeInp(ei.data(), ei.size());
    CHECK(di && di->input.moveBits == 0x0B && di->input.fire &&
          near(di->input.aim, 123.4f, 1.f / 16.f + 1e-3f));

    proto::SnapMsg s{};
    s.tick = 4000000000u; s.phase = proto::Phase::Battle;
    s.hp[0] = 2; s.hp[1] = 3;
    s.tanks[1] = {10.5f, -20.25f, 90.f, 45.f, 1, 3};
    s.missiles.push_back({5.f, 5.f, 0.f, 0});
    s.missiles.push_back({-5.f, -5.f, 180.f, 1});
    s.events.fire[1] = s.events.hit[0] = s.events.die[0] = true;
    auto es = proto::encodeSnap(s);
    auto ds = proto::decodeSnap(es.data(), es.size());
    CHECK(ds && ds->tick == 4000000000u && ds->phase == proto::Phase::Battle);
    CHECK(ds && ds->tanks[1].animFrame == 3 && ds->tanks[1].state == 1);
    CHECK(ds && ds->missiles.size() == 2 && ds->missiles[1].owner == 1);
    CHECK(ds && ds->events.die[0] && !ds->events.fire[0]);
}

static void testProtoBadPackets() {
    // 截断: 每种消息砍掉最后一个字节都必须失败
    auto eh = proto::encodeHost("ab");
    CHECK(!proto::decodeHost(eh.data(), eh.size() - 1));
    auto ej = proto::encodeJoin("ab", false);
    CHECK(!proto::decodeJoin(ej.data(), ej.size() - 1));
    auto ei = proto::encodeInp(InputState{});
    CHECK(!proto::decodeInp(ei.data(), ei.size() - 1));
    proto::SnapMsg s{}; s.phase = proto::Phase::Countdown; s.hp[0] = s.hp[1] = 3;
    auto es = proto::encodeSnap(s);
    CHECK(!proto::decodeSnap(es.data(), es.size() - 1));
    // 首字节 msgId 不匹配
    CHECK(!proto::decodeHost(es.data(), es.size()));
    // 名字超长(31B 上限): encode 返回空包, decode 也必须拒绝
    const std::string longName(40, 'x');
    auto elong = proto::encodeJoin(longName, false);
    CHECK(elong.empty() &&
          !proto::decodeJoin(elong.data(), elong.size()));
    // SNAP 非法枚举/越界值 -> 整包 nullopt(不崩溃)
    // 字节偏移: [0]id [1]ver [2..5]tick [6]phase [7]hp0 [8]hp1
    //           [9..18]tank0(x2y2dir2turret2state1frame1) [19..28]tank1 ...
    auto mutate = [&](int patchIdx, std::uint8_t v) {
        auto b = proto::encodeSnap(s);
        b[patchIdx] = v;
        return proto::decodeSnap(b.data(), b.size()).has_value();
    };
    CHECK(!mutate(6, 4));   // phase=4
    CHECK(!mutate(7, 4));   // hp0=4
    CHECK(!mutate(17, 3));  // tank0.state=3
    CHECK(!mutate(27, 3));  // tank1.state=3
    CHECK(mutate(18, 6));   // state==0 时 animFrame 不校验(仅 state==1 校验)
    // state==1 且 animFrame>5(客户端拿它当 6 帧数组下标)
    proto::SnapMsg boom = s;
    boom.tanks[0].state = 1; boom.tanks[0].animFrame = 6;
    CHECK(!proto::decodeSnap(proto::encodeSnap(boom).data(),
                             proto::encodeSnap(boom).size()));
    boom.tanks[0].animFrame = 5; // 边界值 5 合法
    CHECK(proto::decodeSnap(proto::encodeSnap(boom).data(),
                            proto::encodeSnap(boom).size()));
    // owner>1 / missileCount>64
    proto::SnapMsg many = s; many.missiles.resize(65, {0.f, 0.f, 0.f, 0});
    CHECK(!proto::decodeSnap(proto::encodeSnap(many).data(),
                             proto::encodeSnap(many).size()));
    proto::SnapMsg own = s; own.missiles.push_back({0.f, 0.f, 0.f, 2});
    CHECK(!proto::decodeSnap(proto::encodeSnap(own).data(),
                             proto::encodeSnap(own).size()));
    // ERR reason 只定义 1/2, 3=坏包
    auto ebad = proto::encodeErr(3, 1);
    CHECK(!proto::decodeErr(ebad.data(), ebad.size()));
}

static void testSnapAcceptRule() {
    using A = proto::Accept;
    CHECK(proto::acceptVerdict(101, 100) == A::Apply);
    CHECK(proto::acceptVerdict(100, 100) == A::Skip);   // 重复包: 事件不重放
    CHECK(proto::acceptVerdict(99, 100) == A::Discard); // 旧包
    // (暂停可见性 N1 的端到端验证在 Task 4 冒烟第 7 项, 纯函数层无增量可测)
}
```

在 `int main()`（unit_tests.cpp:203）里现有调用后追加：

```cpp
    testProtoQuantize();
    testProtoRoundTrip();
    testProtoBadPackets();
    testSnapAcceptRule();
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cd /d/Berton/Tank-Battle/tank-battle-cpp && "/d/Scoop/apps/msys2/current/ucrt64/bin/g++.exe" -B"D:/Scoop/apps/msys2/current/ucrt64/bin/" -std=c++20 -O1 -Wall -Wextra -Wshadow -Wconversion tests/unit_tests.cpp -o unit_tests.exe -lsfml-system && ./unit_tests.exe`
Expected: 编译失败 `Protocol.hpp: No such file or directory`（正是要的红）。

- [ ] **Step 3: 写实现**

`src/InputState.hpp`：

```cpp
#pragma once
// 一帧玩家意图(联机双方公共输入格式): 键鼠与语音口令在 Game 侧归一化成它
#include <cstdint>

struct InputState {
    std::uint8_t moveBits = 0; // bit0=W上 bit1=S下 bit2=A左 bit3=D右
    float aim = 0.f;           // 炮塔绝对朝向(Scratch 方向, 度; 发送方本地算好)
    bool fire = false;         // 发射键按住(或语音"开炮"冷却窗口期)
};
```

`src/Protocol.hpp`（全部 inline，头文件即实现）：

```cpp
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
```

注意：`decodeSnap` 读 `phase` 时先按裸 u8 存，校验后再转枚举（`Phase` 只有 0-3）。

- [ ] **Step 4: 跑测试确认通过**

Run: 同 Step 2 命令。
Expected: `PASS`（unit_tests 无 failures 输出，main 末尾打印全部通过）。

- [ ] **Step 5: 跑完整构建（四步全绿）**

Run: `cd /d/Berton/Tank-Battle/tank-battle-cpp && cmd.exe //c build.bat`
Expected: `[4/4] done.`

- [ ] **Step 6: Commit**

```bash
git add src/InputState.hpp src/Protocol.hpp tests/unit_tests.cpp
git commit -m "联机协议层：InputState 输入归一化 + Protocol 纯编解码（小端/角度折叠/快照校验），含接受规则与坏包单测"
```

---

### Task 2: Battle.hpp/cpp（PvP 纯逻辑模拟，TDD）

**Files:**
- Create: `tank-battle-cpp/src/Battle.hpp`
- Create: `tank-battle-cpp/src/Battle.cpp`
- Modify: `tank-battle-cpp/tests/unit_tests.cpp`
- Modify: `tank-battle-cpp/build.bat:42-45`（游戏编译行）与 `build.bat:52-53`（单测编译行）：各追加 `src/Battle.cpp`

**Interfaces:**
- Consumes: `common.hpp` 的 `stage::dirVector/boxHitTest/rotatedBoxAABB`；`Protocol.hpp` 的 `proto::SnapMsg/TankSnap/MissileSnap/Phase/EventBits`（做 makeSnap 胶水）。
- Produces:
  - `struct BattleDefs`（见下方代码，默认值即运行值）
  - `struct BattleTank/BattleMissile`、`enum class TankState:uint8_t{Alive,Exploding,Invulnerable}`、`enum class BattlePhase{Countdown,Battle,Over}`
  - `struct BattleState{BattlePhase phase;float countdown;BattleTank tanks[2];std::vector<BattleMissile> missiles;int hp[2];int winner;bool fired[2],hit[2],died[2];}`（winner: -1 进行中 / 0,1 胜者 / 2 平局）
  - `void resetBattle(BattleState&, const BattleDefs&)`
  - `void stepBattle(BattleState&, const BattleDefs&, const InputState in[2], float dt)`（dt=1/30 固定步长；暂停=调用方不调用本函数）
  - `proto::SnapMsg makeSnap(const BattleState&, bool paused)`（tick 字段留 0，由 NetSession 填）

- [ ] **Step 1: 写失败测试**（unit_tests.cpp 追加，`#include "../src/Battle.hpp"`）

```cpp
// ---------------- PvP 战斗模拟(Battle) ----------------
#include "../src/Battle.hpp"

// 小数值测试配置(不依赖素材): 场地 ±100, 速度 10, 出生点分离
static BattleDefs testDefs() {
    BattleDefs d;
    d.playerSpeed = 10.f; d.missileSpeed = 100.f; d.fireCooldown = 0.5f;
    d.minX = -100.f; d.maxX = 100.f; d.minY = -100.f; d.maxY = 100.f;
    d.tankHalfExtents = {10.f, 10.f}; d.tankCenterOffset = {0.f, 0.f};
    d.tankSize = 1.f;
    d.missileHalfExtents = {2.f, 2.f}; d.missileCenterOffset = {0.f, 0.f};
    d.missileSize = 1.f;
    d.spawn[0] = {0.f, -50.f}; d.spawnDir[0] = 0.f;
    d.spawn[1] = {0.f, 50.f};  d.spawnDir[1] = 180.f;
    return d;
}
static InputState idle() { return {}; }

static void testBattleCountdownThenControls() {
    BattleDefs d = testDefs(); BattleState s; resetBattle(s, d);
    InputState in[2] = {idle(), idle()};
    // 步进到倒计时结束(90 步=2.997s, 浮点累积误差可能差 1 步, 用条件循环)
    for (int i = 0; i < 120 && s.phase == BattlePhase::Countdown; ++i)
        stepBattle(s, d, in, 1.f / 30.f);
    CHECK(s.phase == BattlePhase::Battle);
    in[0].moveBits = 0x01; // W
    stepBattle(s, d, in, 1.f / 30.f);
    CHECK(s.tanks[0].pos.y > d.spawn[0].y);            // 倒计时后才动
    CHECK(s.tanks[0].dir == 0.f);
    in[0].aim = 123.f; stepBattle(s, d, in, 1.f / 30.f);
    CHECK(near(s.tanks[0].turret, 123.f));             // 炮塔=上报绝对角
}

static void testBattleFenceAndCooldown() {
    BattleDefs d = testDefs(); BattleState s; resetBattle(s, d);
    s.phase = BattlePhase::Battle;
    InputState in[2] = {idle(), idle()};
    in[0].moveBits = 0x01;                              // 一路向上顶到 maxY
    for (int i = 0; i < 600; ++i) stepBattle(s, d, in, 1.f / 30.f);
    CHECK(s.tanks[0].pos.y <= d.maxY);
    // 连发冷却: 首帧可发, 下一帧(同按住)不可。注意开火帧导弹即前进一步
    // (与单人同帧序一致), 炮口必须朝向开阔方向: 朝右(90°)时导弹路径
    // (0,100)->(100,100)->(200,100) 全程在舞台 ±240/±180 内不会出界消失
    in[0].moveBits = 0; in[0].fire = true; in[0].aim = 90.f;
    stepBattle(s, d, in, 1.f / 30.f);
    CHECK(s.missiles.size() == 1 && s.fired[0]);
    stepBattle(s, d, in, 1.f / 30.f);
    CHECK(s.missiles.size() == 1 && !s.fired[0]);
}

static void testBattleMissileOwnerNoSelfHit() {
    BattleDefs d = testDefs(); BattleState s; resetBattle(s, d);
    s.phase = BattlePhase::Battle;
    s.tanks[0].pos = {50.f, -50.f}; // 错开 x: 导弹路径 x=50 不经过任何坦克
    InputState in[2] = {idle(), idle()};     // (双方原出生点相距恰 100=导弹一步,
    in[0].fire = true; in[0].aim = 0.f;      //  原地朝上开炮会开火帧即命中对方)
    stepBattle(s, d, in, 1.f / 30.f); // 出生帧即前进一步: (50,-50)->(50,50)
    in[0].fire = false;
    CHECK(s.missiles.size() == 1);    // 没被"打到自己"吞掉
    CHECK(s.hp[0] == 3 && s.hp[1] == 3 && // 也没打到对方(路径已错开)
          s.tanks[0].state == TankState::Alive);
    for (int i = 0; i < 6; ++i) stepBattle(s, d, in, 1.f / 30.f);
    CHECK(s.missiles.empty());        // 飞出舞台上界即消失
    CHECK(s.hp[0] == 3 && s.hp[1] == 3);
}

static void testBattleHitHpRespawnInvuln() {
    BattleDefs d = testDefs(); BattleState s; resetBattle(s, d);
    s.phase = BattlePhase::Battle;
    // 导弹每步 100 > 命中窗(±12), 必须放"下一步落点"上: 坦克1 在 (0,50),
    // 导弹放 (0,-40) 朝上 -> 下一步到 (0,60), |60-50|=10 <= 12 命中。
    // 该落点同时穿过坦克0 的命中区(它在自己出生点 (0,-50)), 顺带验证属主跳过
    s.missiles.push_back({{0.f, -40.f}, 0.f, 0});
    InputState in[2] = {idle(), idle()};
    stepBattle(s, d, in, 1.f / 30.f);
    CHECK(s.hit[1] && s.hp[1] == 2);
    CHECK(s.tanks[1].state == TankState::Exploding);    // 冻结: 不可动不可发
    in[1].moveBits = 0x01; in[1].fire = true;
    stepBattle(s, d, in, 1.f / 30.f);
    CHECK(s.tanks[1].pos == d.spawn[1]);                // 没动
    in[1].moveBits = 0; in[1].fire = false;             // 清输入, 别让测试自造导弹
    // 爆炸 6 帧后回出生点+无敌; 转换帧上朝向/炮塔被重置为 spawnDir(180),
    // 之后各帧 turret 恒等于上报 aim(炮塔永远跟随输入)——所以炮塔断言
    // 只能在转换帧当步做
    bool respawned = false;
    for (int i = 0; i < 30 && !respawned; ++i) {
        stepBattle(s, d, in, 1.f / 30.f);
        if (s.tanks[1].state == TankState::Invulnerable) {
            respawned = true;
            CHECK(s.tanks[1].pos == d.spawn[1] && s.tanks[1].dir == 180.f);
            CHECK(near(s.tanks[1].turret, 180.f));
        }
    }
    CHECK(respawned);
    // 无敌期导弹正中也不扣血: 放"下一步正好落在坦克1 身上"的导弹
    s.missiles.push_back({{0.f, -50.f}, 0.f, 0}); // 下一步到 (0,50)=坦克1 中心
    for (int i = 0; i < 5; ++i) stepBattle(s, d, in, 1.f / 30.f);
    CHECK(s.hp[1] == 2 && s.tanks[1].state == TankState::Invulnerable);
    // 无敌 1.5s 后恢复可被击中
    for (int i = 0; i < 50; ++i) stepBattle(s, d, in, 1.f / 30.f);
    CHECK(s.tanks[1].state == TankState::Alive);
}

static void testBattleFatalFreezeAndDraw() {
    BattleDefs d = testDefs();
    // 致命一击: hp1=1, 命中即全场冻结(爆炸动画也不播), 胜者=0
    {
        BattleState s; resetBattle(s, d); s.phase = BattlePhase::Battle;
        s.hp[1] = 1;
        s.missiles.push_back({{0.f, -40.f}, 0.f, 0}); // 下一步命中坦克1
        InputState in[2] = {idle(), idle()};
        stepBattle(s, d, in, 1.f / 30.f);
        CHECK(s.phase == BattlePhase::Over && s.winner == 0 && s.died[1]);
        const int frame = s.tanks[1].animFrame;
        for (int i = 0; i < 30; ++i) stepBattle(s, d, in, 1.f / 30.f);
        CHECK(s.tanks[1].animFrame == frame); // 计时器全停(禁墙钟)
    }
    // 同 tick 双亡 -> 平局(两发导弹同帧各命中一人)
    {
        BattleState s; resetBattle(s, d); s.phase = BattlePhase::Battle;
        s.hp[0] = s.hp[1] = 1;
        s.missiles.push_back({{0.f, -40.f}, 0.f, 0});    // 下一步命中坦克1
        s.missiles.push_back({{0.f, 40.f}, 180.f, 1});   // 下一步到 (0,-60), 距坦克0(0,-50) 10 命中
        InputState in[2] = {idle(), idle()};
        stepBattle(s, d, in, 1.f / 30.f);
        CHECK(s.phase == BattlePhase::Over && s.winner == 2);
        CHECK(s.died[0] && s.died[1]);
    }
}

static void testBattleResetKeepsCallerTickContract() {
    // C1 契约: resetBattle 只重置战斗, 不碰任何网络计数;
    // tick 由 NetSession 持有且永不回退 —— 这里锁 resetBattle 的完整性
    BattleDefs d = testDefs();
    BattleState s; resetBattle(s, d);
    s.phase = BattlePhase::Over; s.winner = 0; s.hp[0] = 0;
    s.missiles.push_back({{}, 0.f, 0});
    resetBattle(s, d);
    CHECK(s.phase == BattlePhase::Countdown && s.winner == -1);
    CHECK(s.hp[0] == 3 && s.hp[1] == 3 && s.missiles.empty());
    CHECK(s.tanks[0].pos == d.spawn[0] && s.tanks[1].pos == d.spawn[1]);
}

static void testBattleDeterminism() {
    BattleDefs d = testDefs();
    BattleState a, b; resetBattle(a, d); resetBattle(b, d);
    InputState in[2] = {{0x09, 30.f, true}, {0x02, 200.f, false}};
    for (int i = 0; i < 300; ++i) {
        stepBattle(a, d, in, 1.f / 30.f);
        stepBattle(b, d, in, 1.f / 30.f);
    }
    for (int i = 0; i < 2; ++i) {
        CHECK(a.tanks[i].pos == b.tanks[i].pos);
        CHECK(a.hp[i] == b.hp[i] && a.tanks[i].state == b.tanks[i].state);
    }
    CHECK(a.missiles.size() == b.missiles.size());
}

static void testMakeSnap() {
    BattleDefs d = testDefs(); BattleState s; resetBattle(s, d);
    s.phase = BattlePhase::Battle; s.hp[1] = 2;
    s.missiles.push_back({{1.f, 2.f}, 90.f, 1});
    const proto::SnapMsg snap = makeSnap(s, false);
    CHECK(snap.phase == proto::Phase::Battle && snap.hp[1] == 2);
    CHECK(snap.missiles.size() == 1 && snap.missiles[0].owner == 1);
    const proto::SnapMsg paused = makeSnap(s, true);
    CHECK(paused.phase == proto::Phase::Paused);       // 暂停标签可见性(N1)
}
```

main() 追加调用这 7 个函数。

- [ ] **Step 2: 跑测试确认失败**

Run: `cd /d/Berton/Tank-Battle/tank-battle-cpp && "/d/Scoop/apps/msys2/current/ucrt64/bin/g++.exe" -B"D:/Scoop/apps/msys2/current/ucrt64/bin/" -std=c++20 -O1 -Wall -Wextra -Wshadow -Wconversion tests/unit_tests.cpp src/Battle.cpp -o unit_tests.exe -lsfml-system && ./unit_tests.exe`
Expected: 编译失败 `Battle.hpp: No such file`。

- [ ] **Step 3: 写实现**

`src/Battle.hpp`：

```cpp
#pragma once
// 1v1 PvP 战斗模拟(纯逻辑, 只依赖 common.hpp + Protocol.hpp, 可独立单测)。
// 与单人模式刻意不共享运动代码(PvP 行为不同: 中弹不死/重生/无敌),
// 共用的是 common.hpp 的坐标/碰撞原语。所有数值经 BattleDefs 注入:
// 运行值由 Game 开局注入(Game.hpp 常量是 private, Assets 元数据运行期才有),
// 单测用任意小数值构造。PvP 新增数值(出生点/血量/无敌/冻结)不是 .sb3 来源。
#include "InputState.hpp"
#include "Protocol.hpp"
#include "common.hpp"

#include <vector>

struct BattleDefs {
    // ---- 沿用原版单人常量(Game 注入 Game.hpp 的值) ----
    float playerSpeed = 5.f;     // PlayerSpeed
    float missileSpeed = 10.f;   // MissileSpeed
    float fireCooldown = 0.5f;   // FireCooldown
    float minX = -211.f, maxX = 205.f, minY = -154.f, maxY = 150.f; // 玩家围栏
    // 命中盒: 注意 boxHitTest 内部会乘 size, 这里存 100% 尺寸的原始值 + 乘数
    sf::Vector2f tankHalfExtents{1.f, 1.f};      // Assets::playerBody.halfExtents
    sf::Vector2f tankCenterOffset{0.f, 0.f};     // Assets::playerBody.centerOffset
    float tankSize = 0.30f;                      // 与单人渲染/判定同乘数
    sf::Vector2f missileHalfExtents{1.f, 1.f};   // Assets::missile.halfExtents
    sf::Vector2f missileCenterOffset{0.f, 0.f};  // Assets::missile.centerOffset
    float missileSize = 0.50f;
    // ---- PvP 新增(非 .sb3 来源) ----
    int maxHp = 3;
    float frameTime = 0.1f;      // 爆炸帧时长(同原版 6 帧×0.1s)
    float explosionTime = 0.6f;  // 行为级反转: 原版爆炸期间可继续驾驶, PvP 冻结
    float invulnTime = 1.5f;     // 原版无无敌概念
    sf::Vector2f spawn[2] = {{0.f, -120.f}, {0.f, 120.f}};
    float spawnDir[2] = {0.f, 180.f}; // 主机底朝上, 客户端顶朝下, 炮口互指
};

enum class TankState : std::uint8_t { Alive = 0, Exploding = 1, Invulnerable = 2 };
enum class BattlePhase { Countdown, Battle, Over };

struct BattleTank {
    sf::Vector2f pos; float dir = 0.f, turret = 0.f;
    TankState state = TankState::Alive;
    int animFrame = 0;       // 爆炸帧 0..5
    float animTimer = 0.f;
    float invulnTimer = 0.f;
    float cooldown = 0.f;
};

struct BattleMissile { sf::Vector2f pos; float dir; std::uint8_t owner; };

struct BattleState {
    BattlePhase phase = BattlePhase::Countdown;
    float countdown = 3.f;
    BattleTank tanks[2];
    std::vector<BattleMissile> missiles;
    int hp[2] = {3, 3};
    int winner = -1;               // -1 进行中 / 0,1 胜者 / 2 平局
    bool fired[2] = {false, false}; // 本帧事件(供快照 events 位, 每帧开头清零)
    bool hit[2] = {false, false};
    bool died[2] = {false, false};
};

void resetBattle(BattleState& s, const BattleDefs& d);
// 固定步长推进(dt=1/30)。暂停=调用方不调(网络层 tick 照增, 见 spec §6.3)
void stepBattle(BattleState& s, const BattleDefs& d, const InputState in[2], float dt);
// 战斗态 -> 快照胶水(tick 留 0 由 NetSession 填; paused 把 phase 映射为 3)
proto::SnapMsg makeSnap(const BattleState& s, bool paused);
```

`src/Battle.cpp`：

```cpp
#include "Battle.hpp"

void resetBattle(BattleState& s, const BattleDefs& d) {
    s = BattleState{};
    for (int i = 0; i < 2; ++i) {
        s.tanks[i].pos = d.spawn[i];
        s.tanks[i].dir = d.spawnDir[i];
        s.tanks[i].turret = d.spawnDir[i];
    }
    s.hp[0] = s.hp[1] = d.maxHp;
}

void stepBattle(BattleState& s, const BattleDefs& d, const InputState in[2],
                float dt) {
    for (int i = 0; i < 2; ++i) s.fired[i] = s.hit[i] = s.died[i] = false;

    if (s.phase == BattlePhase::Countdown) {
        for (int i = 0; i < 2; ++i) s.tanks[i].turret = in[i].aim;
        s.countdown -= dt;
        if (s.countdown <= 0.f) s.phase = BattlePhase::Battle;
        return;
    }
    if (s.phase == BattlePhase::Over) return; // 全场冻结: 一切计时器停

    for (int i = 0; i < 2; ++i) {
        BattleTank& t = s.tanks[i];
        t.turret = in[i].aim; // 炮塔=输入方本地算好的绝对角
        t.cooldown -= dt;
        if (t.state == TankState::Exploding) {
            // 冻结: 不可移动/开火/被判定(导弹穿过)
            t.animTimer -= dt;
            if (t.animTimer <= 0.f) {
                ++t.animFrame;
                t.animTimer = d.frameTime;
            }
            if (t.animFrame >= 6) { // 回出生点+无敌, 朝向/炮塔重置
                t.pos = d.spawn[i];
                t.dir = d.spawnDir[i];
                t.turret = d.spawnDir[i];
                t.state = TankState::Invulnerable;
                t.invulnTimer = d.invulnTime;
                t.cooldown = 0.f;
            }
            continue;
        }
        if (t.state == TankState::Invulnerable) {
            t.invulnTimer -= dt;
            if (t.invulnTimer <= 0.f) t.state = TankState::Alive;
        }
        // WASD: 与单人 Game::updatePlayer 相同的逐键围栏语义(W,S,A,D 顺序)
        if ((in[i].moveBits & 0x01) != 0 && t.pos.y < d.maxY) {
            t.dir = 0.f; t.pos.y += d.playerSpeed;
        }
        if ((in[i].moveBits & 0x02) != 0 && t.pos.y > d.minY) {
            t.dir = 180.f; t.pos.y -= d.playerSpeed;
        }
        if ((in[i].moveBits & 0x04) != 0 && t.pos.x > d.minX) {
            t.dir = -90.f; t.pos.x -= d.playerSpeed;
        }
        if ((in[i].moveBits & 0x08) != 0 && t.pos.x < d.maxX) {
            t.dir = 90.f; t.pos.x += d.playerSpeed;
        }
        if (in[i].fire && t.cooldown <= 0.f) {
            s.missiles.push_back({t.pos, t.turret, static_cast<std::uint8_t>(i)});
            t.cooldown = d.fireCooldown;
            s.fired[i] = true;
        }
    }

    // 导弹: 前进 -> 出界消失 -> 只与对方坦克判定(出生帧不自伤)
    for (std::size_t k = 0; k < s.missiles.size();) {
        BattleMissile& m = s.missiles[k];
        m.pos += stage::dirVector(m.dir) * d.missileSpeed;
        if (stage::rotatedBoxAABB(d.missileHalfExtents, d.missileCenterOffset,
                                  d.missileSize, m.pos, m.dir)
                .overlapsStageEdge()) {
            s.missiles.erase(s.missiles.begin() + static_cast<std::ptrdiff_t>(k));
            continue;
        }
        const int foe = 1 - static_cast<int>(m.owner);
        BattleTank& ft = s.tanks[foe];
        if (ft.state == TankState::Alive &&
            stage::boxHitTest(d.tankHalfExtents, d.tankCenterOffset, d.tankSize,
                              ft.pos, ft.dir, m.pos,
                              d.missileHalfExtents * d.missileSize)) {
            s.hp[foe] -= 1;
            s.hit[foe] = true;
            ft.state = TankState::Exploding;
            ft.animFrame = 0;
            ft.animTimer = d.frameTime;
            s.missiles.erase(s.missiles.begin() + static_cast<std::ptrdiff_t>(k));
            continue;
        }
        ++k;
    }

    // 判负(含同 tick 双亡=平局): 血量归零全场立即冻结
    if (s.hp[0] <= 0 || s.hp[1] <= 0) {
        s.phase = BattlePhase::Over;
        s.winner = (s.hp[0] <= 0 && s.hp[1] <= 0)
                       ? 2
                       : (s.hp[0] <= 0 ? 1 : 0);
        if (s.hp[0] <= 0) s.died[0] = true;
        if (s.hp[1] <= 0) s.died[1] = true;
    }
}

proto::SnapMsg makeSnap(const BattleState& s, bool paused) {
    proto::SnapMsg out;
    out.phase = paused ? proto::Phase::Paused
                       : static_cast<proto::Phase>(s.phase); // 枚举值 0/1/2 对齐
    for (int i = 0; i < 2; ++i) {
        out.hp[i] = static_cast<std::uint8_t>(s.hp[i]);
        const BattleTank& t = s.tanks[i];
        out.tanks[i] = {t.pos.x, t.pos.y, t.dir, t.turret,
                        static_cast<std::uint8_t>(t.state),
                        static_cast<std::uint8_t>(t.animFrame)};
        out.events.fire[i] = s.fired[i];
        out.events.hit[i] = s.hit[i];
        out.events.die[i] = s.died[i];
    }
    out.missiles.reserve(s.missiles.size());
    for (const BattleMissile& m : s.missiles)
        out.missiles.push_back({m.pos.x, m.pos.y, m.dir, m.owner});
    return out;
}
```

- [ ] **Step 4: 跑测试确认通过**

Run: 同 Step 2。Expected: 全部通过。

- [ ] **Step 5: build.bat 两处编译行追加 src/Battle.cpp + CRLF 校验**

`build.bat:43` 改为：
```
    src/main.cpp src/Assets.cpp src/Game.cpp src/Battle.cpp app_icon.res.o ^
```
`build.bat:52` 改为：
```
g++ -B%U%\bin\ -std=c++20 -O1 -Wall -Wextra -Wshadow -Wconversion tests/unit_tests.cpp src/Battle.cpp ^
```
改完立刻恢复/校验 CRLF（本机铁律）：

Run: `cd /d/Berton/Tank-Battle/tank-battle-cpp && "F:/program files/python313/python.exe" -c "d=open('build.bat','rb').read(); import sys; sys.exit(0 if b'\r\n' in d and d.count(b'\n')==d.count(b'\r\n') else 1)" && echo CRLF-OK`
Expected: `CRLF-OK`。失败则 `"F:/program files/python313/python.exe" -c "d=open('build.bat','rb').read().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'); open('build.bat','wb').write(d)"` 后复验。

- [ ] **Step 6: 完整构建**

Run: `cmd.exe //c build.bat`。Expected: `[4/4] done.`（含新单测全过）

- [ ] **Step 7: Commit**

```bash
git add src/Battle.hpp src/Battle.cpp tests/unit_tests.cpp build.bat
git commit -m "PvP 纯逻辑模拟 Battle：BattleDefs 数值注入（命中盒存 100% 原值×乘数）、导弹只判对方、爆炸冻结回出生点无敌、双亡平局、计时器禁墙钟，附单测；build.bat 两处编译行追加"
```

---

### Task 3: NetSession.hpp/cpp（socket 层：发现/进房/收发/过滤/心跳）

**Files:**
- Create: `tank-battle-cpp/src/NetSession.hpp`
- Create: `tank-battle-cpp/src/NetSession.cpp`
- Modify: `tank-battle-cpp/build.bat:43`（游戏编译行再追加 `src/NetSession.cpp`，单测行**不加**——socket 层不进单测）

**Interfaces:**
- Consumes: Protocol 全部、InputState。
- Produces（Game 集成依赖的精确 API）:

```cpp
struct FoundHost { sf::IpAddress addr; std::string name; };
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
    bool isHost() const;
    bool clientJoined() const;        // 等待页 -> 倒计时的条件
    const InputState& remoteInput() const;
    bool remoteReady() const;         // 双 R 重开
    bool remoteInputStale() const;    // 暂停判定: >1s 无 INP 且 KEEP 活
    void sendSnap(const proto::SnapMsg& s); // 内部 ++outTick(跨局单调)再编码发出

    // 客户端侧
    const std::vector<FoundHost>& foundHosts() const;
    const std::optional<proto::SnapMsg>& snap() const; // 最新已接受快照(>才更新)
    bool peerRecent() const;      // 对端 KEEP/任一包 5 秒内仍在(客户端兜底标签用)
    void sendInput(const InputState& in); // 倒计时/战斗/暂停期 30Hz(空闲也发全零)
    void setReady(bool r);                // R 状态随 JOIN 5Hz 上报

    std::vector<NetEvent> takeEvents();
    sf::IpAddress peerAddress() const;
};
```

- [ ] **Step 1: 写 NetSession.hpp**（接口即上方代码，加 `#include "Battle.hpp"`、`<SFML/Network.hpp>`、`<atomic>`、`<chrono>`、`<optional>`、`<string>`、`<thread>`、`<vector>`）

- [ ] **Step 2: 写 NetSession.cpp**（核心实现，关键段落如下——执行者照此完成并在模糊处以 spec §6 为准）

状态机与成员：

```cpp
#include "NetSession.hpp"
#include <algorithm>
#include <iostream>

namespace {
using Clock = std::chrono::steady_clock;
inline float secsSince(Clock::time_point t) {
    return std::chrono::duration<float>(Clock::now() - t).count();
}
} // namespace

// 成员(NetSession.hpp private):
//   角色枚举 Role{None,HostWaiting,HostSession,Scan,ScanDone,Joining,ClientSession}
//   sf::UdpSocket sock_;      // 主收发(非阻塞; 客户端首次 send 自动绑随机端口)
//   sf::UdpSocket keepSock_;  // 心跳专用(仅后台线程 send; SFML socket 非线程安全)
//   std::jthread keepThread_; // 1Hz KEEP -> 对端主端口(语音 voicePingThread 同款)
//   sf::IpAddress peerIp_; unsigned short peerPort_ = 0; // 对端主端口
//   std::string myName_, peerName_; bool peerReady_ = false;
//   InputState remoteInput_; Clock::time_point lastAny_, lastInp_, scanStart_;
//   std::uint32_t outTick_ = 0, lastSnapTick_ = 0;
//   std::optional<proto::SnapMsg> snap_;
//   std::vector<FoundHost> hosts_;
//   float joinTimer_ = 0.f, scanTimer_ = 0.f; // 5Hz 重发计时
//   bool connected_ = false, disconnectedFired_ = false;
//   std::vector<NetEvent> events_;
```

poll() 主干（收包循环 + 过滤，buf 1024）：

```cpp
void NetSession::poll(float dt) {
    // --- 周期发送 ---
    if (role_ == Role::Scan || role_ == Role::Joining) {
        scanTimer_ -= dt;
        if (role_ == Role::Scan && scanTimer_ <= 0.f) { // DISC 5Hz 广播
            const auto d = proto::encodeDisc();
            sock_.send(d.data(), d.size(), sf::IpAddress::Broadcast, Port);
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
        std::size_t got = 0; std::optional<sf::IpAddress> from; unsigned short fport = 0;
        if (sock_.receive(buf_, sizeof(buf_), got, from, fport) != sf::Socket::Status::Done)
            break;
        handlePacket(buf_, got, from.value_or(sf::IpAddress::Any), fport);
    }
    // --- 掉线检测(5s 无任何包, 含 KEEP) ---
    if (connected_ && !disconnectedFired_ && secsSince(lastAny_) > 5.f) {
        disconnectedFired_ = true;
        events_.push_back({NetEvent::Kind::Disconnected});
    }
}
```

handlePacket 的过滤与分发（spec §6.3/§6.4 的全部规则）：

```cpp
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
            sock_.send(h.data(), h.size(), ip, port);
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
        if (role_ == Role::ClientSession && ver == proto::kVersion) {
            if (auto m = proto::decodeSnap(p, n)) {
                if (proto::acceptVerdict(m->tick, lastSnapTick_) ==
                    proto::Accept::Apply) {
                    lastSnapTick_ = m->tick; snap_ = std::move(m);
                    if (!connected_) { // 第一个 SNAP = 连接成功(spec §4)
                        connected_ = true; role_ = Role::ClientSession;
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
```

其余成员（执行者实现，行为规格）：`startHost`：`sock_.setBlocking(false); bind(Port)` 失败→false；`startScan/startJoin`：客户端 socket 非阻塞、不预绑定（首次 send 自动分配）；`startKeepalive`：`keepThread_ = std::jthread([this](std::stop_token st){ 每 1s 用 keepSock_ 发 encodeKeep() 到对端主端口, 语音心跳同款 10×100ms 睡眠循环 })`；`sendSnap(const proto::SnapMsg& s)`：**先拷贝再填 tick**——`proto::SnapMsg m = s; m.tick = ++outTick_; auto b = proto::encodeSnap(m); sock_.send(b.data(), b.size(), peerIp_, peerPort_)`（outTick_ 永不重置——C1）；`sendInput`：编码 INP 发往对端主端口；`setReady/remoteReady`：ready 随 JOIN 夹带（poll 的 5Hz 分支）；`remoteInputStale`：`connected_ && secsSince(lastInp_) > 1.f && secsSince(lastAny_) <= 5.f`（**阶段闸在调用方**：Game 仅 Battle 相位询问，倒计时/结算不算停滞）；`peerRecent`：`connected_ && secsSince(lastAny_) <= 5.f`；`close(sendBye)`：尽力发 BYE→request_stop+join 线程→unbind。

- [ ] **Step 3: build.bat 游戏编译行追加 + CRLF 校验**

`build.bat:43` 再追加 `src/NetSession.cpp`：
```
    src/main.cpp src/Assets.cpp src/Game.cpp src/Battle.cpp src/NetSession.cpp app_icon.res.o ^
```
CRLF 校验同 Task 2 Step 5。

- [ ] **Step 4: 编译验证（0 警告）+ 完整构建**

Run: `cmd.exe //c build.bat`。Expected: `[4/4] done.`（Game 尚未使用 NetSession，先确认独立编译干净；Game.cpp 未动，单人不受影响。）

- [ ] **Step 5: Commit**

```bash
git add src/NetSession.hpp src/NetSession.cpp build.bat
git commit -m "NetSession 会话层：UDP 52021 发现/进房/快照收发，幂等现状广播无 ack；源过滤（非 KEEP 认对端 IP+主端口，KEEP 认 IP 任意端口）；1Hz 心跳线程防拖窗误杀；忙碌拒绝；tick 跨局单调"
```

---

### Task 4: net_host_stub 控制台驱动 + net_smoke.py 端到端冒烟

**Files:**
- Create: `tank-battle-cpp/tests/net_host_stub.cpp`（仅冒烟用，不进 build.bat）
- Create: `tank-battle-cpp/tools/net_smoke.py`

**Interfaces:**
- Consumes: NetSession/Battle 全部 API；net_smoke.py 用 Python 原生 socket 独立实现协议（双实现交叉验证）。

- [ ] **Step 1: 写 net_host_stub.cpp**

```cpp
// 无窗口主机驱动: 供 tools/net_smoke.py 端到端验证联机协议闭环。
// 编译(项目根目录):
//   g++ -B<ucrt64>/bin/ -std=c++20 -O1 -Wall -Wextra -Wshadow -Wconversion ^
//       tests/net_host_stub.cpp src/Battle.cpp src/NetSession.cpp ^
//       -o net_host_stub.exe -lsfml-network -lsfml-system
#include "../src/Battle.hpp"
#include "../src/NetSession.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

int main() {
    NetSession net;
    if (!net.startHost("stub-room")) {
        std::printf("[stub] bind 52021 failed\n");
        return 1;
    }
    BattleDefs d;                 // 贴近真实值的手写配置(无素材)
    d.tankHalfExtents = {20.f, 12.f};
    d.missileHalfExtents = {7.f, 3.f};
    BattleState s;
    resetBattle(s, d);
    const auto tick = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 3600; ++frame) { // 上限 120s
        net.poll(1.f / 30.f);
        if (net.clientJoined()) {
            InputState local{};                  // 主机坦克: 原地朝上待打
            InputState in[2] = {local, net.remoteInput()};
            if (!net.remoteInputStale())
                stepBattle(s, d, in, 1.f / 30.f);
            net.sendSnap(makeSnap(s, net.remoteInputStale()));
            if (s.phase == BattlePhase::Over) {
                std::printf("[stub] battle over winner=%d\n", s.winner);
                return 0;
            }
        }
        if (frame % 30 == 0)
            std::printf("[stub] t=%.0fs joined=%d\n",
                        static_cast<double>(frame) / 30.0, net.clientJoined() ? 1 : 0);
        std::this_thread::sleep_until(tick +
            std::chrono::milliseconds(33 * (frame + 1)));
    }
    return 0;
}
```

编译并启动（后台）：

Run: `cd /d/Berton/Tank-Battle/tank-battle-cpp && "/d/Scoop/apps/msys2/current/ucrt64/bin/g++.exe" -B"D:/Scoop/apps/msys2/current/ucrt64/bin/" -std=c++20 -O1 -Wall -Wextra -Wshadow -Wconversion tests/net_host_stub.cpp src/Battle.cpp src/NetSession.cpp -o net_host_stub.exe -lsfml-network -lsfml-system && ./net_host_stub.exe &`
Expected: 输出 `[stub] t=0s joined=0`（等待客户端）。

- [ ] **Step 2: 写 net_smoke.py**（独立协议实现；`python net_smoke.py 127.0.0.1`）

```python
# -*- coding: utf-8 -*-
# 联机端到端冒烟: 以独立 Python 协议实现扮演客户端, 对 net_host_stub 驱动的
# 主机会话走完 发现 -> JOIN -> SNAP -> INP 移动 -> 命中扣血 -> busy -> 暂停。
# 用法: python tools/net_smoke.py [host_ip]  (默认 127.0.0.1)
import socket, struct, sys, time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = 52021
VER = 1

def disc():    return bytes([1, VER])
def join(name, ready=False):
    return bytes([4, VER, len(name)]) + name.encode() + bytes([1 if ready else 0])
def inp(bits, aim_deg, fire):
    a = int(round((aim_deg % 360.0) * 16)) % 5760
    return bytes([5, VER, bits]) + struct.pack("<H", a) + bytes([1 if fire else 0])
def keep():    return bytes([8, VER])

def parse_snap(b):
    assert b[0] == 6 and len(b) >= 31, "bad snap"  # 31B = 空导弹最小包
    tick, = struct.unpack_from("<I", b, 2)
    phase, hp0, hp1 = b[6], b[7], b[8]
    off = 9
    tanks = []
    for _ in range(2):
        x, y, d, t = struct.unpack_from("<hhHH", b, off)
        state, frame = b[off+8], b[off+9]
        tanks.append(dict(x=x/16.0, y=y/16.0, state=state, frame=frame))
        off += 10
    n = b[off]; off += 1
    missiles = []
    for _ in range(n):
        x, y, d = struct.unpack_from("<hhH", b, off)
        missiles.append(dict(x=x/16.0, y=y/16.0, owner=b[off+6]))
        off += 7
    return dict(tick=tick, phase=phase, hp=[hp0, hp1], tanks=tanks,
                missiles=missiles)

def expect(cond, msg):
    if not cond:
        print("FAIL:", msg); sys.exit(1)
    print("PASS:", msg)

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(0.5)
s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

# 1) 发现: 广播 DISC(本机回环也能收到 stub 的单播回应)
s.sendto(disc(), ("255.255.255.255", PORT))
s.sendto(disc(), (HOST, PORT))
name = None
for _ in range(10):
    try:
        b, a = s.recvfrom(2048)
        if b[0] == 2:
            name = b[3:3+b[2]].decode()
            expect(a[0] == HOST, "discovered host %s room=%r" % (a[0], name))
            break
    except socket.timeout:
        s.sendto(disc(), (HOST, PORT))
expect(name is not None, "host responded to DISC")

# 2) JOIN 5Hz 直到第一个 SNAP(连接成功标志)
snap = None
t0 = time.time()
while time.time() - t0 < 5.0:
    s.sendto(join("smoke-client"), (HOST, PORT))
    try:
        b, _ = s.recvfrom(2048)
        if b[0] == 6:
            snap = parse_snap(b); break
    except socket.timeout:
        pass
expect(snap is not None, "first SNAP arrived (joined)")
expect(snap["phase"] == 0, "phase=countdown")
last_tick = snap["tick"]

def next_snap(timeout=3.0, keepalive=True):
    global last_tick
    t0 = time.time()
    while time.time() - t0 < timeout:
        if keepalive:
            s.sendto(keep(), (HOST, PORT))  # 心跳走"另一端口"模拟独立 socket
        try:
            b, _ = s.recvfrom(2048)
            if b[0] == 6:
                sn = parse_snap(b)
                if sn["tick"] > last_tick:
                    last_tick = sn["tick"]
                    return sn
        except socket.timeout:
            pass
    return None

# 3) 倒计时 3s 后进战斗
sn = None
for _ in range(120):
    s.sendto(inp(0, 0.0, False), (HOST, PORT))
    sn = next_snap(0.5)
    if sn and sn["phase"] == 1:
        break
expect(sn and sn["phase"] == 1, "countdown -> battle (phase self-heal)")

# 4) 移动: 按住 W 1 秒, y 必须上升(位移同步生效)
y0 = sn["tanks"][1]["y"]
for _ in range(30):
    s.sendto(inp(1, 0.0, False), (HOST, PORT))
    sn = next_snap(0.5)
y1 = sn["tanks"][1]["y"]
expect(y1 > y0 + 5.0, "own tank moved y %.1f -> %.1f" % (y0, y1))

# 5) 命中扣血: 客户端坦克朝下瞄准主机(它在 (0,-120) 原地), 炮塔 180 度连发
for _ in range(90):
    s.sendto(inp(0, 180.0, True), (HOST, PORT))
    sn = next_snap(0.5)
    if sn["hp"][0] < 3:
        break
expect(sn["hp"][0] < 3, "host tank hit, hp0=%d" % sn["hp"][0])

# 6) busy: 第二个 socket 在战斗中 JOIN, 必须收到 ERR{reason=2}
s2 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s2.settimeout(2.0)
s2.sendto(join("intruder"), (HOST, PORT))
b, _ = s2.recvfrom(2048)
expect(b[0] == 3 and b[2] == 2, "busy ERR for third-party JOIN")

# 6.5) 源过滤: 陌生端口(s2)伪造 SNAP/BYE 直发主机, 必须被无反响地丢弃
#      (非对端 IP 维度本机回环测不了, 显式移交 Task 8 双机清单)
for fake in (bytes([6, VER]) + b"\xff" * 40, bytes([7, VER])):
    s2.sendto(fake, (HOST, PORT))
sn = None
for _ in range(30):
    s.sendto(inp(0, 0.0, False), (HOST, PORT))
    sn = next_snap(0.5)
    if sn and sn["phase"] == 1:
        break
expect(sn and sn["phase"] == 1, "forged SNAP/BYE ignored (source filter)")

# 7) 暂停(N1): 停发 INP 1.5s(KEEP 照发), 必须收到 phase=3 且 tick 前进
t0 = time.time()
paused = None
while time.time() - t0 < 3.0:
    paused = next_snap(0.5)   # 内部只发 keep 不发 inp
    if paused and paused["phase"] == 3:
        break
expect(paused and paused["phase"] == 3, "pause visible (phase=3, tick=%d)" %
       (paused["tick"] if paused else -1))

# 8) 恢复: 继续发 INP, phase 回 1
for _ in range(30):
    s.sendto(inp(0, 0.0, False), (HOST, PORT))
    sn = next_snap(0.5)
    if sn and sn["phase"] == 1:
        break
expect(sn and sn["phase"] == 1, "resumed after INP resumed")

print("ALL SMOKE CHECKS PASSED")
```

- [ ] **Step 3: 跑通冒烟**

Run: `"F:/program files/python313/python.exe" tools/net_smoke.py 127.0.0.1`（stub 在另一终端跑着）
Expected: 9 行 `PASS:`（含 6.5 源过滤）+ `ALL SMOKE CHECKS PASSED`。
**跑完立刻杀掉 stub**（它占着 52021，残留会让 Task 5 起的所有建主局/双开手测绑不上端口）：
Run: `taskkill //IM net_host_stub.exe //F 2>/dev/null; true`

- [ ] **Step 4: 单人冒烟确认未受影响 + Commit**

Run: `powershell.exe -ExecutionPolicy Bypass -File tools/run_and_shoot.ps1 -Phase playing`
Expected: 单人照常（本任务没动 Game，跑一次是纪律）。

```bash
git add tests/net_host_stub.cpp tools/net_smoke.py
git commit -m "联机端到端冒烟：无窗口主机驱动 + 独立 Python 协议实现（发现/加入/移动/命中扣血/busy 拒绝/暂停可见性/恢复 全链路 PASS）"
```

---

### Task 5: Game 集成 A——模式入口/大厅/倒计时/会话接线

**Files:**
- Modify: `tank-battle-cpp/src/Game.hpp`
- Modify: `tank-battle-cpp/src/Game.cpp`
- Test: 单人冒烟 + 手动双开验证大厅流程（渲染部分在 Task 6 完成后全验）

**Interfaces:**
- Consumes: NetSession/Battle/Protocol 全 API。
- Produces（Task 6 依赖）：`Game` 新增 `enum class Mode{Solo,NetHost,NetClient}`、`Mode mode()`；内部 net 状态机与 `buildLocalInput()`、`netUpdateHost(dt)`、`netUpdateClient(dt)`、`BattleState battle_` / `proto::SnapMsg view_`。

- [ ] **Step 1: Game.hpp 增加成员与声明**（不动单人成员）

在 `#include "Assets.hpp"` 后加：

```cpp
#include "Battle.hpp"
#include <memory>
class NetSession;
```

class Game 内新增（public）：

```cpp
    enum class Mode { Solo, NetHost, NetClient };
    Mode mode() const { return mode_; }
    void requestDirectJoin(const std::string& hostIp); // main.cpp --join 用
```

private 新增：

```cpp
    // ---- 联机(单人路径不动; 全部以 mode_ != Solo 为闸) ----
    struct NetView {                    // 客户端侧渲染视图(来自最新快照)
        proto::SnapMsg snap;
        bool has = false;
        float countdownLocal = 0.f;     // phase->0 启动的本地 3s 倒计时
        bool wasCountdown = false;
        float noSnapSince = 0.f;        // >1s 且未掉线 -> 本地"暂停中"兜底标签
    };
    void netStartHost();
    void netStartScan();
    void netStartJoin(sf::IpAddress host);
    void netLeave();                    // Esc: BYE+清理+回 Title
    InputState buildLocalInput();       // 键盘+语音 -> InputState(归一化点)
    void netUpdateHost(float dt);
    void netUpdateClient(float dt);
    void netHandleEvents();
    Mode mode_ = Mode::Solo;
    std::unique_ptr<NetSession> net_;
    BattleDefs netDefs_;
    BattleState battle_;                // 主机模拟用
    NetView view_;                      // 客户端渲染用
    bool netLocalReady_ = false;        // 本机 R(结算期)
    bool netWasOver_ = false;           // 客户端: 用于 Over->新局跳变时清 ready
    bool joiningStarted_ = false;       // 客户端: 扫描选定主机后置位
    float netJoinWait_ = 0.f;           // 客户端: JOIN 后等首个 SNAP 的计时(5s)
    float netBlinkTimer_ = 0.f;         // 无敌闪烁时钟(0.1s 翻转)
    static std::string machineName();   // COMPUTERNAME 兜底 "host"/"client"
    // 从单人 updatePlayer 抽出的炮塔跟随段(仅 Game.cpp:161-183 的函数体,
    // **守卫 if(phase!=Title&&...) 留在单人调用点不进函数**——联机会话期间
    // Game::phase 恒为 Title, 守卫进了函数体联机炮塔就永远不更新):
    // 鼠标跟随的 pointDirection 锚点改用参数(单人传 player.pos; 主机传
    // battle_.tanks[0].pos; 客户端传 view_.snap.tanks[1].pos——单人结构
    // player.pos 在联机中恒 {0,0}, 沿用会系统性偏瞄)
    void updateTurretLocal(float dt, sf::Vector2f anchorPos);
```

- [ ] **Step 2: Game.cpp 实现接线**（`#include "NetSession.hpp"` 置顶；析构 `Game::~Game(){ if (net_) net_->close(true); }` —— Game 原来无析构，声明加上）

关键实现（执行者按此写，单人 case 一律不碰）：

```cpp
// 标题画面新增入口(在 handleEvent 的 KeyPressed 分支、现有 R 逻辑后):
//   H -> mode_==Solo && phase==Title 时 netStartHost()
//   J -> mode_==Solo && phase==Title 时 netStartScan()
// Esc: mode_ != Solo 时 netLeave() 而不是 quit=true
std::string Game::machineName() {
    const char* cn = std::getenv("COMPUTERNAME");
    return cn ? cn : "host";
}
void Game::netStartHost() {
    auto n = std::make_unique<NetSession>();
    if (!n->startHost(machineName())) {
        std::cout << "[net] 52021 被占用(已有机局?), 回标题\n";
        return; // 端口占用提示(渲染文字在 Task 6)
    }
    net_ = std::move(n); mode_ = Mode::NetHost; phase = Phase::Title; // 复用 Title 渲染底
}
void Game::netStartScan() {
    net_ = std::make_unique<NetSession>();
    net_->startScan(machineName());
    mode_ = Mode::NetClient;
}
void Game::netStartJoin(sf::IpAddress host) {
    net_->startJoin(host, machineName());
}
void Game::netLeave() {
    if (net_) net_->close(true);
    net_.reset(); mode_ = Mode::Solo; phase = Phase::Title;
    netLocalReady_ = false; view_ = NetView{};
}

InputState Game::buildLocalInput() {
    // 键盘与语音口令在这里归一化(联机里语音与键盘同向不叠加——单人玩具行为不复现)
    InputState in;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W)) in.moveBits |= 0x01;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S)) in.moveBits |= 0x02;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A)) in.moveBits |= 0x04;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D)) in.moveBits |= 0x08;
    if (voiceMoveDir == 0.f) in.moveBits |= 0x01;         // 语音移动口令映射
    else if (voiceMoveDir == 180.f) in.moveBits |= 0x02;
    else if (voiceMoveDir == -90.f) in.moveBits |= 0x04;
    else if (voiceMoveDir == 90.f) in.moveBits |= 0x08;
    in.aim = player.turretDir;   // 本地炮塔语义(鼠标/←→/语音)已在单人代码算好
    in.fire = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left) ||
              sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Space) ||
              voiceFireWait > 0.f;
    return in;
}

void Game::netUpdateHost(float dt) {
    net_->poll(dt);
    netHandleEvents();
    if (!net_->clientJoined()) return;
    if (battle_.phase == BattlePhase::Countdown && battle_.countdown >= 2.99f) {
        // 首帧注入 BattleDefs(速度/围栏=单人常量; 命中盒=Assets 原始值+乘数,
        // boxHitTest 内部乘 size —— 千万别预乘, 否则盒放大 3.3 倍)
        netDefs_.playerSpeed = PlayerSpeed;
        netDefs_.missileSpeed = MissileSpeed;
        netDefs_.fireCooldown = FireCooldown;
        netDefs_.minX = PlayerMinX; netDefs_.maxX = PlayerMaxX;
        netDefs_.minY = PlayerMinY; netDefs_.maxY = PlayerMaxY;
        netDefs_.tankHalfExtents = a.playerBody.halfExtents;
        netDefs_.tankCenterOffset = a.playerBody.centerOffset;
        netDefs_.tankSize = 0.30f;
        netDefs_.missileHalfExtents = a.missile.halfExtents;
        netDefs_.missileCenterOffset = a.missile.centerOffset;
        netDefs_.missileSize = 0.50f;
        resetBattle(battle_, netDefs_);
    }
    // 本机炮塔沿用单人跟随/手转/语音逻辑 -> 作为 aim 上报; 锚点=主机坦克位置
    // (守卫已在单人调用点外, 这里无条件调); 主机自己的移动不走单人路径,
    // 全部经 Battle(单一真相源)
    updateTurretLocal(dt, battle_.tanks[0].pos);
    InputState in[2] = {buildLocalInput(), net_->remoteInput()};
    // 暂停判定仅战斗阶段适用(spec §6.3); 倒计时/结算期客户端不发 INP 不算停滞
    const bool paused = battle_.phase == BattlePhase::Battle &&
                        net_->remoteInputStale();
    if (!paused) stepBattle(battle_, netDefs_, in, dt);
    for (int i = 0; i < 2; ++i) {          // 本机音效(事件)
        if (battle_.fired[i] && a.sndFirePlayer) playSound(*a.sndFirePlayer);
        if (battle_.hit[i] && a.sndExplosion) playSound(*a.sndExplosion);
    }
    net_->sendSnap(makeSnap(battle_, paused));
    if (battle_.phase == BattlePhase::Over) {
        // 双 R 重开: 本机 R 置 netLocalReady_, 远端看 remoteReady()
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::R)) netLocalReady_ = true;
        if (netLocalReady_ && net_->remoteReady()) {
            netLocalReady_ = false;
            resetBattle(battle_, netDefs_);   // tick 在 NetSession, 永不重置(C1)
        }
    }
}

void Game::netUpdateClient(float dt) {
    net_->poll(dt);
    netHandleEvents();
    // 扫描期: 2s 后有结果(0 个->提示重试; >=1 个->startJoin 第一个)
    // (多个主机列表选择 UI 在 Task 6 Step 2.5; 先自动连第一个)
    if (!view_.has && net_ && !net_->foundHosts().empty() && !joiningStarted_) {
        netStartJoin(net_->foundHosts().front().addr);
        joiningStarted_ = true;
        netJoinWait_ = 0.f;
    }
    // 连接超时(spec §4/§7): JOIN 发出 5 秒无任何 SNAP(--join 写错 IP/对端
    // 死亡) -> 退回扫描, 不停在无反馈画面
    if (!view_.has && joiningStarted_) {
        netJoinWait_ += dt;
        if (netJoinWait_ > 5.f) {
            netJoinWait_ = 0.f;
            joiningStarted_ = false;
            net_->startScan(machineName());
        }
    }
    // 本机炮塔跟随(锚点=客户端坦克快照位置); INP 倒计时起即 30Hz 发
    // (结算期不发, 改由 NetSession 5Hz 发 JOIN 带 ready 位)
    updateTurretLocal(dt, view_.has
                                ? sf::Vector2f(view_.snap.tanks[1].x,
                                               view_.snap.tanks[1].y)
                                : sf::Vector2f(0.f, 120.f));
    const bool inGame = view_.has &&
        (view_.snap.phase == proto::Phase::Countdown ||
         view_.snap.phase == proto::Phase::Battle ||
         view_.snap.phase == proto::Phase::Paused);
    if (inGame) net_->sendInput(buildLocalInput()); // 空闲也发全零, 暂停判定依赖
    if (net_->snap()) {
        const proto::SnapMsg& sn = *net_->snap();
        const bool nowCd = sn.phase == proto::Phase::Countdown;
        if (nowCd && !view_.wasCountdown) view_.countdownLocal = 3.f; // 本地倒计时(N3)
        view_.wasCountdown = nowCd;
        view_.snap = sn; view_.has = true;
        view_.noSnapSince = 0.f;
        for (int i = 0; i < 2; ++i) {        // 事件音效(NetSession 已保证仅新 tick)
            if (sn.events.fire[i] && a.sndFirePlayer) playSound(*a.sndFirePlayer);
            if (sn.events.hit[i] && a.sndExplosion) playSound(*a.sndExplosion);
        }
        // Over->新局跳变: 清本机 ready(I-C, 否则第二局主机单边 R 即重开)
        if (sn.phase != proto::Phase::Over && netWasOver_) netLocalReady_ = false;
        netWasOver_ = sn.phase == proto::Phase::Over;
    } else {
        view_.noSnapSince += dt; // >1s 且对端 KEEP 仍活 -> 兜底暂停标签(N7/M-4,
                                 //  真掉线 1-5s 间隙不误显, 见 Task 6 渲染条件)
    }
    if (view_.has && view_.snap.phase == proto::Phase::Countdown)
        view_.countdownLocal -= dt;
    if (view_.has && view_.snap.phase == proto::Phase::Over) {
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::R)) netLocalReady_ = true;
        net_->setReady(netLocalReady_);      // 随 JOIN 5Hz 上报, 主机收双方 R 重开
    }
}

void Game::netHandleEvents() {
    for (const NetEvent& ev : net_->takeEvents()) {
        if (ev.kind == NetEvent::Kind::Disconnected) {
            std::cout << "[net] 对方已离开, 回标题\n";
            netLeave();
        } else if (ev.kind == NetEvent::Kind::Busy) {
            // spec §4: busy 退回扫描页(不是回标题)
            std::cout << "[net] 对局进行中, 重新扫描\n";
            joiningStarted_ = false; netJoinWait_ = 0.f;
            view_ = NetView{};
            net_->startScan(machineName());
        } else if (ev.kind == NetEvent::Kind::VersionMismatch) {
            std::cout << "[net] 版本不一致, 请两台机器使用同一份构建\n";
            netLeave();
        }
    }
}
```

`update(float dt)` 开头加：

```cpp
    if (mode_ == Mode::NetHost) { netUpdateHost(dt); netBlinkTimer_ += dt; return; }
    if (mode_ == Mode::NetClient) { netUpdateClient(dt); netBlinkTimer_ += dt; return; }
```

注意 `pollVoice(dt)` 要在这两个分支**之前**调用（语音是本地输入源，两个模式都要轮询）——把原 `pollVoice(dt)` 挪到 switch 前并保持在函数第一行。`joiningStarted_` 补一个 bool 成员。requestDirectJoin：`net_ = make_unique<NetSession>(); mode_=NetClient; net_->startJoin(sf::IpAddress(ip), name)`。

- [ ] **Step 3: 编译 + 完整构建 + 单人冒烟**

Run: `cmd.exe //c build.bat && powershell.exe -ExecutionPolicy Bypass -File tools/run_and_shoot.ps1 -Phase playing`
Expected: 四步绿 + 单人截图与改前一致（标题画面多一行联机提示属预期，Task 6 才加字，此时还无渲染改动）。

- [ ] **Step 4: 手动双开验证大厅流程**（无渲染也能验：控制台输出）

终端 A：`./tank-battle.exe`（按 H 前先不开，先确认单人正常）。终端 B：`cmd.exe //c "tank-battle.exe --join 127.0.0.1"`（Task 7 才接 main 参数，此处可跳过）。最低限度：A 开 H 后 `net_host_stub` 冒烟仍过（会话层未被 Game 改动）。
Expected: A 控制台出现 `[voice]` 两行照旧，无 `[net]` 报错。

- [ ] **Step 5: Commit**

```bash
git add src/Game.hpp src/Game.cpp
git commit -m "Game 集成联机 A：模式入口 H/J/Esc 离场、BattleDefs 开局注入（Assets 原值+乘数）、主机模拟+快照、客户端输入上报与本地倒计时、双 R 重开；单人路径零改动"
```

---

### Task 6: Game 集成 B——渲染/HUD/结算/暂停标签 + 单人回归

**Files:**
- Modify: `tank-battle-cpp/src/Game.cpp`（render 与标题提示）

**Interfaces:**
- Consumes: Task 5 的 `mode_/battle_/view_/netBlinkTimer_`。
- Produces: 完整可见的联机对战（后续仅剩参数/字体/文档）。

- [ ] **Step 1: 标题画面加联机提示行**（render 的 Title 分支，`screenStart` 绘制后）：

```cpp
        if (a.font) { // 联机入口提示(README 已知差异: 标题新增一行文字)
            sf::Text hint = makeText(utf8("H 创建联机对战    J 加入对战"),
                                      20, sf::Color(0x44, 0x3c, 0x1b));
            sf::FloatRect hb = hint.getLocalBounds();
            hint.setOrigin({hb.size.x / 2.f, hb.size.y / 2.f});
            hint.setPosition(stage::toWindow({0.f, -140.f}));
            target.draw(hint);
        }
```

- [ ] **Step 2: 联机战斗渲染**（render() 的 `target.clear();` 之后、背景绘制**之前**插入 `if (mode_ != Mode::Solo) { renderNet(target); return; }`——renderNet 自己画背景，插在背景之后会画两次；新增私有 `void renderNet(sf::RenderWindow&) const`。注意**不要**插进 `if (phase == Title)…else` 结构里：联机会话期间 `Game::phase` 恒为 Title（这是复用标题渲染底的既有约定），插在 else 前后都会让联机画面走单人标题分支）：

```cpp
// 联机画面: 主机画 battle_, 客户端画 view_.snap; 己方炮塔本地覆盖(跟手)
void Game::renderNet(sf::RenderWindow& target) const {
    target.clear();
    target.draw(sf::Sprite(a.background));
    const bool host = mode_ == Mode::NetHost;
    const bool inBattle = host ? battle_.phase != BattlePhase::Countdown
                               : view_.has && view_.snap.phase != proto::Phase::Countdown;
    if (!inBattle) { // 大厅/等待页(房间名等待 / 扫描提示 / 倒计时大字)
        if (a.font) {
            sf::Text t = makeText(utf8(host ? "等待对手加入..."
                                 : (view_.has ? "" : "扫描中... 按 J 重试或用 --join IP")),
                                  24, sf::Color(0x44, 0x3c, 0x1b));
            // ...居中绘制; 倒计时阶段画 ceil(countdown) 大字(主机用 battle_.countdown,
            // 客户端用 view_.countdownLocal, phase->1 自愈)
        }
    } else {
        // 双坦克: 客户端坦克(索引1)整体蓝色 tint 区分
        const auto tankOf = [&](int i) -> proto::TankSnap {
            if (host) {
                const BattleTank& t = battle_.tanks[i];
                return {t.pos.x, t.pos.y, t.dir, t.turret,
                        static_cast<std::uint8_t>(t.state),
                        static_cast<std::uint8_t>(t.animFrame)};
            }
            return view_.snap.tanks[i];
        };
        for (int i = 0; i < 2; ++i) {
            const proto::TankSnap t = tankOf(i);
            const bool blink = t.state == 2 &&            // 无敌闪烁(0.1s 翻转)
                (static_cast<int>(netBlinkTimer_ * 10.f) % 2 == 0);
            if (t.state == 1) {                           // 爆炸帧(数组下标已由
                target.draw(makeSprite(a.playerExplosion[t.animFrame], // 协议校验<=5)
                                       0.60f, {t.x, t.y}, t.dir));
            } else if (!blink) {
                sf::Sprite body = makeSprite(a.playerBody, 0.30f, {t.x, t.y}, t.dir);
                if (i == 1) body.setColor(sf::Color(150, 180, 255));
                target.draw(body);
            }
        }
        // 导弹(客户端导弹同 tint)
        auto drawMissiles = [&](const std::vector<proto::MissileSnap>& ms) {
            for (const proto::MissileSnap& m : ms) {
                sf::Sprite sp = makeSprite(a.missile, 0.50f, {m.x, m.y}, m.dir);
                if (m.owner == 1) sp.setColor(sf::Color(150, 180, 255));
                target.draw(sp);
            }
        };
        if (host) drawMissiles(makeSnap(battle_, false).missiles);
        else drawMissiles(view_.snap.missiles);
        // 炮塔最上层: 己方用本地 player.turretDir(跟手), 对方用快照值
        for (int i = 0; i < 2; ++i) {
            const proto::TankSnap t = tankOf(i);
            const bool own = host ? i == 0 : i == 1;
            const float dir = own ? player.turretDir : t.turret;
            const bool exploding = t.state == 1;
            if (!exploding) {
                sf::Sprite tur = makeSprite(a.playerTurret, 0.30f, {t.x, t.y}, dir);
                if (i == 1) tur.setColor(sf::Color(150, 180, 255));
                target.draw(tur);
            }
        }
        drawNetHud(target); // 血条(下方 Step 3)
        // 结算画面 + 胜者文字 + 双 R 提示; 暂停标签条件:
        //   phase==3(主机发的暂停) 或(phase==1 且 view_.noSnapSince>1 且
        //   net_->peerRecent()——KEEP 仍活才显示, 真掉线 1-5s 间隙不误显"暂停中")
    }
}
```

（执行者补全注释处的居中绘制/结算/暂停文字，文字常量见 Task 7 字体清单；结算判定：主机 `battle_.winner`，客户端 `view_.snap.hp` 推导——hp0/hp1 均归零=平局。）

注意己方炮塔跟手依赖 `player.turretDir` 仍在被更新——**把单人 `updatePlayer`
的炮塔跟随段（Game.cpp:161-183 的函数体）抽成
`updateTurretLocal(float dt, sf::Vector2f anchorPos)`**，两个关键点（C-3）：
① 守卫 `if (phase != Phase::Title && …)`（Game.cpp:160）**留在单人调用点**，
不进函数体——联机会话期间 `Game::phase` 恒为 Title，守卫进了函数体则联机
炮塔永远不更新（鼠标/←→/语音全失效）；② 函数内 `pointDirection` 的锚点
改用参数：单人调用点传 `player.pos`（行为零变化），`netUpdateHost` 传
`battle_.tanks[0].pos`、`netUpdateClient` 传 `view_.snap.tanks[1].pos`
（Task 5 代码已按此调用；单人结构 `player.pos` 在联机中恒 {0,0}，沿用会
系统性偏瞄）。

- [ ] **Step 2.5: 扫描结果列表 UI（多主机时数字键选择，spec §4）**：
  客户端扫描 2 秒（等 `ScanDone` 事件）后仍无连接时，若 `foundHosts()` 非空
  且数量 >1，列 `1. <房间名>` ×N（最多 9 个），按数字键 1-9 调
  `netStartJoin(foundHosts()[k].addr)`；恰 1 个则自动连（Task 5 已实现的
  自动路径）。数字与 `.` 为 ASCII，字体子集已含。

- [ ] **Step 3: 血条 HUD**（`drawNetHud`：两行文字 `玩家1(房主)`/`玩家2` + 各 3 格 `sf::RectangleShape`（12×12，满=深色 `0x44,0x3c,0x1b`、空=浅色 `0xd0,0xc8,0xa8`），画在左上角原"分数"位置附近；不引入分数字样。血量取值：主机读 `battle_.hp[i]`，客户端读 `view_.snap.hp[i]`）

- [ ] **Step 4: 编译 + 完整构建 + 单人冒烟（必须全绿）**

Run: `cmd.exe //c build.bat && powershell.exe -ExecutionPolicy Bypass -File tools/run_and_shoot.ps1 -Phase playing`
Expected: 四步绿；单人画面与 main 基线一致（新增标题提示行除外，属 README 已知差异）。

- [ ] **Step 5: 本机双实例联机全流程手测**

终端 A：`./tank-battle.exe` 按 H；终端 B：`./tank-battle.exe`（Task 7 后可 `--join`）按 J。
核对 spec §8 双机清单的本机可验项：发现→等待页显示已连接→倒计时→开战→双坦克/导弹/tint→命中扣血爆炸→回出生点无敌闪烁→3 血判负→结算文字→双 R 重开不卡死（C1）→单侧 Esc 对方 5s 内回标题→拖 B 窗口 3s 显示暂停、松手恢复。
Expected: 全部符合；不符则按 spec 对应节修。

- [ ] **Step 6: Commit**

```bash
git add src/Game.cpp src/Game.hpp
git commit -m "Game 集成联机 B：双坦克渲染（客户端蓝色 tint/爆炸帧/无敌闪烁）、己方炮塔本地跟手、血条 HUD、结算与平局画面、双 R 重开、暂停标签（phase=3 与客户端兜底）；单人玩法路径零改动（仅标题新增一行联机提示，README 已记）"
```

---

### Task 7: main.cpp --join、字体子集、README、AGENTS.md

**Files:**
- Modify: `tank-battle-cpp/src/main.cpp:34-35`
- Modify: `tank-battle-cpp/tools/subset_font.py`（TEXT 追加新 UI 字）
- Modify: `tank-battle-cpp/assets/fonts/NotoSansSC-Game.otf`（脚本再生成）
- Modify: `tank-battle-cpp/README.md`
- Modify: `AGENTS.md`（架构边界加一行）

**Interfaces:** 无代码接口；产物是文档与资产。

- [ ] **Step 1: main.cpp 参数解析**（`--join` 可出现在任意位置；首个非 `--` 位置参数仍是 assetDir——现有用法不破坏）：

```cpp
    std::string assetDir = "assets", joinIp;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--join" && i + 1 < argc)
            joinIp = argv[++i];
        else if (arg.rfind("--", 0) != 0 && assetDir == "assets")
            assetDir = arg; // 兼容 tank-battle.exe <assets路径> 的既有用法;
                            // 未知 -- 开头参数不吞掉 assetDir
    }
```

`Game game(assets, window);` 之后：

```cpp
    if (!joinIp.empty()) game.requestDirectJoin(joinIp);
```

- [ ] **Step 2: 字体子集扩字**。新 UI 文字常量（Game.cpp 里 utf8 字面量）合集合：

```python
NEWUI = ("创建联机对战等待手加入已连接房间玩家暂停中对方离胜利平局双"
         "未发现进版一致占扫端口描满忙示请两台机器使用同份构重试或启IP")
```

用 python 找出 TEXT 缺字并追加到 `subset_font.py` 的 TEXT 字符串尾部（先算差集再手贴进去）：

Run: `"F:/program files/python313/python.exe" -c "exec(open('tools/subset_font.py',encoding='utf-8').read().split('opts =')[0]); NEWUI='创建联机对战等待手加入已连接房间玩家暂停中对方离胜利平局双未发现进版进一致占扫端口描满忙示请两台机器使用同份构重试或启'; print(''.join(c for c in NEWUI if c not in TEXT))"`
把输出接到 TEXT 末尾，然后重跑子集脚本（源字体 NotoSansSC-Regular.otf 需在项目根，.gitignore 已忽略，首次需按脚本头注释下载）：
Run: `"F:/program files/python313/python.exe" tools/subset_font.py`
Expected: `missing: NONE`。

- [ ] **Step 3: README**：① 新增"联机对战（局域网 1v1）"章节：玩法/操作（同单人+H/J 入口/--join）、UDP 52021 与防火墙"允许专用网络"、断线/暂停行为、双机验收清单（spec §8）；② "与原版的已知差异"追加 spec §9 的 ①-⑤ 全文要点 + 联机模式语音与键盘同向不叠加。

- [ ] **Step 4: AGENTS.md 架构边界加一行**（`src/Game.*` 条目后）：

```markdown
- `src/Battle.*`、`src/NetSession.*`、`src/Protocol.hpp`、`src/InputState.hpp`：局域网 1v1 对战（UDP 52021，主机权威+快照同步）。纯度约束与单人零改动原则见 `docs/superpowers/specs/2026-09-27-lan-multiplayer-design.md`；改协议必须同步 net_smoke.py。
```

- [ ] **Step 5: 构建 + 单人冒烟 + 联机手测一轮 + Commit**

Run: `cmd.exe //c build.bat && powershell.exe -ExecutionPolicy Bypass -File tools/run_and_shoot.ps1 -Phase playing`
Expected: 四步绿 + 冒烟过 + 联机文字全部正常显示（无豆腐块）。

```bash
git add src/main.cpp tools/subset_font.py assets/fonts/NotoSansSC-Game.otf README.md ../AGENTS.md
git commit -m "--join 启动参数（不破坏 assets 位置参数）、字体子集扩联机 UI 字、README 联机章节与已知差异①-⑤、AGENTS.md 架构边界补联机模块"
```

---

### Task 8: 全量验证收尾

**Files:** 无新文件；验证 + 可能的微调。

- [ ] **Step 1: 干净全量构建**

Run: `cmd.exe //c build.bat`
Expected: 四步全绿，0 警告，DLL 闭包 25 个不变（deploy_dlls.py 输出）。

- [ ] **Step 2: 三类回归**：
  1. `powershell.exe -ExecutionPolicy Bypass -File tools/run_and_shoot.ps1 -Phase playing`（单人）
  2. `./net_host_stub.exe &` + `"F:/program files/python313/python.exe" tools/net_smoke.py 127.0.0.1`（协议）
  3. 双实例手动全流程（Task 6 Step 5 清单）
  Expected: 全过。

- [ ] **Step 3: 双机真机验收**（拿另一台电脑，拷整个 tank-battle-cpp 目录）按 README 清单逐项打勾；清单须含**非对端 IP 伪造包被丢弃**（第三台机器向双方各发伪造 SNAP/BYE/JOIN，对局不受影响——本机回环冒烟只覆盖了"同 IP 异端口"维度）；不过项记录并修。

- [ ] **Step 4: 清理 + 终审提交**（显式列举文件，**不用 `git add -A`**——工作区可能有用户的其他改动）

Run: `taskkill //IM net_host_stub.exe //F 2>/dev/null; true`（stub 会占住 52021，残留进程让下次建主局失败）

```bash
git add src tests/unit_tests.cpp tests/net_host_stub.cpp tools/net_smoke.py tools/subset_font.py assets/fonts/NotoSansSC-Game.otf README.md build.bat ../AGENTS.md
git commit -m "联机对战收尾：全量验证通过（build 四步/单人冒烟/协议冒烟/双机清单）"
```

---

## Self-Review 记录

- **Spec 覆盖**：§3 分层=Task 1-3；§4 流程=Task 5/6；§5.1/5.2=Task 2（数值表逐项落 BattleDefs 与 stepBattle）；§6.1-6.4=Task 1（编解码+校验+accept）/Task 3（过滤/KEEP/忙碌/tick 单调）；§7=Task 3/5；§8=Task 1/2 单测+Task 4 冒烟+Task 6/8 回归；§9=Task 7；§11 顺序=任务序。无缺口。
- **占位符**：原稿 Task 1 的 encodeSnap 占位双版本已按计划评审删除，保留干跑验证过的唯一版本；Task 6 Step 2 有两处"执行者补全"（居中绘制/结算文字），给出了判定规则与文字来源（Task 7 字体清单），不算 TBD——若执行中发现文字集合缺失，以 NEWUI 全集兜底。Task 1/2 的全部代码块与测试已在本机干跑（g++ 同参数编译 + 运行，0 警告全绿），这是本轮计划定稿的新门禁。
- **类型一致性**：InputState/ proto::SnapMsg/ TankSnap/ BattleState/ NetSession API 在各任务间已逐一对照（makeSnap 的 BattlePhase→proto::Phase 枚举值 0/1/2 对齐，paused=3 覆盖）。
- **Review Focus → 测试映射**：见各 Focus 行尾注。
