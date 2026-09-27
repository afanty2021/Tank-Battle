// 单元测试: 舞台坐标换算 / 方向体系 / 碰撞几何(含偏心包围盒回归)。
// 纯逻辑测试, 不依赖游戏状态; 由 build.bat 编译运行, 也可手动:
//   g++ -std=c++20 tests/unit_tests.cpp -o unit_tests.exe -lsfml-system
#include "../src/common.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

static int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static bool near(float a, float b, float eps = 0.001f) {
    return std::abs(a - b) <= eps;
}

using namespace stage;

static void testConversions() {
    CHECK(toWindow({-240.f, 180.f}) == sf::Vector2f(0.f, 0.f));    // 左上角
    CHECK(toWindow({240.f, -180.f}) == sf::Vector2f(960.f, 720.f)); // 右下角
    CHECK(toWindow({0.f, 0.f}) == sf::Vector2f(480.f, 360.f));      // 舞台中心
    CHECK(toStage({0.f, 0.f}) == sf::Vector2f(-240.f, 180.f));
    CHECK(toStage({480.f, 360.f}) == sf::Vector2f(0.f, 0.f));
    sf::Vector2f p{37.5f, -122.25f};
    CHECK(toStage(toWindow(p)) == p); // 往返一致
}

static void testDirectionSystem() {
    // Scratch 方向: 0=上 90=右 180=下 270=左
    CHECK(near(dirVector(0.f).x, 0.f) && near(dirVector(0.f).y, 1.f));
    CHECK(near(dirVector(90.f).x, 1.f) && near(dirVector(90.f).y, 0.f));
    CHECK(near(dirVector(180.f).x, 0.f) && near(dirVector(180.f).y, -1.f));
    CHECK(near(dirVector(270.f).x, -1.f) && near(dirVector(270.f).y, 0.f));

    CHECK(near(pointDirection({0.f, 0.f}, {0.f, 5.f}), 0.f));
    CHECK(near(pointDirection({0.f, 0.f}, {5.f, 0.f}), 90.f));
    CHECK(near(pointDirection({0.f, 0.f}, {0.f, -5.f}), 180.f));
    CHECK(near(pointDirection({0.f, 0.f}, {-5.f, 0.f}), 270.f));

    CHECK(near(spriteRotation(90.f).asDegrees(), 0.f));   // 朝右 = 造型原样
    CHECK(near(spriteRotation(0.f).asDegrees(), -90.f));  // 朝上 = 逆时针 90°
}

// 复现敌方坦克1的真实参数: 造型 272.7x106.6, 旋转中心 (270.37, 53.06)
// 在炮口处, 画布上内容盒中心相对旋转中心偏 (-134.1, +0.4)(y 向下);
// 舞台 y 向上约定取负 y -> (-134.1, -0.4)。30% 尺寸。
static const sf::Vector2f kEnemyHalf{136.2f, 53.2f};
static const sf::Vector2f kEnemyOffset{-134.1f, -0.4f};
static constexpr float kEnemySize = 0.30f;

static void testBoxHitTest() {
    const sf::Vector2f zero{0.f, 0.f};
    // 内容真实中心处(旋转中心左侧约 40 单位)必须命中 —— 修复前这里测不中
    CHECK(boxHitTest(kEnemyHalf, kEnemyOffset, kEnemySize, zero, 90.f,
                     {-40.2f, -0.1f}, zero));
    // 车体最左缘(local x = -270.37*0.3 = -81.1, 半宽 40.86)
    CHECK(boxHitTest(kEnemyHalf, kEnemyOffset, kEnemySize, zero, 90.f,
                     {-80.f, 0.f}, zero));
    CHECK(!boxHitTest(kEnemyHalf, kEnemyOffset, kEnemySize, zero, 90.f,
                      {-83.f, 0.f}, zero)); // 再往左 2 单位就出界
    // 旋转中心右侧是空的(炮口只有 2.4 单位内容): 旧代码在此处误判命中 —— C1 回归
    CHECK(!boxHitTest(kEnemyHalf, kEnemyOffset, kEnemySize, zero, 90.f,
                      {35.f, 0.f}, zero));
    CHECK(!boxHitTest(kEnemyHalf, kEnemyOffset, kEnemySize, zero, 90.f,
                      {41.f, 0.f}, zero));
    // 旋转中心本身仍在车体最右缘内
    CHECK(boxHitTest(kEnemyHalf, kEnemyOffset, kEnemySize, zero, 90.f,
                     {0.f, 0.f}, zero));

    // 朝上(dir=0): local -x 映射到世界 -y, 命中点应转到锚点下方
    CHECK(boxHitTest(kEnemyHalf, kEnemyOffset, kEnemySize, zero, 0.f,
                     {0.f, -40.f}, zero));
    CHECK(!boxHitTest(kEnemyHalf, kEnemyOffset, kEnemySize, zero, 0.f,
                      {0.f, 35.f}, zero));
    CHECK(!boxHitTest(kEnemyHalf, kEnemyOffset, kEnemySize, zero, 0.f,
                      {-40.f, 0.f}, zero)); // 横向在盒外(半宽仅 16)

    // 带点半径的点(导弹): 扩张判定
    CHECK(boxHitTest(kEnemyHalf, kEnemyOffset, kEnemySize, zero, 90.f,
                     {-85.f, 0.f}, {6.f, 3.f}));
}

