#include "Game.hpp"

#include <SFML/Window.hpp>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <optional>

Game::Game(Assets& assets, sf::RenderWindow& win) : a(assets), window(win) {
    if (a.sndMusicStart) titleMusic.emplace(*a.sndMusicStart);
    if (a.sndMusicGameOver) gameOverMusic.emplace(*a.sndMusicGameOver);
    // 语音控制: 非阻塞收命令(助手由 main.cpp 拉起, 端口被占则功能禁用)
    voiceSock.setBlocking(false);
    voiceReady = voiceSock.bind(VoicePort) == sf::Socket::Status::Done;
    std::cout << (voiceReady ? "[voice] 语音控制已就绪(UDP " : "[voice] 端口 ")
              << VoicePort << (voiceReady ? ")" : " 被占用, 语音控制禁用") << '\n';
}

// ---------------- 精灵与碰撞 ----------------

sf::Sprite Game::makeSprite(const Costume& c, float sizePercent,
                            sf::Vector2f pos, float dir) {
    sf::Sprite s(c.texture);
    // 贴图按 2x 舞台分辨率导出, 窗口也是 2x, 所以缩放系数正好等于 Scratch 的 size%
    s.setScale({sizePercent, sizePercent});
    s.setOrigin(c.rotationCenter * stage::Scale);
    s.setPosition(stage::toWindow(pos));
    s.setRotation(stage::spriteRotation(dir));
    return s;
}

// 把 point 转入造型的本地坐标系(未旋转姿态=朝右)后做扩展矩形判定,
// 近似 Scratch 按绘制像素的“碰到”。碰撞盒按 centerOffset 放在真实内容处,
// 而非旋转中心处(敌方坦克的旋转中心在炮口, 偏内容中心约 134 舞台单位)
bool Game::hitTest(const Costume& c, float sizePercent, sf::Vector2f center,
                   float dir, sf::Vector2f point, sf::Vector2f pointHalf) {
    return stage::boxHitTest(c.halfExtents, c.centerOffset, sizePercent,
                             center, dir, point, pointHalf);
}

sf::Text Game::makeText(const sf::String& str, unsigned size, sf::Color color) const {
    sf::Text t(*a.font, str, size);
    t.setFillColor(color);
    return t;
}

void Game::playSound(const sf::SoundBuffer& buffer) {
    for (auto& s : voices)
        if (s.getStatus() == sf::SoundSource::Status::Stopped) {
            s.setBuffer(buffer);
            s.play();
            return;
        }
    voices.emplace_back(buffer);
    voices.back().play();
}

// ---------------- 事件 ----------------

void Game::handleEvent(const sf::Event& event) {
    if (event.is<sf::Event::Closed>())
        quit = true;
    else if (const auto* key = event.getIf<sf::Event::KeyPressed>()) {
        if (key->code == sf::Keyboard::Key::Escape)
            quit = true;
        // Scratch 里“停止全部”后项目就停在那; 这里加一个 R 重开的便利功能
        if (key->code == sf::Keyboard::Key::R &&
            (phase == Phase::Stopped || phase == Phase::GameOverMusic)) {
            if (gameOverMusic) gameOverMusic->stop();
            phase = Phase::Title;
        }
    }
}

// ---------------- 每帧逻辑(1/30s) ----------------

void Game::update(float dt) {
    pollVoice(dt);
    switch (phase) {
    case Phase::Title:
        // “等待 按下鼠标” -> “播放 开始游戏 音乐直到播放完毕”
        if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Left)) {
            if (titleMusic) titleMusic->play();
            phase = Phase::TitleMusic;
        }
        break;
    case Phase::TitleMusic:
        if (!titleMusic ||
            titleMusic->getStatus() == sf::SoundSource::Status::Stopped)
            startGame();
        break;
    case Phase::Playing:
    case Phase::PlayerDying:
    case Phase::GameOverMusic:
        // 原版里 导弹精灵的开火循环和炮塔跟随脚本不监听“游戏结束”,
        // 因此玩家爆炸/结算音乐期间仍可开火、弹道继续飞行(按住鼠标连发)
        updatePlayer(dt);
        updateEnemies(dt);
        updateProjectiles(dt);
        // 结束音乐播完 -> “停止全部”
        if (phase == Phase::GameOverMusic &&
            (!gameOverMusic ||
             gameOverMusic->getStatus() == sf::SoundSource::Status::Stopped))
            phase = Phase::Stopped;
        break;
    case Phase::Stopped:
        break;
    }
    voices.erase(std::remove_if(voices.begin(), voices.end(), [](const sf::Sound& s) {
                     return s.getStatus() == sf::SoundSource::Status::Stopped;
                 }),
                 voices.end());
}

