#pragma once
// ============================================================
//  scratch_like.hpp —— 教学工具箱(给"Scratch 转 C++"课程用)
//
//  它把开窗口/加载图片/按键检测这些麻烦事全部藏起来,
//  只留给你几个函数, 名字和你在 Scratch 里用过的积木一一对应。
//  坐标和方向与 Scratch 完全一样: 舞台中心是 (0,0), x 向右为正
//  (-240~240), y 向上为正(-180~180); 方向 0=朝上, 90=朝右,
//  180=朝下, 270=朝左(顺时针)。
//
//  本文件由老师提供, 第 4 课开始可以打开研究它的内部实现。
// ============================================================
#include "../src/common.hpp"

#include <SFML/Audio.hpp>
#include <SFML/Graphics.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <vector>

// ---------- 你会用的函数(看签名和注释即可, 不用看懂实现) ----------

// ---- 第 1~2 课就会用的 ----
void open_window();                      // 【打开窗口】960x720
bool window_open();                      // 窗口还开着吗? 点 X 或按 Esc 变 false
void clear();                            // 擦干净画面(每一帧的第一句)
void draw_background();                  // 画出坦克大战的战场背景
void draw_tank(float x, float y, float dir);     // 画你的坦克(车身+炮塔)
void draw_enemy(float x, float y, float dir);    // 画一辆敌方坦克
bool key_pressed(const std::string& key);        // 【按下 W 键?】W/A/S/D/数字/SPACE/UP...
void display();                          // 把这一帧显示出来(每一帧最后一句)

// ---- 第 5 课: 导弹 ----
void draw_missile(float x, float y, float dir);  // 画一颗导弹

// ---- 第 6 课: 随机数 / 碰撞 / 文字 ----
float random_int(int a, int b);          // 【在 a 到 b 间随机选一个数】
                                         // (答案一定是整数, 用 float 装着方便当坐标用)
bool touching(float x1, float y1, float x2, float y2); // 【碰到了吗?】两点近于 20
void draw_text(const std::string& s, float x, float y); // 【说】在 (x,y) 写一行字

// ---- 第 7 课: 声音和过场画面 ----
void play_sound(const std::string& name);   // "fire"我方开炮 "boom"爆炸 "enemy"敌炮
void play_music(const std::string& name);   // "start"开场曲 "over"结束曲(整曲播放)
void draw_start_screen();                   // 开场画面(占满全屏)
void draw_gameover_screen();                // 结束画面(占满全屏)

// ---- 第 8 课: 鼠标与瞄准 ----
float mouse_x();                         // 【鼠标的 x 坐标】(舞台坐标)
float mouse_y();                         // 【鼠标的 y 坐标】
float face_direction(float x1, float y1, float x2, float y2); // 【面向 _】的角度
void draw_turret(float x, float y, float dir);  // 只画炮塔(车身不动炮塔转时用)
float step_x(float dir, float steps);    // 【移动 steps 步】的横向分量
float step_y(float dir, float steps);    // 【移动 steps 步】的纵向分量

// ---------- 内部实现(第 4 课以后再看) ----------
namespace sl {

// 所有 SFML 对象集中在一个堆分配的 State 里: 开窗时 new 一份,
// 集中生灭, 也避开静态全局对象在退出阶段的析构顺序问题。
struct State {
    sf::RenderWindow win;                // 游戏窗口
    sf::Texture bgTex, bodyTex, turretTex, enemyTex, missileTex,
                startTex, overTex;       // 用到的图片
    sf::Font font;                       // 中文字体(和正式版同一个子集)
    sf::SoundBuffer fireBuf, boomBuf, enemyBuf, musicStartBuf, musicOverBuf;
    std::vector<sf::Sound> playing;      // 短音效通道(先清停再入列, 同正式版)
    std::optional<sf::Sound> musicCh;    // 音乐专用单通道(SFML 3 的 Sound
                                         // 无默认构造, 用 optional 装配)
    std::mt19937 rng{std::random_device{}()};
};

inline State* s = nullptr;               // 纯指针, 不是 SFML 对象, 做全局安全

inline State& S() {                      // 所有函数进门先问: 开窗了吗
    if (!s) {
        std::cerr << "先调用 open_window() 才能用其他函数!\n";
        std::exit(1);
    }
    return *s;
}

inline void load(sf::Texture& t, const char* file) {
    if (t.loadFromFile(file)) return;
    std::cerr << "找不到图片 " << file
              << "\n请在 tank-battle-cpp 目录里运行程序(assets 文件夹要在旁边)\n";
    std::exit(1);
}

// 旋转轴心(画布像素, 数据来自 assets/manifest.json——改素材时同步改这里。
// 常数只在这里写一遍, draw_tank/draw_turret 共用, 防止三处拷贝漂移)
inline const sf::Vector2f kBodyOrigin{112.15f * stage::Scale, 54.04f * stage::Scale};
inline const sf::Vector2f kTurretOrigin{63.5f * stage::Scale, 45.12f * stage::Scale};
inline const sf::Vector2f kMissileOrigin{28.f * stage::Scale, 9.f * stage::Scale};

// 按游戏里的样子画一张精灵图: origin 是旋转轴心(画布像素),
// size 是大小百分比, (x,y)/dir 用 Scratch 约定
inline sf::Sprite make(const sf::Texture& t, sf::Vector2f origin, float size,
                       float x, float y, float dir) {
    sf::Sprite spr(t);
    spr.setScale({size, size});
    spr.setOrigin(origin);
    spr.setPosition(stage::toWindow({x, y}));
    spr.setRotation(stage::spriteRotation(dir));
    return spr;
}

// 清掉播完的通道, 再开一条新的播 buf——与正式版 Game::playSound 同一
// 套路: 先清已停的再入列, 通道数组不会越积越长
inline void play_buffer(const sf::SoundBuffer& buf) {
    auto& v = S().playing;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const sf::Sound& snd) {
                               return snd.getStatus() == sf::Sound::Status::Stopped;
                           }),
            v.end());
    v.emplace_back(buf);
    v.back().play();
}

} // namespace sl

