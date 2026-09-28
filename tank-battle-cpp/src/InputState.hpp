#pragma once
// 一帧玩家意图(联机双方公共输入格式): 键鼠与语音口令在 Game 侧归一化成它
#include <cstdint>

struct InputState {
    std::uint8_t moveBits = 0; // bit0=W上 bit1=S下 bit2=A左 bit3=D右
    float aim = 0.f;           // 炮塔绝对朝向(Scratch 方向, 度; 发送方本地算好)
    bool fire = false;         // 发射键按住(或语音"开炮"冷却窗口期)
};
