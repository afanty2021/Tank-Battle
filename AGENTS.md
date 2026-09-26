# AGENTS.md

## 这个工作区是什么

Scratch 3《坦克大战》（.sb3，位于 `坦克大战素材和源码\`）的忠实 C++20/SFML 3 移植，移植项目在 `tank-battle-cpp\`。非 git 仓库。

**移植首要原则**：玩法数值与行为必须与 .sb3 积木一致；任何有意偏离都要记录在 `tank-battle-cpp\README.md` 的"与原版的已知差异"一节，行为级语义改动需配单元测试回归（本项目已经过三轮代码评审，反弹/碰撞/冻结等语义都有回归测试锁住）。

## 构建与测试

- 唯一本机验证过的路线：`cd tank-battle-cpp && build.bat`，四步全绿才算通过（编译 `-Wall -Wextra -Wshadow -Wconversion` 需 0 警告 → 单测 → 素材清单校验 → DLL 闭包部署，闭包应恰好 24 个 DLL）。
- 工具链：MSYS2 UCRT64 g++（`D:\Scoop\apps\msys2\current\ucrt64`）。本机无 cmake（CMakeLists.txt 未验证）；Dev-C++ 自带 TDM-GCC 4.9.2 编不了 SFML 3，勿用。
- 运行 `tank-battle.exe` 必须在 `tank-battle-cpp\` 目录下（按当前工作目录找 `assets\`）。
- 改 `src/Assets.cpp` 里的造型常量后，`tools/check_manifest.py` 会与 `assets/manifest.json` 核对；重新导出素材用 `tools/extract_assets.py`（需 Python3 + Pillow + ffmpeg）。

## 本机环境坑（不写下来必然踩）

- 本机安全软件破坏子进程按 PATH 搜索 DLL/工具：g++ 必须带 `-B<ucrt64>\bin\`；运行期 DLL 靠 `tools/deploy_dlls.py` 拷到 exe 旁；gcc 重装/升级后需重跑 `tools\fix_gcc_dlls.cmd`，否则 cc1plus 报 0xC0000139。
- Git Bash 的 coreutils 损坏（`ls`/`cat`/`sleep` 等找不到）：用绝对路径、`cmd.exe //c` 或 Python。Python 在 `F:/program files/python313/python.exe`。
- 所有 `.bat`/`.cmd` 必须是 CRLF，否则 cmd 解析括号块错乱。
- Read 工具读图片会上传 CDN 而非内联显示：截图验证改用 PIL 像素统计或 analyze_image 工具。
- 运行时冒烟：`powershell.exe -ExecutionPolicy Bypass -File tools/run_and_shoot.ps1 -Phase playing`（默认只抓标题画面）。

## 架构边界

- `src/common.hpp`：纯逻辑（舞台↔窗口坐标、Scratch 方向体系、碰撞盒几何、边缘反弹），只依赖 `<SFML/System.hpp>`，必须保持可被 `tests/unit_tests.cpp` 直接 include 独立编译。
- `src/Assets.*`：素材加载 + 造型元数据（旋转中心、alpha 紧包围盒）。坐标约定：`centerOffset` 为舞台 y 向上约定（画布 y 取负）；`toLocal` 的本地系是 y 向上、运动左侧为正。
- `src/Game.*`：全部游戏逻辑——Phase 状态机对应 Scratch 广播链，实体更新与渲染；游戏常量在 `Game.hpp` 顶部，逐条对应积木参数，不要"顺手优化"。
- 反弹语义（scratch-vm 取证结论，勿回退）：仅当朝越界边运动才翻转方向；越界判定用翻转前的盒，位置钳制用**翻转后**重算的盒（setDirection→keepInFence 顺序）。改动前先看 `testBounceBehaviour`。

## 改动前必读

`tank-battle-cpp\README.md`（Scratch↔C++ 对应关系表、已知差异清单、构建细节）。判断原版行为有疑问时以 .sb3 的 project.json 积木为准（`sensing_of`/菜单值需从 `blocks` 字典解析）。
