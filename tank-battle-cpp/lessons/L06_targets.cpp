// ============================================================
//  第 6 课 · 打靶得分: 随机数 + 碰撞 + 记分文字
//
//  三个新积木(工具箱里本来就有, 今天启用):
//    random_int(-200, 200)      【在 -200 到 200 间随机选一个数】
//    touching(x1, y1, x2, y2)   两点靠得够近(距离<20)就算碰上
//    draw_text("分数: 3", x, y) 在舞台上写一行字(以 x,y 为中心)
//
//  还有一个新招: std::to_string(数) —— 把数字变成能拼进句子的文字。
//
//  玩法: 坦克左右移动, 空格发射; 敌人从顶上慢慢爬下来, 打中 +1 分
//  并在随机位置重生。
//
//  编译: 双击 lessons\compile.bat 输入课号 6 回车,
//        然后双击上一层 tank-battle-cpp\ 文件夹里的 L06.exe
// ============================================================
#include "scratch_like.hpp"
#include <string>              // std::to_string 在这里
#include <vector>

int main() {
    open_window();

    // 坦克和导弹(第 5 课全套搬来)
    float x = 0, y = -150;
    std::vector<float> mx, my;
    int fire_wait = 0;

    // 敌人: 随机横坐标出生, 从顶上往下爬
    float ex = random_int(-200, 200);   // random_int 回答一个随机整数
    float ey = 170;

    int score = 0;                      // 分数(Scratch 变量)

    while (window_open()) {
        // ---- 1. 驾驶 + 发射(第 2/5 课) ----
        if (key_pressed("A")) x = x - 5;
        if (key_pressed("D")) x = x + 5;
        if (x > 205)  x = 205;    // 活动范围与第 2 课/正式版一致
        if (x < -211) x = -211;    // (两边不对称是原版积木就这么写的)

        if (fire_wait > 0) fire_wait = fire_wait - 1;
        if (key_pressed("SPACE") && fire_wait == 0) {
            mx.push_back(x);
            my.push_back(y + 20);
            fire_wait = 10;
        }

        // ---- 2. 敌人往下爬, 爬出底部就随机重生 ----
        ey = ey - 2;
        if (ey < -190) {
            ex = random_int(-200, 200);
            ey = 170;
        }

        // ---- 3. 导弹飞 + 命中判定(从后往前, 因为可能要删) ----
        for (int i = (int)mx.size() - 1; i >= 0; i--) {
            my[i] = my[i] + 10;
            if (touching(mx[i], my[i], ex, ey)) {     // 【碰到敌人?】
                score = score + 1;                    // 得分!
                ex = random_int(-200, 200);           // 敌人换个地方重生
                ey = 170;
                mx.erase(mx.begin() + i);             // 这颗导弹用掉了
                my.erase(my.begin() + i);
            } else if (my[i] > 200) {                 // 没打中, 飞出屏删掉
                mx.erase(mx.begin() + i);
                my.erase(my.begin() + i);
            }
        }

        // ---- 4. 画(顺序: 背景在最底, 文字在最上) ----
        clear();
        draw_background();
        draw_tank(x, y, 0);
        draw_enemy(ex, ey, 180);                      // 180 = 面朝下(冲你来)
        for (int i = 0; i < (int)mx.size(); i++)
            draw_missile(mx[i], my[i], 0);
        draw_text("分数: " + std::to_string(score), 0, 155);  // 顶部记分
        display();
    }
    return 0;
}

// ============================================================
//  试一试:
//  1. 敌人速度 2 改成 1(好打)或 4(太难), 找一个你觉得刚刚好的数。
//  2. 打中重生的敌人每次快一点: 加一个 float speed = 2;,
//     命中时写 speed = speed + 0.5;, 把 ey = ey - 2 改成 ey = ey - speed。
//  3. touching 的判定圈半径写在 scratch_like.hpp 里(搜 20),
//     找到并改成 30 —— 变得好打多了(这叫"调整手感")。
//  4. 进阶: 漏掉敌人扣分 —— 在敌人重生(没被打中那次)的地方 score = score - 1;
//     提示: 需要区分"打中重生"和"漏掉重生", 可以用一个 bool missed。
// ============================================================
