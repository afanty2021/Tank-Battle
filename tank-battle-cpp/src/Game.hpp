#pragma once
// 《坦克大战》游戏主体 —— 逐条对应原 Scratch 工程里的各角色脚本
//
// Scratch 角色        -> C++ 对应
//   玩家坦克车身      -> Player(车身移动/爆炸动画)
//   玩家坦克炮塔      -> Player::turretDir(跟随车身、指向鼠标)
//   敌方坦克1 + 克隆  -> std::vector<Enemy>(每帧生成/移动/开炮/爆炸)
//   导弹   + 克隆     -> std::vector<Missile>(鼠标左键发射)
//   子弹   + 克隆     -> std::vector<Bullet>(敌人每 2 秒发射)
//   开始游戏/游戏结束 -> Phase::Title / Phase::GameOverMusic
//   角色1(操作提示)   -> helpText
//   舞台变量 分数     -> score
#include "Assets.hpp"
#include "Battle.hpp"
#include "common.hpp"

#include <SFML/Network.hpp>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

class NetSession; // 联机会话层(Task 4), Game.cpp 内使用完整定义

struct Player {
    sf::Vector2f pos;
    float dir = 0.f;            // 车身朝向(Scratch 方向)
    float turretDir = 0.f;      // 炮塔朝向, 每帧指向鼠标
    bool alive = true;          // 是否已被子弹击中
    int explosionFrame = -1;    // -1=正常; 0..5=爆炸动画进行中
    float explosionTimer = 0.f; // 每帧 0.1s
};

struct Enemy {
    sf::Vector2f pos;
    float dir;
    int costume;                // 0 或 1 (敌方坦克1/敌方坦克2 造型)
    bool exploding = false;
    int frame = 0;
    float animTimer = 0.f;      // 爆炸帧计时(0.1s/帧)
    float fireTimer = 2.f;      // 每 2 秒开炮
};

struct Missile {
    sf::Vector2f pos;
    float dir;
    float dyingTimer = 0.f;     // 命中后原地停留 0.1s 再消失(还原 Scratch 的 wait 0.1)
};

struct Bullet {
    sf::Vector2f pos;
    float dir;
};

enum class Phase {
    Title,           // 开始画面: 等待鼠标点击
    TitleMusic,      // 点击后播放开始音乐, 播完正式开始
    Playing,         // 游戏进行中
    PlayerDying,     // 玩家爆炸动画中(0.6s)
    GameOverMusic,   // 游戏结束画面 + 结束音乐, 播完冻结
    Stopped,         // 一切停止(对应 Scratch 的“停止全部”); 按 R 重新开始
};

class Game {
public:
    Game(Assets& assets, sf::RenderWindow& window);
    ~Game(); // 联机收尾: 尽力 BYE+停心跳(net_ 析构再 close 兜底; 单人无操作)

    void handleEvent(const sf::Event& event);
    void update(float dt);       // 固定步长 1/30s, 对应 Scratch 的 30fps 帧模型
    void render(sf::RenderWindow& target) const;
    bool wantQuit() const { return quit; } // Esc / 关闭窗口
    bool voiceReady() const { return voiceBound; } // 未接管 UDP 端口时为 false

    // ---- 联机模式(Task 5): 默认 Solo, 一切联机路径以 mode_ != Solo 为闸 ----
    enum class Mode { Solo, NetHost, NetClient };
    Mode mode() const { return mode_; }
    void requestDirectJoin(const std::string& hostIp); // main.cpp --join 用

private:
    // ---- 数值全部来自 Scratch 积木(每“步”都是每帧位移) ----
    static constexpr float PlayerSpeed = 5.f;      // motion_changeyby 5
    static constexpr float EnemySpeed = 2.f;       // motion_movesteps 2
    static constexpr float MissileSpeed = 10.f;    // motion_movesteps 10
    static constexpr float BulletSpeed = 5.f;      // motion_movesteps 5
    static constexpr float FireCooldown = 0.5f;    // control_wait 0.5
    static constexpr float EnemyFireInterval = 2.f;// control_wait 2
    static constexpr float ExplosionFrameTime = 0.1f;
    static constexpr float TurretTurnSpeed = 180.f; // ←/→ 键旋转炮塔(移植版新增, 每秒 180°)
    // 语音控制(移植版新增, 仅 Windows, 助手见 tools/voice_control.cs):
    // 助手识别中文口令后经本机 UDP 发命令; 心跳失联 5 秒助手自动退出
    static constexpr unsigned short VoicePort = 52017;
    static constexpr float VoiceTurretSpeed = 360.f;  // 语音口令的炮塔转速(度/秒)
    static constexpr float VoiceMoveMaxTime = 3.f;    // 移动口令无“停”时的自动停止
    // 玩家活动范围: motion_if x>-211 / x<205 / y>-154 / y<150
    static constexpr float PlayerMinX = -211.f, PlayerMaxX = 205.f;
    static constexpr float PlayerMinY = -154.f, PlayerMaxY = 150.f;