static void testRotatedAABB() {
    // 用合成精确值验证几何本身(不耦合真实贴图的像素取整误差):
    // half=(40,16), offset=(-30,2)(舞台约定: 盒心在锚点后方 30、左侧 2), size=1
    const sf::Vector2f h{40.f, 16.f}, o{-30.f, 2.f};
    // dir=90(朝右, 不旋转): x ∈ [-70,10], y ∈ [-14,18]
    WorldAABB b = rotatedBoxAABB(h, o, 1.f, {0.f, 0.f}, 90.f);
    CHECK(near(b.min.x, -70.f) && near(b.max.x, 10.f));
    CHECK(near(b.min.y, -14.f) && near(b.max.y, 18.f));
    CHECK(!b.overlapsStageEdge()); // 舞台中心的敌人不出界

    // dir=0(朝上): 朝前(+x)转到 +y; local +y(运动左侧)转到世界 -x,
    // 故 y∈[-14,18] -> x∈[-18,14], x∈[-70,10] -> y∈[-70,10]
    WorldAABB u = rotatedBoxAABB(h, o, 1.f, {0.f, 0.f}, 0.f);
    CHECK(near(u.min.x, -18.f) && near(u.max.x, 14.f));
    CHECK(near(u.min.y, -70.f) && near(u.max.y, 10.f));

    // 左墙反弹边界: 锚点 x=-240+70 时盒左缘恰好贴墙(-240.0 出界, -239.9 不出界)
    CHECK(rotatedBoxAABB(h, o, 1.f, {-240.f + 69.9f, 0.f}, 90.f).overlapsStageEdge());
    CHECK(!rotatedBoxAABB(h, o, 1.f, {-240.f + 70.1f, 0.f}, 90.f).overlapsStageEdge());

    // 敌方坦克真实参数(30%): 锚点在原点时盒左缘约 -81, 验证偏移确实生效
    WorldAABB e = rotatedBoxAABB(kEnemyHalf, kEnemyOffset, kEnemySize, {0.f, 0.f}, 90.f);
    CHECK(near(e.min.x, -81.f, 0.5f));
    CHECK(e.max.x < 1.f); // 旋转中心右侧几乎为空(炮口仅 ~0.7 单位)
    CHECK(e.min.x < -75.f); // 回归: 若忽略 offset, min.x 只会到 -40.9

    // 竖直偏移符号约定(N2): 画布上盒心在旋转中心“下方”的造型(画布 y 向下
    // 为正), 舞台约定 offset.y 为负; dir=90 原姿态下盒必须落在锚点下方
    WorldAABB vb = rotatedBoxAABB({4.f, 2.f}, {0.f, -8.f}, 1.f, {0.f, 0.f}, 90.f);
    CHECK(near(vb.min.y, -10.f) && near(vb.max.y, -6.f));
    CHECK(boxHitTest({4.f, 2.f}, {0.f, -8.f}, 1.f, {0.f, 0.f}, 90.f,
                     {0.f, -8.f}, {0.f, 0.f}));
    CHECK(!boxHitTest({4.f, 2.f}, {0.f, -8.f}, 1.f, {0.f, 0.f}, 90.f,
                      {0.f, 8.f}, {0.f, 0.f}));
}