void Game::startGame() {
    // 广播“开始游戏”后各角色的初始化
    score = 0;                                    // 舞台: 分数=0
    player = Player{};
    player.pos = {-58.f, -137.f};                 // motion_gotoxy(-58,-137)
    player.dir = 0.f;                             // motion_pointindirection(0)
    enemies.clear();
    missiles.clear();
    bullets.clear();
    fireCooldown = 0.f;
    turretManual = false;
    voiceTurretRemain = 0.f;
    voiceMoveDir = -1.f;
    voiceMoveTimer = 0.f;
    voiceFireWait = 0.f;
    spawnTimer = 0.f;                             // 敌方克隆循环立即先生成一个
    phase = Phase::Playing;
}

void Game::spawnEnemy() {
    // 敌方克隆体: 随机 x∈[-150,150], y=150, 随机造型 1/2, 随机方向 130~240
    std::uniform_real_distribution<float> xz(-150.f, 150.f);
    std::uniform_real_distribution<float> dz(130.f, 240.f);
    enemies.push_back(
        {{xz(rng), 150.f}, dz(rng), (int)(rng() & 1)});
}

void Game::updatePlayer(float dt) {
    // 炮塔脚本: 永远重复(移到车身位置, 面向鼠标)。
    // 被击中/游戏结束后炮塔仅隐藏, 其跟随脚本仍在运行, 导弹仍沿其朝向发射。
    // 鼠标坐标经当前视图映射回 960x720 逻辑系(窗口缩放后依然准确)。
    // 移植版附加(非原版): ←/→ 方向键逆/顺时针旋转炮塔, 接管期间暂停
    // 鼠标跟随; 鼠标位置一变立即恢复原版的“面向鼠标”
    if (phase != Phase::Title && phase != Phase::TitleMusic && phase != Phase::Stopped) {
        const bool left = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left);
        const bool right = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right);
        const sf::Vector2i mouseNow = sf::Mouse::getPosition(window);
        if (left || right) turretManual = true;
        if (mouseNow != lastMousePos) {
            turretManual = false;
            voiceTurretRemain = 0.f; // 鼠标接管时取消未完成的语音旋转
        }
        lastMousePos = mouseNow;
        if (std::abs(voiceTurretRemain) > 0.01f) {
            // 语音口令: 按角度差旋转到目标(期间与方向键一样暂停鼠标跟随)
            turretManual = true;
            const float step = std::min(VoiceTurretSpeed * dt, std::abs(voiceTurretRemain));
            const float s = voiceTurretRemain > 0.f ? step : -step;
            player.turretDir += s;
            voiceTurretRemain -= s;
        } else if (left || right) {
            player.turretDir += (right ? TurretTurnSpeed : -TurretTurnSpeed) * dt;
        } else if (!turretManual) {
            player.turretDir = stage::pointDirection(
                player.pos,
                stage::toStage(window.mapPixelToCoords(mouseNow)));
        }
    }

    // 车身移动循环不监听“被击中/游戏结束”: 玩家被击中后(含整个结算音乐期间)
    // 隐形车身仍可用 WASD 驾驶, 爆炸动画与导弹发射点也随之移动
    const bool controllable = phase == Phase::Playing || phase == Phase::PlayerDying ||
                              phase == Phase::GameOverMusic;
    if (controllable) {
        // WASD 移动: 先转向移动方向, 每帧 5 步, 且不能超出活动范围
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W) && player.pos.y < PlayerMaxY) {
            player.dir = 0.f;
            player.pos.y += PlayerSpeed;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S) && player.pos.y > PlayerMinY) {
            player.dir = 180.f;
            player.pos.y -= PlayerSpeed;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A) && player.pos.x > PlayerMinX) {
            player.dir = -90.f;
            player.pos.x -= PlayerSpeed;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D) && player.pos.x < PlayerMaxX) {
            player.dir = 90.f;
            player.pos.x += PlayerSpeed;
        }
        // 语音移动口令: 持续朝该方向走, 直到“停”或 VoiceMoveMaxTime 自动停
        if (voiceMoveDir >= 0.f) {
            player.dir = voiceMoveDir;
            if (voiceMoveDir == 0.f && player.pos.y < PlayerMaxY)
                player.pos.y += PlayerSpeed;
            else if (voiceMoveDir == 180.f && player.pos.y > PlayerMinY)
                player.pos.y -= PlayerSpeed;
            else if (voiceMoveDir == 90.f && player.pos.x < PlayerMaxX)
                player.pos.x += PlayerSpeed;
            else if (voiceMoveDir == -90.f && player.pos.x > PlayerMinX)
                player.pos.x -= PlayerSpeed;
        }
    }
    if (!player.alive && player.explosionFrame >= 0 && phase == Phase::PlayerDying) {
        // 玩家爆炸: b1..b6 每帧 0.1s, 播完 -> 广播“游戏结束”
        player.explosionTimer -= dt;
        if (player.explosionTimer <= 0.f) {
            ++player.explosionFrame;
            player.explosionTimer = ExplosionFrameTime;
            if (player.explosionFrame >= 6) {
                phase = Phase::GameOverMusic; // 隐藏车身并显示结束画面
                if (gameOverMusic) gameOverMusic->play();
            }
        }
    }

    // 鼠标左键或空格发射导弹(0.5 秒冷却), 从炮塔位置沿炮塔朝向飞出。
    // 导弹精灵的开火循环不监听“被击中/游戏结束”, 死亡与结算期间仍可发射。
    // 空格为移植版新增的发射键(原版仅“按下鼠标”); 语音“开炮”同享冷却
    fireCooldown -= dt;
    if (voiceFireWait > 0.f) voiceFireWait -= dt;
    const bool fireHeld = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left) ||
                          sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Space);
    if (phase != Phase::Title && phase != Phase::TitleMusic && phase != Phase::Stopped &&
        (fireHeld || voiceFireWait > 0.f) && fireCooldown <= 0.f) {
        missiles.push_back({player.pos, player.turretDir});
        if (a.sndFirePlayer) playSound(*a.sndFirePlayer);
        fireCooldown = FireCooldown;
        voiceFireWait = 0.f;
    }
}