    void startGame();
    void spawnEnemy();
    void updatePlayer(float dt);
    void updateEnemies(float dt);
    void updateProjectiles(float dt);
    void pollVoice(float dt);
    void handleVoiceCommand(const std::string& cmd);
    void playSound(const sf::SoundBuffer& buffer);
    static sf::Sprite makeSprite(const Costume& c, float sizePercent,
                                 sf::Vector2f pos, float dir);
    static bool hitTest(const Costume& c, float sizePercent, sf::Vector2f center,
                        float dir, sf::Vector2f point, sf::Vector2f pointHalf);
    sf::Text makeText(const sf::String& str, unsigned size, sf::Color color) const;

    Assets& a;
    sf::RenderWindow& window;   // 用于读取鼠标位置(炮塔指向)
    Phase phase = Phase::Title;
    bool quit = false;
    int score = 0;
    Player player;
    std::vector<Enemy> enemies;
    std::vector<Missile> missiles;
    std::vector<Bullet> bullets;
    float fireCooldown = 0.f;
    bool turretManual = false;  // 方向键接管炮塔期间暂停鼠标跟随; 鼠标一动即恢复
    sf::Vector2i lastMousePos{};
    // 语音控制状态(端口绑定失败则 voiceBound=false, 功能自动禁用)
    sf::UdpSocket voiceSock;      // 收命令(仅主线程使用)
    sf::UdpSocket voicePingSock;  // 发心跳(仅后台线程使用; SFML socket 非线程安全)
    // 心跳独立于主循环: 拖动/缩放窗口时 Windows 进入模态循环, 主线程的
    // update() 不再执行, 若心跳挂在 update 里会被 5 秒超时误杀助手
    std::jthread voicePingThread;
    bool voiceBound = false;
    float voiceTurretRemain = 0.f; // 待旋转角度(带符号, 正=顺时针, 语音/方向键共用 manual 态)
    float voiceMoveDir = -1.f;     // 语音移动方向(Scratch 方向值; <0=无)
    float voiceMoveTimer = 0.f;    // 语音移动自动停止计时
    float voiceFireWait = 0.f;     // “开炮”等待冷却的窗口期
    float spawnTimer = 0.f;      // 敌方生成计时(1~5 秒随机)
    std::mt19937 rng{std::random_device{}()};
    std::vector<sf::Sound> voices;      // 复用的播放通道
    std::optional<sf::Sound> titleMusic;     // 开始音乐(SFML 3 的 Sound 无默认构造)
    std::optional<sf::Sound> gameOverMusic;  // 结束音乐

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
    // 联机画面(Task 6): render() 以 mode_ != Solo 早退进来; 自绘背景,
    // 大厅/倒计时/战斗(双坦克+HUD)/结算/暂停标签全在此, 不碰单人 if/else
    void renderNet(sf::RenderWindow& target) const;
    void drawNetHud(sf::RenderWindow& target) const; // 血条(Step 3)
    void drawCenteredText(sf::RenderWindow& target, const sf::String& str,
                          unsigned size, sf::Color color,
                          sf::Vector2f stagePos) const; // 居中文字小工具
    Mode mode_ = Mode::Solo;
    std::unique_ptr<NetSession> net_;
    BattleDefs netDefs_;
    BattleState battle_;                // 主机模拟用
    NetView view_;                      // 客户端渲染用
    bool netLocalReady_ = false;        // 本机 R(结算期)
    bool netWasOver_ = false;           // 客户端: 用于 Over->新局跳变时清 ready
    bool joiningStarted_ = false;       // 客户端: 扫描选定主机后置位
    bool netScanDone_ = false;          // 客户端: 2s 扫描窗已结束(Step 2.5:
                                        // 0 个提示重试/恰 1 个自动连/>1 个列列表)
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
};
