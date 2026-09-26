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
#include "common.hpp"

#include <random>
#include <vector>

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

    void handleEvent(const sf::Event& event);
    void update(float dt);       // 固定步长 1/30s, 对应 Scratch 的 30fps 帧模型
    void render(sf::RenderWindow& target) const;
    bool wantQuit() const { return quit; } // Esc / 关闭窗口

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
    // 玩家活动范围: motion_if x>-211 / x<205 / y>-154 / y<150
    static constexpr float PlayerMinX = -211.f, PlayerMaxX = 205.f;
    static constexpr float PlayerMinY = -154.f, PlayerMaxY = 150.f;

    void startGame();
    void spawnEnemy();
    void updatePlayer(float dt);
    void updateEnemies(float dt);
    void updateProjectiles(float dt);
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
    float spawnTimer = 0.f;      // 敌方生成计时(1~5 秒随机)
    std::mt19937 rng{std::random_device{}()};
    std::vector<sf::Sound> voices;      // 复用的播放通道
    std::optional<sf::Sound> titleMusic;     // 开始音乐(SFML 3 的 Sound 无默认构造)
    std::optional<sf::Sound> gameOverMusic;  // 结束音乐
};