// ---------------- 语音控制(UDP 命令轮询) ----------------

void Game::pollVoice(float dt) {
    if (!voiceReady) return;
    // 非阻塞收命令(每帧最多 8 条, 命令格式见 tools/voice_control.cs)
    char buf[64];
    for (int i = 0; i < 8; ++i) {
        std::size_t got = 0;
        std::optional<sf::IpAddress> addr;
        unsigned short rport = 0;
        if (voiceSock.receive(buf, sizeof(buf) - 1, got, addr, rport) !=
            sf::Socket::Status::Done)
            break;
        buf[got] = '\0';
        std::string cmd(buf);
        while (!cmd.empty() &&
               (cmd.back() == '\n' || cmd.back() == '\r' || cmd.back() == ' '))
            cmd.pop_back();
        if (!cmd.empty()) handleVoiceCommand(cmd);
    }
    // 心跳: 告知助手游戏仍在运行(助手失联 5 秒自动退出)
    voicePingTimer -= dt;
    if (voicePingTimer <= 0.f) {
        voicePingTimer = 1.f;
        (void)voiceSock.send("PING", 4, sf::IpAddress(127, 0, 0, 1),
                             static_cast<unsigned short>(VoicePort + 1));
    }
    if (voiceMoveTimer > 0.f) {
        voiceMoveTimer -= dt;
        if (voiceMoveTimer <= 0.f) voiceMoveDir = -1.f;
    }
}

void Game::handleVoiceCommand(const std::string& cmd) {
    if (cmd == "FIRE") {
        voiceFireWait = 1.f; // 等冷却的窗口期
    } else if (cmd == "STOP") {
        voiceMoveDir = -1.f;
        voiceMoveTimer = 0.f;
        voiceTurretRemain = 0.f;
    } else if (cmd.rfind("TURRET_CW ", 0) == 0) {
        voiceTurretRemain = std::clamp(voiceTurretRemain + static_cast<float>(std::atoi(cmd.c_str() + 10)),
                                       -360.f, 360.f);
    } else if (cmd.rfind("TURRET_CCW ", 0) == 0) {
        voiceTurretRemain = std::clamp(voiceTurretRemain - static_cast<float>(std::atoi(cmd.c_str() + 11)),
                                       -360.f, 360.f);
    } else if (cmd == "MOVE_UP") {
        voiceMoveDir = 0.f;
        voiceMoveTimer = VoiceMoveMaxTime;
    } else if (cmd == "MOVE_DOWN") {
        voiceMoveDir = 180.f;
        voiceMoveTimer = VoiceMoveMaxTime;
    } else if (cmd == "MOVE_LEFT") {
        voiceMoveDir = -90.f;
        voiceMoveTimer = VoiceMoveMaxTime;
    } else if (cmd == "MOVE_RIGHT") {
        voiceMoveDir = 90.f;
        voiceMoveTimer = VoiceMoveMaxTime;
    }
}

