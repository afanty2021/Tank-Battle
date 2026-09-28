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
