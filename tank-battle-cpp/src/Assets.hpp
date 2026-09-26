#pragma once
// 素材加载: 图片/字体/音效, 以及 Scratch 造型元数据(逻辑尺寸、旋转中心、碰撞包围盒)
#include <SFML/Audio.hpp>
#include <SFML/Graphics.hpp>
#include <optional>
#include <string>

// 一个 Scratch “造型”:
//  - texture        已按 2x 舞台分辨率导出的贴图
//  - stageSize      100% 大小时的舞台逻辑尺寸(单位=舞台坐标)
//  - rotationCenter 旋转中心(自造型左上角, 舞台单位) —— 决定精灵锚点
//  - halfExtents    不透明像素紧包围盒的半宽/半高(舞台单位) —— 用于碰撞检测
//  - centerOffset   包围盒中心相对旋转中心的偏移(舞台单位, y 向上约定:
//                   画布 y 向下, 故盒心在旋转中心“下方”时此值为负)。
//                   Scratch 旋转中心常不在内容中心(敌方坦克的旋转中心在炮口),
//                   碰撞盒与边缘反弹都必须按此偏移放置, 否则整体错位约 40 单位。
struct Costume {
    sf::Texture texture;
    sf::Vector2f stageSize;
    sf::Vector2f rotationCenter;
    sf::Vector2f halfExtents;
    sf::Vector2f centerOffset;
};

class Assets {
public:
    bool load(const std::string& dir);

    Costume playerBody, playerTurret;
    Costume enemyTank[2];
    Costume playerExplosion[6];
    Costume enemyExplosion[6];
    Costume missile, bullet;

    sf::Texture background, screenStart, screenGameOver;

    std::optional<sf::Font> font;

    std::optional<sf::SoundBuffer> sndExplosion;
    std::optional<sf::SoundBuffer> sndFirePlayer;
    std::optional<sf::SoundBuffer> sndFireEnemy;
    std::optional<sf::SoundBuffer> sndMusicStart;
    std::optional<sf::SoundBuffer> sndMusicGameOver;

private:
    static bool loadCostume(Costume& c, const std::string& file,
                            sf::Vector2f stageSize, sf::Vector2f rotationCenter);
    static void tightBox(const sf::Texture& tex, sf::Vector2f rotationCenter,
                         sf::Vector2f& halfExtents, sf::Vector2f& centerOffset);
};
