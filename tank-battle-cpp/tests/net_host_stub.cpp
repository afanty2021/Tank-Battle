// 无窗口主机驱动: 供 tools/net_smoke.py 端到端验证联机协议闭环。
// 编译(项目根目录):
//   g++ -B<ucrt64>/bin/ -std=c++20 -O1 -Wall -Wextra -Wshadow -Wconversion ^
//       tests/net_host_stub.cpp src/Battle.cpp src/NetSession.cpp ^
//       -o net_host_stub.exe -lsfml-network -lsfml-system
#include "../src/Battle.hpp"
#include "../src/NetSession.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

int main() {
    NetSession net;
    if (!net.startHost("stub-room")) {
        std::printf("[stub] bind 52021 failed\n");
        return 1;
    }
    BattleDefs d;                 // 贴近真实值的手写配置(无素材)
    d.tankHalfExtents = {20.f, 12.f};
    d.missileHalfExtents = {7.f, 3.f};
    BattleState s;
    resetBattle(s, d);
    const auto tick = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 3600; ++frame) { // 上限 120s
        net.poll(1.f / 30.f);
        if (net.clientJoined()) {
            InputState local{};                  // 主机坦克: 原地朝上待打
            InputState in[2] = {local, net.remoteInput()};
            if (!net.remoteInputStale())
                stepBattle(s, d, in, 1.f / 30.f);
            net.sendSnap(makeSnap(s, net.remoteInputStale()));
            if (s.phase == BattlePhase::Over) {
                std::printf("[stub] battle over winner=%d\n", s.winner);
                return 0;
            }
        }
        if (frame % 30 == 0)
            std::printf("[stub] t=%.0fs joined=%d\n",
                        static_cast<double>(frame) / 30.0, net.clientJoined() ? 1 : 0);
        std::this_thread::sleep_until(tick +
            std::chrono::milliseconds(33 * (frame + 1)));
    }
    return 0;
}