#ifdef _WIN32
extern "C" __declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int);
#endif

inline void open_window() {
    using namespace sl;
    if (s) return;
#ifdef _WIN32
    SetConsoleOutputCP(65001);     // 让中文报错在默认 cmd(GBK)里也不乱码
#endif
    s = new State();
    // 退出时不特意销毁(几 MB 由操作系统整体回收): 教学程序不把
    // 精力花在退出阶段的析构顺序上
    State& g = *s;
    g.win.create(sf::VideoMode({960, 720}), utf8("坦克大战 · 教学版"),
                 sf::Style::Close);            // 固定大小窗口, 免去缩放的数学
    g.win.setFramerateLimit(30);               // 30 帧/秒, 和 Scratch 一样
    load(g.bgTex,     "assets/images/background.jpg");
    load(g.bodyTex,   "assets/images/player_body.png");
    load(g.turretTex, "assets/images/player_turret.png");
    load(g.enemyTex,  "assets/images/enemy_tank_1.png");
    load(g.missileTex,"assets/images/missile.png");
    load(g.startTex,  "assets/images/screen_start.jpg");
    load(g.overTex,   "assets/images/screen_gameover.jpg");
    if (!g.font.openFromFile("assets/fonts/NotoSansSC-Game.otf")) {
        std::cerr << "找不到字体 assets/fonts/NotoSansSC-Game.otf\n";
        std::exit(1);
    }
    if (!g.fireBuf.loadFromFile("assets/sounds/fire_player.ogg") ||
        !g.boomBuf.loadFromFile("assets/sounds/explosion.ogg") ||
        !g.enemyBuf.loadFromFile("assets/sounds/fire_enemy.ogg") ||
        !g.musicStartBuf.loadFromFile("assets/sounds/music_start.ogg") ||
        !g.musicOverBuf.loadFromFile("assets/sounds/music_gameover.ogg")) {
        std::cerr << "找不到音效(assets/sounds/*.ogg)\n";
        std::exit(1);
    }
    g.playing.reserve(8);          // 预留容量, 连发音效也不触发扩容搬移
}

inline bool window_open() {
    using namespace sl;
    auto& win = S().win;
    while (const auto ev = win.pollEvent()) {
        if (ev->is<sf::Event::Closed>())
            win.close();                       // 点了右上角 X
        else if (const auto* k = ev->getIf<sf::Event::KeyPressed>();
                 k && k->code == sf::Keyboard::Key::Escape)
            win.close();                       // 按 Esc 也能关(和正式版一样)
    }
    return win.isOpen();
}

inline void clear() {
    sl::S().win.clear(sf::Color::White);       // 底色刷白, 像 Scratch 的空白舞台
}

inline void display() {
    sl::S().win.display();     // 画好的东西这一刻才出现在屏幕上
}

inline void draw_background() {
    auto& g = sl::S();
    g.win.draw(sf::Sprite(g.bgTex));           // 背景图正好 960x720, 铺满窗口
}

inline void draw_tank(float x, float y, float dir) {
    using namespace sl;
    auto& g = S();
    // 车身和炮塔都画在 (x,y), 旋转轴心用上面统一定义的三个常数
    g.win.draw(make(g.bodyTex, kBodyOrigin, 0.30f, x, y, dir));
    g.win.draw(make(g.turretTex, kTurretOrigin, 0.30f, x, y, dir));
}

inline void draw_turret(float x, float y, float dir) {
    using namespace sl;
    auto& g = S();
    g.win.draw(make(g.turretTex, kTurretOrigin, 0.30f, x, y, dir));
}

