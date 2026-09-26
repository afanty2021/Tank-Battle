#include "Assets.hpp"
#include "common.hpp"

#include <iostream>

// 造型元数据来自 Scratch 工程 project.json / 导出清单 assets/manifest.json
bool Assets::loadCostume(Costume& c, const std::string& file,
                         sf::Vector2f stageSize, sf::Vector2f rotationCenter) {
    sf::Texture tex;
    if (!tex.loadFromFile(file)) {
        std::cerr << "[assets] 无法加载图片: " << file << '\n';
        return false;
    }
    c.texture = std::move(tex);
    c.texture.setSmooth(true);
    c.stageSize = stageSize;
    c.rotationCenter = rotationCenter;
    tightBox(c.texture, rotationCenter, c.halfExtents, c.centerOffset);
    return true;
}

// 扫描 alpha 通道, 求不透明像素的紧包围盒, 换算回舞台单位:
// halfExtents=半宽半高; centerOffset=包围盒中心相对旋转中心的偏移
// (舞台 y 向上约定: 画布 y 向下, 盒心在旋转中心“下方”时 y 为负)。
// Scratch 的“碰到”按实际绘制像素判定, 用紧包围盒比整张贴图更接近原版手感。
void Assets::tightBox(const sf::Texture& tex, sf::Vector2f rotationCenter,
                      sf::Vector2f& halfExtents, sf::Vector2f& centerOffset) {
    sf::Image img = tex.copyToImage();
    unsigned x0 = img.getSize().x, y0 = img.getSize().y, x1 = 0, y1 = 0;
    for (unsigned y = 0; y < img.getSize().y; ++y)
        for (unsigned x = 0; x < img.getSize().x; ++x)
            if (img.getPixel({x, y}).a > 40) {
                x0 = std::min(x0, x); y0 = std::min(y0, y);
                x1 = std::max(x1, x); y1 = std::max(y1, y);
            }
    if (x1 < x0) { // 全透明
        halfExtents = {0.f, 0.f};
        centerOffset = {0.f, 0.f};
        return;
    }
    float scaleX = 1.f / stage::Scale, scaleY = 1.f / stage::Scale;
    halfExtents = {static_cast<float>(x1 - x0 + 1) / 2.f * scaleX,
                   static_cast<float>(y1 - y0 + 1) / 2.f * scaleY};
    sf::Vector2f boxCenter{static_cast<float>(x0 + x1 + 1) / 2.f * scaleX,
                           static_cast<float>(y0 + y1 + 1) / 2.f * scaleY};
    // 画布 y 向下 -> 舞台 y 向上: y 取负, 与 toLocal/rotatedBoxAABB 的本地系一致
    centerOffset = {boxCenter.x - rotationCenter.x, rotationCenter.y - boxCenter.y};
}

bool Assets::load(const std::string& dir) {
    const std::string img = dir + "/images/", snd = dir + "/sounds/";

    //                        文件                     逻辑尺寸        旋转中心(舞台单位)
    if (!loadCostume(playerBody,     img + "player_body.png",    {203, 106},           {112.15f, 54.04f}))    return false;
    if (!loadCostume(playerTurret,   img + "player_turret.png",  {225.5f, 92.5f},      {63.54f, 45.08f}))     return false;
    if (!loadCostume(enemyTank[0],   img + "enemy_tank_1.png",   {272.738f, 106.613f}, {270.37f, 53.06f}))    return false;
    if (!loadCostume(enemyTank[1],   img + "enemy_tank_2.png",   {272.755f, 106.658f}, {271.88f, 52.08f}))    return false;
    if (!loadCostume(missile,        img + "missile.png",        {55, 18},             {28, 9}))              return false;
    if (!loadCostume(bullet,         img + "bullet.png",         {61.886f, 8.457f},    {32.05f, 4.53f}))      return false;

    const sf::Vector2f playerExpl[6][2] = {
        {{80, 120}, {40, 60}}, {{90, 120}, {45, 60}}, {{90, 120}, {45, 60}},
        {{83, 120}, {41.5f, 60}}, {{82, 120}, {41, 60}}, {{71, 120}, {35.5f, 60}}};
    const sf::Vector2f enemyExpl[6][2] = {
        {{80, 72}, {84, 37}}, {{90, 96}, {94, 50}}, {{90, 94}, {90, 47}},
        {{81, 89}, {83.5f, 44}}, {{82, 107}, {87, 46}}, {{71, 111}, {73.5f, 54}}};
    for (int i = 0; i < 6; ++i) {
        if (!loadCostume(playerExplosion[i],
                         img + "explosion_player_" + std::to_string(i + 1) + ".png",
                         playerExpl[i][0], playerExpl[i][1])) return false;
        if (!loadCostume(enemyExplosion[i],
                         img + "explosion_enemy_" + std::to_string(i + 1) + ".png",
                         enemyExpl[i][0], enemyExpl[i][1])) return false;
    }

    auto loadTex = [](sf::Texture& t, const std::string& f) {
        sf::Texture tex;
        if (!tex.loadFromFile(f)) {
            std::cerr << "[assets] 无法加载图片: " << f << '\n';
            return false;
        }
        t = std::move(tex);
        return true;
    };
    if (!loadTex(background,      img + "background.jpg"))      return false;
    if (!loadTex(screenStart,     img + "screen_start.jpg"))    return false;
    if (!loadTex(screenGameOver,  img + "screen_gameover.jpg")) return false;

    auto loadSnd = [](std::optional<sf::SoundBuffer>& dst, const std::string& f) {
        sf::SoundBuffer buf;
        if (!buf.loadFromFile(f)) {
            std::cerr << "[assets] 无法加载音效: " << f << '\n';
            return false;
        }
        dst = std::move(buf);
        return true;
    };
    if (!loadSnd(sndExplosion,     snd + "explosion.ogg"))      return false;
    if (!loadSnd(sndFirePlayer,    snd + "fire_player.ogg"))    return false;
    if (!loadSnd(sndFireEnemy,     snd + "fire_enemy.ogg"))     return false;
    if (!loadSnd(sndMusicStart,    snd + "music_start.ogg"))    return false;
    if (!loadSnd(sndMusicGameOver, snd + "music_gameover.ogg")) return false;

    // 中文字体: 优先系统黑体, 用于“分数”和操作提示
    const std::string fontPaths[] = {"C:/Windows/Fonts/simhei.ttf",
                                     "C:/Windows/Fonts/msyh.ttc",
                                     dir + "/fonts/simhei.ttf"};
    for (const auto& path : fontPaths) {
        sf::Font f;
        if (f.openFromFile(path)) {
            font = std::move(f);
            break;
        }
    }
    if (!font) std::cerr << "[assets] 警告: 未找到中文字体, 分数/提示文字将无法显示\n";
    return true;
}