static void testBounceBehaviour() {
    // 回归(第二轮复查 Critical): 敌人出生在 y=150, 车体向上伸出顶边,
    // 若反弹不判断运动朝向, 出生帧就被反向并永远困在顶部条带(y≥99)。
    // 行为级测试: 出生方向 130~240(向下), 每帧走 2 步, 120 帧后必须
    // 已离开顶部条带继续下行; 正下方向应已下穿中场
    for (float d0 : {130.f, 180.f, 200.f, 240.f}) {
        sf::Vector2f pos{0.f, 150.f};
        float dir = d0;
        for (int t = 0; t < 120; ++t) {
            pos += dirVector(dir) * 2.f;
            dir = bounceOffEdges(dir, pos, kEnemyHalf, kEnemyOffset, kEnemySize);
        }
        CHECK(pos.y < 100.f); // 无守卫时所有方向都困在 y∈[99,180]
    }

    // 语义: 盒子超出顶边但正向下行(背离) -> 方向保持, 仅位置被拉回
    {
        sf::Vector2f pos{0.f, 150.f}; // dir=180 时车体伸出顶边约 51 单位
        float dir = bounceOffEdges(180.f, pos, kEnemyHalf, kEnemyOffset, kEnemySize);
        CHECK(near(dir, 180.f)); // 不得反转
        CHECK(pos.y < 100.f);    // 被拉回到合法位置(车体挂上边缘之下)
    }
    // 语义+回归(第三轮复查 N1): 盒子超出顶边且正向上行才镜像; 且钳制必须
    // 用翻转后的盒——dir=0 撞顶翻转成 180 后车体改挂锚点上方伸出 ~81,
    // 若按翻转前的盒钳制, 锚点会停在 ~179.3, 车体整帧悬在墙外 ~80 单位
    {
        sf::Vector2f pos{0.f, 180.f}; // dir=0 时炮口是全车最高点
        float dir = bounceOffEdges(0.f, pos, kEnemyHalf, kEnemyOffset, kEnemySize);
        CHECK(near(dir, 180.f)); // 0°(上) 撞顶 -> 180°(下)
        CHECK(pos.y < 100.f);    // 按翻转后盒钳制 -> 锚点拉到 ~99
        WorldAABB post = rotatedBoxAABB(kEnemyHalf, kEnemyOffset, kEnemySize,
                                        pos, dir);
        CHECK(!post.overlapsStageEdge());    // 反弹当帧盒子就完全回到场内
        CHECK(near(post.max.y, Top, 0.01f)); // 且同帧贴着顶边
    }
}

// 语音命令协议(回归敏感点: 前缀匹配 + strtol 度数语义 + ±360 累加钳制)。
// 行为基线来自旧版 handleVoiceCommand 内联实现(atoi 语义: "abc"->0, "45x"->45)
static void testVoiceCommand() {
    using K = voice::Command::Kind;
    CHECK(voice::parse("FIRE").kind == K::Fire);
    CHECK(voice::parse("STOP").kind == K::Stop);
    CHECK(voice::parse("MOVE_UP").kind == K::MoveUp);
    CHECK(voice::parse("MOVE_DOWN").kind == K::MoveDown);
    CHECK(voice::parse("MOVE_LEFT").kind == K::MoveLeft);
    CHECK(voice::parse("MOVE_RIGHT").kind == K::MoveRight);

    CHECK(voice::parse("TURRET_CW 45").kind == K::TurretCW);    // 无换行符也认
    CHECK(voice::parse("TURRET_CW 45").degrees == 45);
    CHECK(voice::parse("TURRET_CCW 90").kind == K::TurretCCW);
    CHECK(voice::parse("TURRET_CCW 90").degrees == 90);
    CHECK(voice::parse("TURRET_CW  30").degrees == 30);         // 双空格: strtol 跳过空白
    CHECK(voice::parse("TURRET_CW -700").degrees == -700);
    CHECK(voice::parse("TURRET_CW abc").degrees == 0);          // 非数字 -> 0
    CHECK(voice::parse("TURRET_CW 45x").degrees == 45);         // 数字前缀
    CHECK(voice::parse("TURRET_CW 99999").degrees == 99999);    // 原始度数不在这层钳
    CHECK(voice::parse("TURRET_CW").kind == K::None);           // 缺空格/度数
    CHECK(voice::parse("TURRET_CW ").kind == K::None);          // 只有前缀(空串度数)
    CHECK(voice::parse("turret_cw 45").kind == K::None);        // 大小写敏感
    CHECK(voice::parse("").kind == K::None);
    CHECK(voice::parse("FIREX").kind == K::None);
    CHECK(voice::parse("\xE5\xBC\x80\xE7\x82\xAE").kind == K::None); // 中文原文不走本协议
    // 超长输入(UDP 缓冲 64 字节上限): strtol 溢出被钳成确定值, 不再是 atoi 的 UB
    CHECK(near(voice::applyTurret(0.f,
        voice::parse("TURRET_CW " + std::string(50, '9'))), 360.f));

    CHECK(near(voice::applyTurret(0.f, voice::parse("TURRET_CW 45")), 45.f));
    CHECK(near(voice::applyTurret(0.f, voice::parse("TURRET_CCW 90")), -90.f));
    CHECK(near(voice::applyTurret(0.f, voice::parse("TURRET_CW abc")), 0.f));
    CHECK(near(voice::applyTurret(0.f, voice::parse("TURRET_CW 99999")), 360.f));
    CHECK(near(voice::applyTurret(-350.f, voice::parse("TURRET_CCW 30")), -360.f));
    CHECK(near(voice::applyTurret(300.f, voice::parse("TURRET_CW 300")), 360.f)); // 连发口令封顶一圈
    CHECK(near(voice::applyTurret(300.f, voice::parse("TURRET_CCW 300")), 0.f));  // 反向冲销
}

