// 坦克大战 —— 由 Scratch 3 工程《坦克大战.sb3》忠实移植为 C++/SFML 3
// 舞台 480x360, 逻辑分辨率恒为 960x720(2 倍渲染); 窗口可任意缩放,
// 内容按 4:3 等比缩放居中(letterbox, 同 Scratch 播放器的等比缩放);
// 逻辑固定 30 步/秒, 对应 Scratch 帧率
#include "Assets.hpp"
#include "Game.hpp"

#include <SFML/Graphics.hpp>
#include <algorithm>
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
    const std::string assetDir = argc > 1 ? argv[1] : "assets";
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

    Game game(assets, window);

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
