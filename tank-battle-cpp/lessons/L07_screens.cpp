// ============================================================
//  第 7 课 · 有开场、有结局、有声音——像一个真游戏
//
//  到现在为止程序只有"玩"一种状态。真正的游戏至少有三种:
//    开场画面 → 游戏中 → 结束画面
//  Scratch 里你用【广播"开始游戏"】切换场景; C++ 里最简单的
//  办法是一个"状态变量":
//
//    int state = 0;    // 0=开场  1=游戏中  2=结束
//    if (state == 0) { ... } else if (state == 1) { ... } else { ... }
//
//  新积木: play_sound("fire"/"boom")  play_music("start"/"over")
//          draw_start_screen()  draw_gameover_screen()
//
//  规则: 敌人漏到底 3 次 → 游戏结束。
//  编译: 双击 lessons\compile.bat 输入课号 7 回车, 然后双击 L07.exe
// ============================================================
#include "scratch_like.hpp"
#include <string>
#include <vector>

int main() {
    open_window();

    int state = 0;                    // 当前状态: 0 开场 / 1 游戏 / 2 结束

    float x = 0, y = -150;            // --- 游戏中的数据 ---
    std::vector<float> mx, my;
    int fire_wait = 0;
    float ex = 0, ey = 170, speed = 2;
    int score = 0;
    int miss = 0;                     // 漏掉几个敌人

    while (window_open()) {
        if (state == 0) {
            // ============ 开场画面 ============
            // 开场图铺满整个屏幕, 上一帧的东西全被盖住, 所以这两个画面
            // 状态可以不 clear()(第 5 课的铁律针对会露底色的普通画面;
            // 全屏大图盖住一切时是例外)
            draw_start_screen();               // 原版的开场图!
            draw_text("按空格开始", 0, -60);
            if (key_pressed("SPACE")) {
                state = 1;                     // 切状态 = 发广播
                play_music("start");           // 开场曲(正式版同款)
            }
        } else if (state == 1) {
            // ============ 游戏中(第 6 课 + 声音) ============
            if (key_pressed("A")) x = x - 5;
            if (key_pressed("D")) x = x + 5;
            if (x > 205)  x = 205;    // 活动范围与第 2 课/正式版一致
            if (x < -211) x = -211;    // (两边不对称是原版积木就这么写的)

            if (fire_wait > 0) fire_wait = fire_wait - 1;
            if (key_pressed("SPACE") && fire_wait == 0) {
                mx.push_back(x);
                my.push_back(y + 20);
                fire_wait = 10;
                play_sound("fire");            // 我方开炮音效
            }

            ey = ey - speed;
            if (ey < -190) {                   // 漏掉一个!
                miss = miss + 1;
                ex = random_int(-200, 200);
                ey = 170;
            }

            for (int i = (int)mx.size() - 1; i >= 0; i--) {
                my[i] = my[i] + 10;
                if (touching(mx[i], my[i], ex, ey)) {
                    score = score + 1;
                    play_sound("boom");        // 爆炸音效
                    ex = random_int(-200, 200);
                    ey = 170;
                    speed = speed + 0.5f;      // 越打越快
                    mx.erase(mx.begin() + i);
                    my.erase(my.begin() + i);
                } else if (my[i] > 200) {
                    mx.erase(mx.begin() + i);
                    my.erase(my.begin() + i);
                }
            }

            clear();
            draw_background();
            draw_tank(x, y, 0);
            draw_enemy(ex, ey, 180);
            for (int i = 0; i < (int)mx.size(); i++)
                draw_missile(mx[i], my[i], 0);
            draw_text("分数: " + std::to_string(score), -100, 155);
            draw_text("生命: " + std::to_string(3 - miss), 100, 155);

            if (miss >= 3) {                   // 生命耗尽 → 结束
                state = 2;
                play_music("over");
            }
        } else {
            // ============ 结束画面 ============
            draw_gameover_screen();
            draw_text("按 R 再来一局", 0, -60);
            // 用 R 而不是空格: 玩家松手慢半拍, 空格就会把新的一局"穿"进去
            if (key_pressed("R")) {            // 重置一切, 回开场
                x = 0; y = -150;
                mx.clear();  my.clear();
                fire_wait = 0;
                ex = 0; ey = 170; speed = 2;
                score = 0; miss = 0;
                state = 0;
            }
        }
        display();    // 每帧的最后统一"贴出来"一次——三个状态共用。
                      // display 贴两次会来回翻两个缓冲, 画面会闪, 别学!
    }
    return 0;
}

// ============================================================
//  试一试:
//  1. 把循环末尾的 display() 挪进 state==1 分支里(其他两个状态不画了
//     就不贴)——开场/结束画面会变成什么样? 想想为什么。
//  2. 改成 5 条生命, 每次命中只加 0.25 的速度——做一个"休闲版"。
//  3. 结束画面上显示本局分数: 在 state==2 里也画一个 draw_text。
//  4. 进阶: 开场画面做成"按 1 简单 / 按 2 困难", 困难 = speed 一开始就是 4。
//     提示: state==0 里再判断 key_pressed("1") 和 key_pressed("2")。
// ============================================================
