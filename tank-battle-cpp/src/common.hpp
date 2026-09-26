#pragma once
// 与 Scratch 舞台坐标/方向体系对接的换算工具 + 碰撞几何(可独立单测)
// 只依赖 sfml-system; 精灵绘制相关的头文件由 Assets.hpp/Game.hpp 自行引入
#include <SFML/System.hpp>
#include <algorithm>
#include <cmath>
#include <string>

// UTF-8 字面量 -> sf::String(用于中文渲染)
inline sf::String utf8(const std::string& s) {
    return sf::String::fromUtf8(s.begin(), s.end());
}

namespace stage {

constexpr float W = 480.f;              // Scratch 舞台逻辑宽
constexpr float H = 360.f;              // Scratch 舞台逻辑高
constexpr float Left = -W / 2;          // -240
constexpr float Right = W / 2;          //  240
constexpr float Top = H / 2;            //  180
constexpr float Bottom = -H / 2;        // -180
constexpr float Scale = 2.f;            // 逻辑单位 -> 窗口像素(窗口 960x720)
constexpr float Pi = 3.14159265358979f;

// 舞台坐标(原点居中, y 向上) -> 窗口像素(左上原点, y 向下)
inline sf::Vector2f toWindow(sf::Vector2f p) {
    return {(p.x - Left) * Scale, (Top - p.y) * Scale};
}

// 窗口像素 -> 舞台坐标
inline sf::Vector2f toStage(sf::Vector2f p) {
    return {p.x / Scale + Left, Top - p.y / Scale};
}

// Scratch 方向体系: 0=上, 90=右, 顺时针。返回该方向上的单位向量
inline sf::Vector2f dirVector(float dir) {
    float r = dir * Pi / 180.f;
    return {std::sin(r), std::cos(r)};
}

// Scratch “面向 <目标>” 的角度
inline float pointDirection(sf::Vector2f from, sf::Vector2f to) {
    float d = std::atan2(to.x - from.x, to.y - from.y) * 180.f / Pi;
    return d < 0.f ? d + 360.f : d;
}

// Scratch 造型默认朝右(90°方向)。SFML 的 setRotation 顺时针为正,
// 因此方向 dir 对应的精灵旋转角是 dir - 90
inline sf::Angle spriteRotation(float dir) {
    return sf::degrees(dir - 90.f);
}

// ---------------- 碰撞几何 ----------------
// Scratch 的旋转中心常不与造型内容中心重合(例如敌方坦克的旋转中心在炮口),
// 碰撞盒必须按“内容紧包围盒相对旋转中心的偏移”放置, 否则会出现系统性错位。

// 把世界坐标点 p 转入造型的本地坐标系(未旋转姿态=朝右, 原点=旋转中心)
inline sf::Vector2f toLocal(sf::Vector2f anchor, float dir, sf::Vector2f p) {
    sf::Vector2f d = p - anchor;
    float phi = (dir - 90.f) * Pi / 180.f; // 造型被顺时针旋转了 dir-90°
    float cs = std::cos(phi), sn = std::sin(phi);
    return {d.x * cs - d.y * sn, d.x * sn + d.y * cs};
}

// 点(带半宽)对旋转盒的判定: 盒中心在旋转中心加 offset 处(100% 尺寸的舞台单位)
inline bool boxHitTest(sf::Vector2f boxHalf, sf::Vector2f boxOffset, float size,
                       sf::Vector2f anchor, float dir,
                       sf::Vector2f point, sf::Vector2f pointHalf) {
    sf::Vector2f local = toLocal(anchor, dir, point) - boxOffset * size;
    sf::Vector2f half = boxHalf * size;
    return std::abs(local.x) <= half.x + pointHalf.x &&
           std::abs(local.y) <= half.y + pointHalf.y;
}

// 内容盒旋转后的世界 AABB(四角投影), 用于“碰到边缘就反弹”的位置钳制
struct WorldAABB {
    sf::Vector2f min, max;
    bool overlapsStageEdge() const {
        return min.x < Left || max.x > Right || min.y < Bottom || max.y > Top;
    }
};
inline WorldAABB rotatedBoxAABB(sf::Vector2f boxHalf, sf::Vector2f boxOffset,
                                float size, sf::Vector2f anchor, float dir) {
    float phi = -(dir - 90.f) * Pi / 180.f; // 本地 -> 世界: 反向旋转
    float cs = std::cos(phi), sn = std::sin(phi);
    sf::Vector2f c = boxOffset * size, h = boxHalf * size;
    const sf::Vector2f corners[4] = {{c.x - h.x, c.y - h.y}, {c.x + h.x, c.y - h.y},
                                     {c.x - h.x, c.y + h.y}, {c.x + h.x, c.y + h.y}};
    WorldAABB box{{1e9f, 1e9f}, {-1e9f, -1e9f}};
    for (const sf::Vector2f& u : corners) {
        sf::Vector2f w{u.x * cs - u.y * sn, u.x * sn + u.y * cs};
        box.min = {std::min(box.min.x, anchor.x + w.x), std::min(box.min.y, anchor.y + w.y)};
        box.max = {std::max(box.max.x, anchor.x + w.x), std::max(box.max.y, anchor.y + w.y)};
    }
    return box;
}

// Scratch “碰到边缘就反弹”: 仅当正朝着越界的那条边运动时才镜像方向
// (已背离则保持方向、只把位置拉回)。敌人出生在 y=150 且车体向上伸出
// 超过顶边, 若无条件反射, 出生帧就会把朝下的方向反转、永远困在顶部。
// 顺序还原 scratch-vm: 越界判定用翻转前的盒, setDirection(新方向)之后
// keepInFence 的位置钳制用翻转后的盒——81 长的车体翻转瞬间盒的跨度整体
// 换向, 沿用旧盒钳制会让车体在墙外悬空 1~2 帧才自愈。
// 就地钳制 pos 并返回新方向。
inline float bounceOffEdges(float dir, sf::Vector2f& pos,
                            sf::Vector2f boxHalf, sf::Vector2f boxOffset, float size) {
    const WorldAABB pre = rotatedBoxAABB(boxHalf, boxOffset, size, pos, dir);
    const sf::Vector2f v = dirVector(dir);
    if (pre.min.x < Left && v.x < 0.f) dir = -dir;          // 朝左撞左墙
    if (pre.max.x > Right && v.x > 0.f) dir = -dir;         // 朝右撞右墙
    if (pre.min.y < Bottom && v.y < 0.f) dir = 180.f - dir; // 朝下撞底边
    if (pre.max.y > Top && v.y > 0.f) dir = 180.f - dir;    // 朝上撞顶边
    const WorldAABB post = rotatedBoxAABB(boxHalf, boxOffset, size, pos, dir);
    pos.x += std::max(0.f, Left - post.min.x) - std::max(0.f, post.max.x - Right);
    pos.y += std::max(0.f, Bottom - post.min.y) - std::max(0.f, post.max.y - Top);
    dir = std::fmod(std::fmod(dir, 360.f) + 360.f, 360.f);
    return dir;
}

} // namespace stage