// ---------------- 联机协议(Protocol.hpp) ----------------
#include "../src/Protocol.hpp"

static void testProtoQuantize() {
    // 角度: 折叠到 [0,360) 后 ×16, 往返误差 <= 1/16 度
    for (float a : {-90.f, 0.f, 359.99f, 360.f, 720.f, -0.01f}) {
        const float back = proto::decAngle(proto::encAngle(a));
        CHECK(back >= 0.f && back < 360.f);
        float folded = std::fmod(std::fmod(a, 360.f) + 360.f, 360.f);
        if (folded >= 360.f - 1.f / 16.f) folded = 0.f; // encAngle 四舍五入可到 360*16->取模归 0
        CHECK(std::abs(back - folded) <= 1.f / 16.f + 1e-4f);
    }
    CHECK(proto::encAngle(-90.f) == proto::encAngle(270.f)); // 负角折叠
    CHECK(proto::encAngle(359.99f) < 5760);                  // 不越 360*16
    // 坐标: 往返误差 <= 1/16 舞台单位
    for (float v : {-240.f, -211.f, 0.f, 137.5f, 240.f})
        CHECK(std::abs(proto::decPos(proto::encPos(v)) - v) <= 1.f / 16.f + 1e-4f);
}

static void testProtoRoundTrip() {
    const proto::HostMsg h{"room-01"};
    auto eh = proto::encodeHost(h.name);
    auto dh = proto::decodeHost(eh.data(), eh.size());
    CHECK(dh && dh->name == "room-01");

    const proto::ErrMsg e{proto::kErrBusy, 7};
    auto ee = proto::encodeErr(e.reason, e.peerVersion);
    auto de = proto::decodeErr(ee.data(), ee.size());
    CHECK(de && de->reason == proto::kErrBusy && de->peerVersion == 7);

    const proto::JoinMsg j{"player-2", true};
    auto ej = proto::encodeJoin(j.name, j.ready);
    auto dj = proto::decodeJoin(ej.data(), ej.size());
    CHECK(dj && dj->name == "player-2" && dj->ready);

    const InputState in{0x0B, 123.4f, true};
    auto ei = proto::encodeInp(in);
    auto di = proto::decodeInp(ei.data(), ei.size());
    CHECK(di && di->input.moveBits == 0x0B && di->input.fire &&
          near(di->input.aim, 123.4f, 1.f / 16.f + 1e-3f));

    proto::SnapMsg s{};
    s.tick = 4000000000u; s.phase = proto::Phase::Battle;
    s.hp[0] = 2; s.hp[1] = 3;
    s.tanks[1] = {10.5f, -20.25f, 90.f, 45.f, 1, 3};
    s.missiles.push_back({5.f, 5.f, 0.f, 0});
    s.missiles.push_back({-5.f, -5.f, 180.f, 1});
    s.events.fire[1] = s.events.hit[0] = s.events.die[0] = true;
    auto es = proto::encodeSnap(s);
    auto ds = proto::decodeSnap(es.data(), es.size());
    CHECK(ds && ds->tick == 4000000000u && ds->phase == proto::Phase::Battle);
    CHECK(ds && ds->tanks[1].animFrame == 3 && ds->tanks[1].state == 1);
    CHECK(ds && ds->missiles.size() == 2 && ds->missiles[1].owner == 1);
    CHECK(ds && ds->events.die[0] && !ds->events.fire[0]);
}

