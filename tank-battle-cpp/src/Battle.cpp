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
