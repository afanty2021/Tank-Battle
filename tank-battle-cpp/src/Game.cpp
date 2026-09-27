#include "Game.hpp"
#include "NetSession.hpp"

#include <SFML/Window.hpp>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>

Game::Game(Assets& assets, sf::RenderWindow& win) : a(assets), window(win) {
    if (a.sndMusicStart) titleMusic.emplace(*a.sndMusicStart);
    if (a.sndMusicGameOver) gameOverMusic.emplace(*a.sndMusicGameOver);
    // 语音控制: 非阻塞收命令(助手由 main.cpp 拉起, 端口被占则功能禁用)
    voiceSock.setBlocking(false);
    voiceBound = voiceSock.bind(VoicePort) == sf::Socket::Status::Done;
    std::cout << (voiceBound ? "[voice] 语音控制已就绪(UDP " : "[voice] 端口 ")
              << VoicePort << (voiceBound ? ")" : " 被占用, 语音控制禁用") << '\n';
    // 心跳线程: 每秒向助手发 PING(失联 5 秒助手自动退出)。独立于主循环,
    // 主线程卡在拖动/缩放窗口的模态循环时心跳照发
    if (voiceBound)
        voicePingThread = std::jthread([this](std::stop_token st) {
            while (!st.stop_requested()) {
                (void)voicePingSock.send("PING", 4, sf::IpAddress(127, 0, 0, 1),
                                         static_cast<unsigned short>(VoicePort + 1));
                for (int i = 0; i < 10 && !st.stop_requested(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
}

Game::~Game() {
    // 联机收尾: 尽力发 BYE + 停心跳线程(NetSession 析构会再 close(false)
    // 兜底一次); 单人模式 net_ 为空, 行为与原隐式析构一致
    if (net_) net_->close(true);
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
        if (key->code == sf::Keyboard::Key::Escape) {
            // 联机中 Esc=离场(尽力 BYE+清理+回标题); 单人=退出
            if (mode_ != Mode::Solo) netLeave();
            else quit = true;
        }
        // Scratch 里“停止全部”后项目就停在那; 这里加一个 R 重开的便利功能
        if (key->code == sf::Keyboard::Key::R &&
            (phase == Phase::Stopped || phase == Phase::GameOverMusic)) {
            if (gameOverMusic) gameOverMusic->stop();
            phase = Phase::Title;
        }
        // 标题画面联机入口(Task 5): H=开主机局, J=扫描加入。
        // 单人玩法不使用 H/J 键, 与任何单人按键不冲突
        if (mode_ == Mode::Solo && phase == Phase::Title) {
            if (key->code == sf::Keyboard::Key::H) netStartHost();
            else if (key->code == sf::Keyboard::Key::J) netStartScan();
        }
    }
}

// ---------------- 每帧逻辑(1/30s) ----------------

void Game::update(float dt) {
    pollVoice(dt); // 语音是本地输入源, 单/联机两模式都要轮询(必须在联机早退之前)
    if (mode_ == Mode::NetHost) { netUpdateHost(dt); netBlinkTimer_ += dt; return; }
    if (mode_ == Mode::NetClient) { netUpdateClient(dt); netBlinkTimer_ += dt; return; }
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
    // 函数体抽为 updateTurretLocal(联机复用, 锚点参数化; 单人传 player.pos,
    // 逐语句等价的纯重构)。守卫留在本调用点不进函数——联机会话期间
    // Game::phase 恒为 Title, 守卫进了函数体联机炮塔就永远不更新
    if (phase != Phase::Title && phase != Phase::TitleMusic && phase != Phase::Stopped)
        updateTurretLocal(dt, player.pos);

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

// 从 updatePlayer 抽出的炮塔跟随段(单人路径的纯重构, 守卫在调用点)。
// 鼠标坐标经当前视图映射回 960x720 逻辑系(窗口缩放后依然准确)。
// 移植版附加(非原版): ←/→ 方向键逆/顺时针旋转炮塔, 接管期间暂停
// 鼠标跟随; 鼠标位置一变立即恢复原版的“面向鼠标”
void Game::updateTurretLocal(float dt, sf::Vector2f anchorPos) {
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
            anchorPos, stage::toStage(window.mapPixelToCoords(mouseNow)));
    }
}

// ---------------- 联机(Task 5; 单人路径不动, 全部以 mode_ != Solo 为闸) ----------------
// 除照抄任务简报外, 本节含五处联机专属的防御性修正(均不触碰单人路径,
// 详见 task-5-report.md 的偏差清单):
//  A) netHandleEvents 内可能 netLeave()(掉线/版本不符), 其后 net_ 为空,
//     两个 netUpdate* 在事件处理后统一判空早退(否则必空引用崩溃);
//  B) 暂停帧不步进模拟, fired/hit/died 保留上帧值 -> 暂停时清位,
//     否则暂停起始帧的事件会整段暂停期在本机与快照里反复重放;
//  C) “新快照”以 tick 判定: NetSession::snap() 恒指向最新已接受帧(并非
//     “本帧新到”), 不判 tick 会每帧重放事件音效, 且 noSnapSince 永不累计
//     (Task 6 的“对端卡住”兜底标签随之失效);
//  D) netLeave 补 joiningStarted_/netJoinWait_ 复位, netStartHost 补
//     battle_ 复位(Esc 后同进程再开 H/J 不吃上一场残留状态);
//  E) voiceFireWait 的递减原在单人 updatePlayer 里, 联机帧在 netUpdate*
//     中递减, 否则语音“开炮”一次后在联机里永久连发。

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
    battle_ = BattleState{}; // (D) 上一场残局(Over)会挡住倒计时首帧注入
}

void Game::netStartScan() {
    net_ = std::make_unique<NetSession>();
    net_->startScan(machineName());
    mode_ = Mode::NetClient;
}

void Game::netStartJoin(sf::IpAddress host) {
    net_->startJoin(host, machineName());
}

void Game::requestDirectJoin(const std::string& hostIp) {
    // main.cpp --join 直连(Task 7 接命令行): 跳过扫描直接进 Joining 等首个 SNAP。
    // SFML 3 无 string 构造(sf::IpAddress(hostIp) 是 SFML 2 写法), 静态
    // resolve 返回 optional; 非法 IP 串则不进联机(保持 Solo)
    if (const auto ip = sf::IpAddress::resolve(hostIp)) {
        net_ = std::make_unique<NetSession>();
        mode_ = Mode::NetClient;
        net_->startJoin(*ip, machineName());
        joiningStarted_ = true;
        netJoinWait_ = 0.f;
    } else {
        std::cout << "[net] 无效的主机地址: " << hostIp << '\n';
    }
}

void Game::netLeave() {
    if (net_) net_->close(true); // 尽力 BYE; UDP 丢包则对端 5s 无包超时兜底
    net_.reset(); mode_ = Mode::Solo; phase = Phase::Title;
    netLocalReady_ = false; view_ = NetView{};
    joiningStarted_ = false; netJoinWait_ = 0.f; // (D) Esc 后再 J 不吃 5s 旧等待
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

void Game::netHandleEvents() {
    for (const NetEvent& ev : net_->takeEvents()) {
        if (!net_) break; // (A) 同批后续事件属于已离场会话(Busy 分支要用 net_)
        if (ev.kind == NetEvent::Kind::Disconnected) {
            std::cout << "[net] 对方已离开, 回标题\n";
            netLeave();
        } else if (ev.kind == NetEvent::Kind::Busy) {
            // spec §4: busy 退回扫描页(不是回标题)。Busy/VersionMismatch 以
            // 5Hz 重复到达, 本分支让会话离开 Joining 态, 天然只处理首个
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

void Game::netUpdateHost(float dt) {
    net_->poll(dt);
    netHandleEvents();
    if (!net_) return; // (A) 事件里可能已 netLeave()
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
    if (voiceFireWait > 0.f) voiceFireWait -= dt; // (E) 递减原在单人 updatePlayer
    InputState in[2] = {buildLocalInput(), net_->remoteInput()};
    // 暂停判定仅战斗阶段适用(spec §6.3); 倒计时/结算期客户端不发 INP 不算停滞
    const bool paused = battle_.phase == BattlePhase::Battle &&
                        net_->remoteInputStale();
    if (!paused) {
        stepBattle(battle_, netDefs_, in, dt);
    } else {
        // (B) 暂停帧不步进: 清事件位, 否则暂停起始帧的 fired/hit/died 会
        // 在整段暂停期反复出声并随每个新快照重放给客户端
        for (int i = 0; i < 2; ++i)
            battle_.fired[i] = battle_.hit[i] = battle_.died[i] = false;
    }
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
    if (!net_) return; // (A) 事件里可能已 netLeave()
    // 扫描期: 2s 后有结果(0 个->提示重试; >=1 个->startJoin 第一个)
    // (多个主机列表选择 UI 在 Task 6 Step 2.5; 先自动连第一个)
    if (!view_.has && !net_->foundHosts().empty() && !joiningStarted_) {
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
    if (inGame) {
        if (voiceFireWait > 0.f) voiceFireWait -= dt; // (E) 递减原在单人 updatePlayer
        net_->sendInput(buildLocalInput()); // 空闲也发全零, 暂停判定依赖
    }
    // (C) 新快照以 tick 判定(每个 tick 恰好处理一次); 无新快照才累计
    // noSnapSince, Task 6 据此画“对端卡住”兜底标签
    if (net_->snap() && (!view_.has || net_->snap()->tick != view_.snap.tick)) {
        const proto::SnapMsg& sn = *net_->snap();
        const bool nowCd = sn.phase == proto::Phase::Countdown;
        if (nowCd && !view_.wasCountdown) view_.countdownLocal = 3.f; // 本地倒计时(N3)
        view_.wasCountdown = nowCd;
        view_.snap = sn; view_.has = true;
        view_.noSnapSince = 0.f;
        for (int i = 0; i < 2; ++i) {        // 事件音效(每 tick 一次)
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

// ---------------- 语音控制(UDP 命令轮询) ----------------

void Game::pollVoice(float dt) {
    if (!voiceBound) return;
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
    if (voiceMoveTimer > 0.f) {
        voiceMoveTimer -= dt;
        if (voiceMoveTimer <= 0.f) voiceMoveDir = -1.f;
    }
}

void Game::handleVoiceCommand(const std::string& cmd) {
    const voice::Command c = voice::parse(cmd); // 解析在 common.hpp, 有单测
    switch (c.kind) {
    case voice::Command::Kind::Fire:
        voiceFireWait = 1.f; // 等冷却的窗口期
        break;
    case voice::Command::Kind::Stop:
        voiceMoveDir = -1.f;
        voiceMoveTimer = 0.f;
        voiceTurretRemain = 0.f;
        break;
    case voice::Command::Kind::TurretCW:
    case voice::Command::Kind::TurretCCW:
        voiceTurretRemain = voice::applyTurret(voiceTurretRemain, c);
        break;
    case voice::Command::Kind::MoveUp:
        voiceMoveDir = 0.f;
        voiceMoveTimer = VoiceMoveMaxTime;
        break;
    case voice::Command::Kind::MoveDown:
        voiceMoveDir = 180.f;
        voiceMoveTimer = VoiceMoveMaxTime;
        break;
    case voice::Command::Kind::MoveLeft:
        voiceMoveDir = -90.f;
        voiceMoveTimer = VoiceMoveMaxTime;
        break;
    case voice::Command::Kind::MoveRight:
        voiceMoveDir = 90.f;
        voiceMoveTimer = VoiceMoveMaxTime;
        break;
    case voice::Command::Kind::None:
        break;
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
