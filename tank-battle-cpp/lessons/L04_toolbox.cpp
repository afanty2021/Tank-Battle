// ============================================================
//  第 4 课 · 打开工具箱: 自己写函数(Scratch 叫"自制积木")
//
//  今天的核心思想:
//    函数 = 把几句代码打包, 起个名字, 以后一句话就能调用。
//    两种函数:
//      void  f(...)  —— 只干活, 不回答(像 open_window)
//      float f(...)  —— 干完活还 return 一个答案回来(今天我们来写这种)
//
//  另一个任务: 用记事本打开 scratch_like.hpp 看一眼——你会发现
//  老师的工具箱也就是一堆普通的函数和数据, 没有魔法。
//
//  编译: 双击 lessons\compile.bat 输入课号 4 回车, 然后双击 L04.exe
// ============================================================
#include "scratch_like.hpp"

// ---- 我们自制的积木(写在 main 前面, main 才认识它们) ----

// 问题积木: x 是不是太靠右了? 回答 是(true) 或 不是(false)
bool too_right(float x) {
    return x > 230;              // return = 把答案递回去
}

// 变号器: 传进来 3, 递回去 -3(第 3 课的取反, 打包成积木)
float flip(float v) {
    return -v;
}

int main() {
    open_window();

    float x = 0, y = 0;
    float vx = 3, vy = 2;

    while (window_open()) {
        x = x + vx;
        y = y + vy;

        // 第 3 课的反弹, 现在用自制积木写——读起来像句子了:
        if (too_right(x)) vx = flip(vx);
        if (too_right(-x)) vx = flip(vx);   // -x 太靠右 = x 太靠左(想一想)
        if (too_right(y)) vy = flip(vy);
        if (too_right(-y)) vy = flip(vy);

        clear();
        draw_background();
        draw_enemy(x, y, 90);
        display();
    }
    return 0;
}

// ============================================================
//  试一试:
//  1. too_right 里的 230 改成 100 —— 场地变小了, 反弹更频繁。
//  2. 自己写一个积木  bool in_center(float x, float y):
//     当坦克在中心区域(|x|<50 且 |y|<50)时回答 true。
//     提示: 需要 && (并且):  return x > -50 && x < 50 && ...;
//  3. 再写一个  float opposite(float dir): 传入方向, 递回相反方向
//     (0<->180, 90<->270)。提示: return dir + 180;
//  4. 打开 scratch_like.hpp, 找到 open_window 的内部实现,
//     数一数它替你干了多少件事。
// ============================================================