static void testProtoBadPackets() {
    // 截断: 每种消息砍掉最后一个字节都必须失败
    auto eh = proto::encodeHost("ab");
    CHECK(!proto::decodeHost(eh.data(), eh.size() - 1));
    auto ej = proto::encodeJoin("ab", false);
    CHECK(!proto::decodeJoin(ej.data(), ej.size() - 1));
    auto ei = proto::encodeInp(InputState{});
    CHECK(!proto::decodeInp(ei.data(), ei.size() - 1));
    proto::SnapMsg s{}; s.phase = proto::Phase::Countdown; s.hp[0] = s.hp[1] = 3;
    auto es = proto::encodeSnap(s);
    CHECK(!proto::decodeSnap(es.data(), es.size() - 1));
    // 首字节 msgId 不匹配
    CHECK(!proto::decodeHost(es.data(), es.size()));
    // 名字超长(31B 上限): encode 返回空包, decode 也必须拒绝
    const std::string longName(40, 'x');
    auto elong = proto::encodeJoin(longName, false);
    CHECK(elong.empty() &&
          !proto::decodeJoin(elong.data(), elong.size()));
    // SNAP 非法枚举/越界值 -> 整包 nullopt(不崩溃)
    // 字节偏移: [0]id [1]ver [2..5]tick [6]phase [7]hp0 [8]hp1
    //           [9..18]tank0(x2y2dir2turret2state1frame1) [19..28]tank1 ...
    auto mutate = [&](int patchIdx, std::uint8_t v) {
        auto b = proto::encodeSnap(s);
        b[patchIdx] = v;
        return proto::decodeSnap(b.data(), b.size()).has_value();
    };
    CHECK(!mutate(6, 4));   // phase=4
    CHECK(!mutate(7, 4));   // hp0=4
    CHECK(!mutate(17, 3));  // tank0.state=3
    CHECK(!mutate(27, 3));  // tank1.state=3
    CHECK(mutate(18, 6));   // state==0 时 animFrame 不校验(仅 state==1 校验)
    // state==1 且 animFrame>5(客户端拿它当 6 帧数组下标)
    proto::SnapMsg boom = s;
    boom.tanks[0].state = 1; boom.tanks[0].animFrame = 6;
    CHECK(!proto::decodeSnap(proto::encodeSnap(boom).data(),
                             proto::encodeSnap(boom).size()));
    boom.tanks[0].animFrame = 5; // 边界值 5 合法
    CHECK(proto::decodeSnap(proto::encodeSnap(boom).data(),
                            proto::encodeSnap(boom).size()));
    // owner>1 / missileCount>64
    proto::SnapMsg many = s; many.missiles.resize(65, {0.f, 0.f, 0.f, 0});
    CHECK(!proto::decodeSnap(proto::encodeSnap(many).data(),
                             proto::encodeSnap(many).size()));
    proto::SnapMsg own = s; own.missiles.push_back({0.f, 0.f, 0.f, 2});
    CHECK(!proto::decodeSnap(proto::encodeSnap(own).data(),
                             proto::encodeSnap(own).size()));
    // ERR reason 只定义 1/2, 3=坏包
    auto ebad = proto::encodeErr(3, 1);
    CHECK(!proto::decodeErr(ebad.data(), ebad.size()));
}

static void testSnapAcceptRule() {
    using A = proto::Accept;
    CHECK(proto::acceptVerdict(101, 100) == A::Apply);
    CHECK(proto::acceptVerdict(100, 100) == A::Skip);   // 重复包: 事件不重放
    CHECK(proto::acceptVerdict(99, 100) == A::Discard); // 旧包
    // (暂停可见性 N1 的端到端验证在 Task 4 冒烟第 7 项, 纯函数层无增量可测)
}

int main() {
    testConversions();
    testDirectionSystem();
    testBoxHitTest();
    testRotatedAABB();
    testBounceBehaviour();
    testVoiceCommand();
    testProtoQuantize();
    testProtoRoundTrip();
    testProtoBadPackets();
    testSnapAcceptRule();
    if (failures == 0)
        std::printf("unit_tests: all passed\n");
    else
        std::printf("unit_tests: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