void Game::updateEnemies(float dt) {
    // 广播“游戏结束”后敌方全部脚本停止: 不再生成/移动/开炮/播放爆炸动画
    if (phase == Phase::GameOverMusic) return;

    // 敌方本体脚本: 永远重复(克隆自己, 等待 1~5 秒)。
    // Scratch 里直到广播“游戏结束”才会停, 因此玩家爆炸期间仍在生成/开炮
    if (phase == Phase::Playing || phase == Phase::PlayerDying) {
        spawnTimer -= dt;
        if (spawnTimer <= 0.f) {
            spawnEnemy();
            std::uniform_real_distribution<float> wz(1.f, 5.f);
            spawnTimer = wz(rng);
        }
    }

    for (auto it = enemies.begin(); it != enemies.end();) {
        Enemy& e = *it;
        if (!e.exploding) {
            // 克隆体脚本1: 每帧前进 2 步, 碰到边缘反弹。
            // 按内容盒(含 centerOffset 偏移)旋转后的 AABB 判定, 仅当正朝着
            // 越界边运动时才翻转方向; 位置钳制用翻转后的盒(scratch-vm 语义)
            e.pos += stage::dirVector(e.dir) * EnemySpeed;
            const Costume& tank = a.enemyTank[e.costume];
            e.dir = stage::bounceOffEdges(e.dir, e.pos,
                                          tank.halfExtents, tank.centerOffset, 0.30f);

            // 克隆体脚本2: 每 2 秒朝自己朝向发一颗子弹
            e.fireTimer -= dt;
            if (e.fireTimer <= 0.f && (phase == Phase::Playing || phase == Phase::PlayerDying)) {
                bullets.push_back({e.pos, e.dir});
                if (a.sndFireEnemy) playSound(*a.sndFireEnemy);
                e.fireTimer = EnemyFireInterval;
            }
        } else {
            // 被导弹击中: 爆炸动画 b1..b6 后删除克隆体
            e.animTimer -= dt;
            if (e.animTimer <= 0.f) {
                ++e.frame;
                e.animTimer = ExplosionFrameTime;
            }
        }
        if (e.exploding && e.frame >= 6)
            it = enemies.erase(it);
        else
            ++it;
    }
}

void Game::updateProjectiles(float dt) {
    // 导弹: 每帧 10 步; 碰到边缘消失; 碰到敌方 -> 分数+1, 敌方爆炸, 导弹 0.1s 后消失
    for (auto it = missiles.begin(); it != missiles.end();) {
        Missile& m = *it;
        if (m.dyingTimer > 0.f) {
            m.dyingTimer -= dt;
            if (m.dyingTimer <= 0.f) it = missiles.erase(it);
            else ++it;
            continue;
        }
        m.pos += stage::dirVector(m.dir) * MissileSpeed;

        sf::Vector2f half = a.missile.halfExtents * 0.50f;
        bool out = stage::rotatedBoxAABB(a.missile.halfExtents, a.missile.centerOffset,
                                         0.50f, m.pos, m.dir)
                       .overlapsStageEdge();
        if (out) {
            it = missiles.erase(it);
            continue;
        }
        for (Enemy& e : enemies) {
            if (e.exploding) continue;
            if (hitTest(a.enemyTank[e.costume], 0.30f, e.pos, e.dir, m.pos, half)) {
                // 结算期间敌方脚本已全部停止: 导弹仍加分并消失, 但不再触发爆炸
                if (phase != Phase::GameOverMusic) {
                    e.exploding = true;
                    e.frame = 0;
                    e.animTimer = ExplosionFrameTime;
                    if (a.sndExplosion) playSound(*a.sndExplosion);
                }
                ++score; // “将 分数 增加 1”(导弹脚本的计分不随游戏结束停止)
                m.dyingTimer = 0.1f; // 命中后停留 0.1 秒
                break;
            }
        }
        ++it;
    }

    // 子弹: 每帧 5 步; 碰到边缘消失; 碰到玩家 -> 广播“被击中”
    for (auto it = bullets.begin(); it != bullets.end();) {
        Bullet& b = *it;
        b.pos += stage::dirVector(b.dir) * BulletSpeed;

        sf::Vector2f half = a.bullet.halfExtents * 0.5912f;
        bool out = stage::rotatedBoxAABB(a.bullet.halfExtents, a.bullet.centerOffset,
                                         0.5912f, b.pos, b.dir)
                       .overlapsStageEdge();
        if (out) {
            it = bullets.erase(it);
            continue;
        }
        if (phase == Phase::Playing && player.alive &&
            hitTest(a.playerBody, 0.30f, player.pos, player.dir, b.pos, half)) {
            // 玩家被击中: 车身播放爆炸动画(size 60%), 炮塔立即隐藏
            player.alive = false;
            player.explosionFrame = 0;
            player.explosionTimer = ExplosionFrameTime;
            phase = Phase::PlayerDying;
        }
        ++it;
    }
}

