// 坦克大战 —— 由 Scratch 3 工程《坦克大战.sb3》忠实移植为 C++/SFML 3
// 舞台 480x360, 逻辑分辨率恒为 960x720(2 倍渲染); 窗口可任意缩放,
// 内容按 4:3 等比缩放居中(letterbox, 同 Scratch 播放器的等比缩放);
// 逻辑固定 30 步/秒, 对应 Scratch 帧率
#ifdef _WIN32
// 语音助手启动需要 CreateProcess; 须在 SFML 之前包含并禁用 min/max 宏
#define NOMINMAX
#include <windows.h>
#endif

#include "Assets.hpp"
#include "Game.hpp"

#include <SFML/Graphics.hpp>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>

// 窗口尺寸变化时重设视图: 保持 4:3 等比缩放并居中, 拉伸会破坏
// 鼠标->舞台坐标映射; 逻辑坐标系始终是 960x720
static void setupView(sf::RenderWindow& window) {
    const sf::Vector2f size = sf::Vector2f(window.getSize());
    if (size.x <= 0.f || size.y <= 0.f) return;
    const float scale = std::min(size.x / 960.f, size.y / 720.f);
    const float w = 960.f * scale, h = 720.f * scale;
    sf::View view(sf::FloatRect({0.f, 0.f}, {960.f, 720.f}));
    view.setViewport(sf::FloatRect({(size.x - w) / 2.f / size.x,
                                    (size.y - h) / 2.f / size.y},
                                   {w / size.x, h / size.y}));
    window.setView(view);
}

int main(int argc, char* argv[]) {
    // --join <ip> 可出现在任意位置(启动直连, 免扫描); 首个非 -- 开头的位置
    // 参数仍是 assets 路径, 兼容 tank-battle.exe <assets路径> 的既有用法
    std::string assetDir = "assets", joinIp;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--join" && i + 1 < argc)
            joinIp = argv[++i];
        else if (arg.rfind("--", 0) != 0 && assetDir == "assets")
            assetDir = arg; // 未知 -- 开头参数不吞掉 assetDir
    }
    const std::string title = "\xE5\x9D\xA6\xE5\x85\x8B\xE5\xA4\xA7\xE6\x88\x98"; // 坦克大战

    Assets assets;
    if (!assets.load(assetDir)) {
        std::cerr << "素材加载失败, 请在项目根目录(含 assets/ 的位置)运行本程序\n";
        return 1;
    }

    sf::RenderWindow window(sf::VideoMode({960, 720}),
                            sf::String::fromUtf8(title.begin(), title.end()),
                            sf::Style::Default);
    window.setFramerateLimit(60);
    setupView(window);
    // 运行中的窗口/任务栏图标(Explorer 里 exe 文件的图标由 res/app_icon.rc 嵌入)
    sf::Image icon;
    if (icon.loadFromFile("res/tank_icon.png"))
        window.setIcon(icon.getSize(), icon.getPixelsPtr());

    Game game(assets, window);
    if (!joinIp.empty()) game.requestDirectJoin(joinIp);

// 语音控制助手(仅 Windows): 独立最小化控制台启动, 游戏退出(心跳失联 5 秒)
// 后自动关闭; 未接管语音端口(通常是已开着另一个游戏实例)时不拉起。
// 必须用 CreateProcess(bInheritHandles=FALSE) 而不是 system("start ..."):
// system 链条会继承游戏的可继承句柄(Windows socket 默认可继承), 助手会
// 拖着游戏的 52017 不放——游戏退出后 5 秒内快速重启, 新实例就绑不上端口,
// 语音静默失效
#ifdef _WIN32
    if (!game.voiceReady())
        std::cout << "[voice] 语音端口未接管(已有游戏实例在跑?), 不拉起助手\n";
    else if (std::filesystem::exists("tools/voice_control.exe")) {
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW; // 等价于 start /MIN
        si.wShowWindow = SW_MINIMIZE;
        PROCESS_INFORMATION pi{};
        wchar_t cmdLine[] = L"tools\\voice_control.exe";
        if (CreateProcessW(nullptr, cmdLine, nullptr, nullptr, FALSE,
                           CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            std::cout << "[voice] 助手拉起失败(错误 " << GetLastError()
                      << "), 语音控制不可用\n";
        }
    }
    else
        std::cout << "[voice] 未找到 tools\\voice_control.exe, 语音控制不可用"
                     "(build.bat 会自动编译)\n";
#endif

    // 固定步长: 逻辑每 1/30 秒推进一步, 渲染每帧执行(与 Scratch 30fps 帧模型一致)
    sf::Clock clock;
    float accumulator = 0.f;
    constexpr float Tick = 1.f / 30.f;

    while (window.isOpen() && !game.wantQuit()) {
        while (const auto event = window.pollEvent()) {
            if (event->is<sf::Event::Resized>())
                setupView(window);
            game.handleEvent(*event);
        }
        if (game.wantQuit())
            break;

        accumulator = std::min(accumulator + clock.restart().asSeconds(), 0.25f);
        while (accumulator >= Tick) {
            game.update(Tick);
            accumulator -= Tick;
        }

        game.render(window);
        window.display();
    }
    return 0;
}
