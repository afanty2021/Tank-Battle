// ============================================================
//  第 8 课 · 毕业作品: 把它改成你自己的游戏
//
//  下面是一个能完整玩的小游戏(第 7 课 + 炮塔瞄准鼠标)。
//  今天你的任务不是从头写, 而是【改造】——挑下面的改造单做,
//  做完给同学玩, 收集吐槽, 再改一版。最后一节课全班展示。
//
//  ---------------------- 改造单(由易到难) ----------------------
//  A. 手感调校: 速度/冷却/生命/命中加速, 每项只改一个数。
//  B. 限时模式: 加 int frames = 0; 每帧加一, 用 draw_text 显示
//     "时间: " + std::to_string(60 - frames / 30);  时间到直接 state = 2。
//  C. 双倍敌人: 再加一对 ex2, ey2(和 ex, ey 一样的代码), 命中判定
//     也查它——记得复制的是"逻辑", 变量名要改成 2 号。
//  D. 敌人开炮: 加一个子弹数组 bx, by, 敌人每 90 帧发一颗往下掉,
//     touching(bx[i], by[i], x, y) 时 miss = miss + 3(直接重伤)。
//  E. 无敌护盾: 按住 S 键时敌人子弹碰不到你(但也不能开炮)。
//  F. 换皮: 用画图改 assets\images 里的图(先备份! 见练习册第 9 题)。
//  G. 自由题: 写在下面"我的改造"注释里, 展示时讲给全班听。
//
//  我的改造: ____________
//  同学吐槽 1: ____________  2: ____________  3: ____________
//
//  编译: 双击 lessons\compile.bat L08 , 然后双击 L08.exe
// ============================================================
#include "scratch_like.hpp"
#include <string>
#include <vector>

int main() {
    open_window();

    int state = 0;                    // 0=开场 1=游戏 2=结束

    float x = 0, y = -150;
    std::vector<float> mx, my;      // 导弹的 x / y 账本
    std::vector<float> md;          // 导弹的方向账本(第 8 课新增:
                                    // 每颗导弹记住自己朝哪飞)
    int fire_wait = 0;
    float ex = 0, ey = 170, speed = 2;
    int score = 0, miss = 0;

    while (window_open()) {
        if (state == 0) {
            draw_start_screen();
            draw_text("按空格开始", 0, -60);
            if (key_pressed("SPACE")) {
                state = 1;
                play_music("start");
            }
        } else if (state == 1) {
            // ---- 驾驶: 左右移动 ----
            if (key_pressed("A")) x = x - 5;
            if (key_pressed("D")) x = x + 5;
            if (x > 230)  x = 230;
            if (x < -230) x = -230;

            // ---- 炮塔永远指向鼠标(正式版的核心操作!) ----
            float aim = face_direction(x, y, mouse_x(), mouse_y());
            // ↑ 【面向鼠标】: (x,y) 是炮塔自己, (mouse_x(), mouse_y()) 是目标

            // ---- 发射: 导弹沿炮塔方向飞 ----
            if (fire_wait > 0) fire_wait = fire_wait - 1;
            if (key_pressed("SPACE") && fire_wait == 0) {
                mx.push_back(x);
                my.push_back(y + 20);
                md.push_back(aim);          // 记下这颗导弹的朝向
                fire_wait = 10;
                play_sound("fire");
            }

            // ---- 敌人 ----
            ey = ey - speed;
            if (ey < -190) {
                miss = miss + 1;
                ex = random_int(-200, 200);
                ey = 170;
            }

            // ---- 导弹: 沿各自方向飞(【移动 10 步】拆成横竖两个分量) ----
            for (int i = (int)mx.size() - 1; i >= 0; i--) {
                mx[i] = mx[i] + step_x(md[i], 10);
                my[i] = my[i] + step_y(md[i], 10);
                if (touching(mx[i], my[i], ex, ey)) {
                    score = score + 1;
                    play_sound("boom");
                    ex = random_int(-200, 200);
                    ey = 170;
                    speed = speed + 0.5f;
                    mx.erase(mx.begin() + i);
                    my.erase(my.begin() + i);
                    md.erase(md.begin() + i);
                } else if (my[i] > 200 || my[i] < -200 ||
                           mx[i] > 250 || mx[i] < -250) {  // 四个方向都可能出屏
                    mx.erase(mx.begin() + i);
                    my.erase(my.begin() + i);
                    md.erase(md.begin() + i);
                }
            }

            // ---- 画 ----
            clear();
            draw_background();
            draw_enemy(ex, ey, 180);
            draw_tank(x, y, 0);             // 车身朝上
            draw_turret(x, y, aim);         // 炮塔单独画, 朝鼠标
            for (int i = 0; i < (int)mx.size(); i++)
                draw_missile(mx[i], my[i], md[i]);
            draw_text("分数: " + std::to_string(score), -100, 155);
            draw_text("生命: " + std::to_string(3 - miss), 100, 155);

            if (miss >= 3) {
                state = 2;
                play_music("over");
            }
        } else {
            draw_gameover_screen();
            draw_text("分数: " + std::to_string(score) + "  按空格再来一局", 0, -60);
            if (key_pressed("SPACE")) {
                x = 0; y = -150;
                mx.clear();  my.clear();  md.clear();
                fire_wait = 0;
                ex = 0; ey = 170; speed = 2;
                score = 0; miss = 0;
                state = 0;
            }
        }
        display();
    }
    return 0;
}