// ---------------- 渲染 ----------------

void Game::render(sf::RenderWindow& target) const {
    target.clear();
    target.draw(sf::Sprite(a.background));

    if (phase == Phase::Title || phase == Phase::TitleMusic) {
        // 开始画面(点击后音乐播完才进入游戏)
        target.draw(sf::Sprite(a.screenStart));
    } else {
        // 子弹(敌方炮弹)
        for (const Bullet& b : bullets)
            target.draw(makeSprite(a.bullet, 0.5912f, b.pos, b.dir));

        // 敌方坦克(正常=坦克造型 30%, 爆炸=b1..b6 放大到 60%)
        for (const Enemy& e : enemies) {
            if (e.exploding && e.frame < 6)
                target.draw(makeSprite(a.enemyExplosion[e.frame], 0.60f, e.pos, e.dir));
            else if (!e.exploding)
                target.draw(makeSprite(a.enemyTank[e.costume], 0.30f, e.pos, e.dir));
        }

        // 玩家(车身爆炸时显示 b1..b6 放大到 60%, 同时炮塔隐藏)
        if (player.explosionFrame >= 0 && player.explosionFrame < 6)
            target.draw(makeSprite(a.playerExplosion[player.explosionFrame], 0.60f,
                                   player.pos, player.dir));
        else if (player.alive || phase == Phase::Playing)
            target.draw(makeSprite(a.playerBody, 0.30f, player.pos, player.dir));

        // 导弹
        for (const Missile& m : missiles)
            target.draw(makeSprite(a.missile, 0.50f, m.pos, m.dir));

        // 炮塔(移到最前层): 跟随车身, 指向鼠标
        if (player.alive)
            target.draw(makeSprite(a.playerTurret, 0.30f, player.pos, player.turretDir));

        // 角色1 的操作提示(普通精灵层级, 会被结束画面盖住)。
        // 造型画布 291.33x21.36, 旋转中心在画布下方 131.6 单位之外,
        // 按旋转中心法则文字渲染于锚点(0,24)上方约 141.5 单位 = 舞台顶部 y≈+166
        if (a.font) {
            sf::Text help = makeText(utf8("上下左右为WSAD键，鼠标左键发射炮弹！"),
                                     30, sf::Color(0x44, 0x3c, 0x1b));
            sf::FloatRect b = help.getLocalBounds();
            help.setOrigin({b.size.x / 2.f, b.size.y / 2.f});
            help.setPosition(stage::toWindow({0.f, 166.f}));
            target.draw(help);
        }

        // 结束画面盖住整个舞台(普通精灵, 压住操作提示)
        if (phase == Phase::GameOverMusic || phase == Phase::Stopped)
            target.draw(sf::Sprite(a.screenGameOver));

        // 舞台变量“分数”监视器画在一切精灵之上(舞台监视器层级)
        if (a.font) {
            sf::Text scoreText =
                makeText(utf8("分数: " + std::to_string(score)),
                         16, sf::Color(0x22, 0x22, 0x22));
            scoreText.setPosition({8.f, 6.f});
            target.draw(scoreText);
        }
    }
}