inline void draw_enemy(float x, float y, float dir) {
    using namespace sl;
    auto& g = S();
    // 敌方坦克的旋转轴心在炮口, 教学版改为绕图片中心转: (x,y) 就是坦克中心
    const sf::Vector2f center = sf::Vector2f(g.enemyTex.getSize()) / 2.f;
    g.win.draw(make(g.enemyTex, center, 0.30f, x, y, dir));
}

inline void draw_missile(float x, float y, float dir) {
    using namespace sl;
    auto& g = S();
    g.win.draw(make(g.missileTex, kMissileOrigin, 0.50f, x, y, dir));
}

inline bool key_pressed(const std::string& key) {
    sl::S();
    std::string k;
    for (char c : key)
        k += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (k.size() == 1 && k[0] >= 'A' && k[0] <= 'Z')     // 26 个字母键
        return sf::Keyboard::isKeyPressed(
            static_cast<sf::Keyboard::Key>(k[0] - 'A'));
    if (k.size() == 1 && k[0] >= '0' && k[0] <= '9')     // 数字键(选难度用)
        return sf::Keyboard::isKeyPressed(
            static_cast<sf::Keyboard::Key>(
                static_cast<int>(sf::Keyboard::Key::Num0) + (k[0] - '0')));
    if (k == "SPACE")  return sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Space);
    if (k == "UP")     return sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up);
    if (k == "DOWN")   return sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down);
    if (k == "LEFT")   return sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left);
    if (k == "RIGHT")  return sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right);
    return false;
}

inline float random_int(int a, int b) {
    auto& g = sl::S();
    if (b < a) std::swap(a, b);                // 传反了也不出错
    std::uniform_int_distribution<int> d(a, b);
    return static_cast<float>(d(g.rng));       // 整数答案, float 类型
}

inline bool touching(float x1, float y1, float x2, float y2) {
    // 两点距离小于 20(舞台单位) 就算碰上——正式版用的是更精确的
    // 旋转矩形碰撞, 教学版先用"够近就算"的圆圈判定
    const float dx = x1 - x2, dy = y1 - y2;
    return dx * dx + dy * dy < 20.f * 20.f;
}

inline void draw_text(const std::string& s, float x, float y) {
    using namespace sl;
    auto& g = S();
    sf::Text t(g.font, utf8(s), 30);           // 30 号字, 深棕色(同正式版提示文字)
    t.setFillColor(sf::Color(0x44, 0x3c, 0x1b));
    sf::FloatRect b = t.getLocalBounds();
    t.setOrigin({b.size.x / 2.f, b.size.y / 2.f}); // 以 (x,y) 为文字中心
    t.setPosition(stage::toWindow({x, y}));
    g.win.draw(t);
}

inline void play_sound(const std::string& name) {
    using namespace sl;
    auto& g = S();
    if (name == "fire")       play_buffer(g.fireBuf);
    else if (name == "boom")  play_buffer(g.boomBuf);
    else if (name == "enemy") play_buffer(g.enemyBuf);
}

inline void play_music(const std::string& name) {
    using namespace sl;
    auto& g = S();
    // 音乐用专用单通道: 同一时刻只有一首, 重开即从头播。
    // emplace 会先析构旧通道(自动停播)再构造新的, 不经过 vector,
    // 避免"正在播放的通道被搬移"弄坏堆
    if (name == "start") {
        g.musicCh.emplace(g.musicStartBuf);
        g.musicCh->play();
    } else if (name == "over") {
        g.musicCh.emplace(g.musicOverBuf);
        g.musicCh->play();
    }
}

inline void draw_start_screen() {
    auto& g = sl::S();
    g.win.draw(sf::Sprite(g.startTex));        // 开场图正好 960x720
}

inline void draw_gameover_screen() {
    auto& g = sl::S();
    g.win.draw(sf::Sprite(g.overTex));
}

inline float mouse_x() {
    auto& g = sl::S();
    return stage::toStage(
        sf::Vector2f(g.win.mapPixelToCoords(sf::Mouse::getPosition(g.win)))).x;
}

inline float mouse_y() {
    auto& g = sl::S();
    return stage::toStage(
        sf::Vector2f(g.win.mapPixelToCoords(sf::Mouse::getPosition(g.win)))).y;
}

inline float face_direction(float x1, float y1, float x2, float y2) {
    sl::S();
    return stage::pointDirection({x1, y1}, {x2, y2}); // 【面向 _】的 Scratch 角度
}

inline float step_x(float dir, float steps) {
    sl::S();
    return stage::dirVector(dir).x * steps; // 【移动 steps 步】= 沿朝向走
}

inline float step_y(float dir, float steps) {
    sl::S();
    return stage::dirVector(dir).y * steps;
}
