#pragma once
// ============================================================
//  scratch_like.hpp —— 教学工具箱(给"Scratch 转 C++"课程用)
//
//  它把开窗口/加载图片/按键检测这些麻烦事全部藏起来,
//  只留给你几个函数, 名字和你在 Scratch 里用过的积木一一对应:
//
//    open_window()                      打开游戏窗口
//    window_open()                      窗口还开着吗? 点 X 或按 Esc 会变 false
//    clear()                            擦干净画面(每一帧的第一句)
//    draw_background()                  画出坦克大战的战场背景
//    draw_tank(x, y, dir)               画你的坦克, (x,y) 是坦克中心
//    draw_enemy(x, y, dir)              画一辆敌方坦克
//    key_pressed("W")                   某个键正被按着吗? (W A S D SPACE UP...)
//    display()                          把这一帧显示出来(每一帧的最后一句)
//
//  坐标和方向与 Scratch 完全一样:
//    舞台中心是 (0,0), x 向右为正(-240~240), y 向上为正(-180~180);
//    方向 0=朝上, 90=朝右, 180=朝下, 270=朝左(顺时针)。
//
//  本文件由老师提供, 现在不用看懂它 —— 第 4 课以后再来研究内部实现。
// ============================================================
#include "../src/common.hpp"

#include <SFML/Graphics.hpp>

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>

// ---------- 你要用的函数(声明) ----------
void open_window();
bool window_open();
void clear();
void draw_background();
void draw_tank(float x, float y, float dir);
void draw_enemy(float x, float y, float dir);
bool key_pressed(const std::string& key);
void display();

// ---------- 内部实现(第 4 课以后再看) ----------
namespace sl {

inline sf::RenderWindow win;                 // 游戏窗口
inline sf::Texture bgTex, bodyTex, turretTex, enemyTex; // 用到的 4 张图
inline bool ready = false;                   // open_window() 调过了吗

inline void check_open() {                   // 所有函数进门先问一句: 开窗了吗
    if (!ready) {
        std::cerr << "先调用 open_window() 才能用其他函数!\n";
        std::exit(1);
    }
}

inline bool load(sf::Texture& t, const char* file) {
    if (t.loadFromFile(file)) return true;
    std::cerr << "找不到图片 " << file
              << "\n请在 tank-battle-cpp 目录里运行程序(assets 文件夹要在旁边)\n";
    std::exit(1);
}

// 按游戏里的样子画一张精灵图: origin 是旋转轴心, (x,y)/dir 用 Scratch 约定
inline sf::Sprite make(const sf::Texture& t, sf::Vector2f origin,
                       float x, float y, float dir) {
    sf::Sprite s(t);
    s.setScale({0.30f, 0.30f});              // 和正式版一样的大小(30%)
    s.setOrigin(origin);
    s.setPosition(stage::toWindow({x, y}));  // 舞台坐标 -> 窗口像素
    s.setRotation(stage::spriteRotation(dir));
    return s;
}

} // namespace sl

inline void open_window() {
    using namespace sl;
    if (ready) return;
    win.create(sf::VideoMode({960, 720}), utf8("坦克大战 · 教学版"),
               sf::Style::Close);            // 固定大小窗口, 免去缩放的数学
    win.setFramerateLimit(30);               // 30 帧/秒, 和 Scratch 一样
    load(bgTex, "assets/images/background.jpg");
    load(bodyTex, "assets/images/player_body.png");
    load(turretTex, "assets/images/player_turret.png");
    load(enemyTex, "assets/images/enemy_tank_1.png");
    ready = true;
}

inline bool window_open() {
    using namespace sl;
    check_open();
    while (const auto ev = win.pollEvent()) {
        if (ev->is<sf::Event::Closed>())
            win.close();                     // 点了右上角 X
        else if (const auto* k = ev->getIf<sf::Event::KeyPressed>();
                 k && k->code == sf::Keyboard::Key::Escape)
            win.close();                     // 按 Esc 也能关(和正式版一样)
    }
    return win.isOpen();
}

inline void clear() {
    sl::check_open();
    sl::win.clear(sf::Color::White);         // 底色刷白, 像 Scratch 的空白舞台
}

inline void draw_background() {
    sl::check_open();
    // 背景图正好是 960x720, 铺满整个窗口
    sl::win.draw(sf::Sprite(sl::bgTex));
}

inline void draw_tank(float x, float y, float dir) {
    using namespace sl;
    check_open();
    // 车身和炮塔都画在 (x,y): 旋转中心数据来自 assets/manifest.json
    win.draw(make(bodyTex, {112.15f * stage::Scale, 54.04f * stage::Scale}, x, y, dir));
    win.draw(make(turretTex, {63.5f * stage::Scale, 45.12f * stage::Scale}, x, y, dir));
}

inline void draw_enemy(float x, float y, float dir) {
    using namespace sl;
    check_open();
    // 敌方坦克的旋转轴心在炮口, 教学版改为绕图片中心转: (x,y) 就是坦克中心
    const sf::Vector2f center = sf::Vector2f(enemyTex.getSize()) / 2.f;
    win.draw(make(enemyTex, center, x, y, dir));
}

inline bool key_pressed(const std::string& key) {
    using namespace sl;
    check_open();
    std::string k;
    for (char c : key)
        k += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (k.size() == 1 && k[0] >= 'A' && k[0] <= 'Z')     // 26 个字母键
        return sf::Keyboard::isKeyPressed(
            static_cast<sf::Keyboard::Key>(k[0] - 'A'));
    if (k == "SPACE")  return sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Space);
    if (k == "UP")     return sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up);
    if (k == "DOWN")   return sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down);
    if (k == "LEFT")   return sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left);
    if (k == "RIGHT")  return sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right);
    return false;
}

inline void display() {
    sl::check_open();
    sl::win.display();                       // 画好的东西这一刻才出现在屏幕上
}
