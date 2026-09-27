# 坦克大战（C++ / SFML 3 移植版）

本项目由 Scratch 3 工程《坦克大战.sb3》逐积木忠实移植为 C++（SFML 3）。
游戏玩法、数值、美术与音效全部来自原 Scratch 工程。

## 玩法

- **W / A / S / D**：移动坦克（车头转向移动方向，活动范围与原版一致）
- **鼠标**：炮塔始终指向鼠标
- **← / →**：逆时针 / 顺时针旋转炮塔（接管期间暂停鼠标跟随，鼠标一动即恢复）
- **鼠标左键 / 空格**：发射导弹（按住连发，0.5 秒冷却）
- 敌方坦克每 1~5 秒从顶部随机位置生成，随机向下方游走、碰到边缘反弹，
  每 2 秒朝自己朝向发射一发子弹（从炮口发出）
- 导弹击中敌方：+1 分并播放爆炸动画；子弹击中玩家：玩家爆炸 → 游戏结束
- **Esc** 退出；游戏结束后按 **R** 重新开始（原版 Scratch 停止后需重新点击绿旗）

## 目录结构

```
tank-battle-cpp/
├── assets/            游戏素材（由 tools/extract_assets.py 从 .sb3 导出）
│   ├── images/        背景、坦克、导弹、子弹、爆炸动画帧、开始/结束画面
│   ├── sounds/        音效（原 mp3 已转为 SFML 支持的 ogg）
│   ├── fonts/         自带中文字体（Noto Sans SC 子集，OFL 授权，tools/subset_font.py 生成）
│   └── manifest.json  导出清单（尺寸/旋转中心/紧包围盒等元数据）
├── src/
│   ├── main.cpp       入口：固定尺寸窗口、固定 30Hz 逻辑主循环（对应 Scratch 帧率）
│   ├── common.hpp     Scratch 舞台坐标/方向体系换算 + 碰撞几何（可独立单测）
│   ├── Assets.hpp/cpp 素材加载；造型元数据（旋转中心、偏心碰撞盒）
│   └── Game.hpp/cpp   全部游戏逻辑（状态机 + 实体更新 + 渲染）
├── res/               图标资源（tank.ico/tank_icon.png + app_icon.rc，tools/make_icon.py 生成）
├── tests/
│   └── unit_tests.cpp 坐标换算/方向体系/碰撞几何（含偏心盒回归）单元测试
├── tools/
│   ├── extract_assets.py  素材提取脚本（从 .sb3 重新导出 assets/）
│   ├── check_manifest.py  校验 Assets.cpp 手工常量与 manifest.json 一致
│   ├── deploy_dlls.py     用 objdump 计算 DLL 依赖闭包并部署到 exe 旁
│   ├── fix_gcc_dlls.cmd   本机安全软件干扰 PATH 搜索的一次性修复（见下）
│   ├── run_and_shoot.ps1  启动游戏并截图（自动化验证用）
│   └── contact_sheet.png  素材全览图（提取后的人工核对用）
├── CMakeLists.txt     CMake 构建（含 unit_tests 与 ctest）
├── build.bat          一键构建（编译 → 单测 → manifest 校验 → DLL 部署）
└── tank-battle.exe    构建产物
```

## 构建（本机 MSYS2 方式，已验证可用）

一次性准备（任一终端，pacman 在 `D:\Scoop\apps\msys2\current\usr\bin`）：

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-sfml
```

之后在项目根目录直接执行 `build.bat`，它会依次：编译（`-Wall -Wextra -Wshadow -Wconversion`）
→ 运行单元测试 → 校验素材清单 → 部署运行时 DLL。

**运行**：在项目根目录执行 `tank-battle.exe`。注意程序按**当前工作目录**
查找 `assets\`——双击资源管理器里的 exe 可以，但从别处用绝对路径启动时
需先 `cd` 到本目录。

> **本机注意事项**：这台机器上子进程按 PATH 搜索 DLL/工具会被某个安全软件
> 干扰（现象：cc1plus 等编译器内部进程报 0xC0000139）。`build.bat` 已做两层
> 绕过：① g++ 加 `-B<ucrt64>\bin\` 显式指定工具目录；② `tools\deploy_dlls.py`
> 把 DLL 依赖闭包全部拷到 exe 旁。另外 `tools\fix_gcc_dlls.cmd` 需要执行一次
> （把 8 个依赖 DLL 拷到 cc1/cc1plus 旁边）——gcc 重装/升级后需重跑。
> （Dev-C++ 自带的 TDM-GCC 4.9.2 只支持 C++11，编不了 SFML 3，勿用。）

## 重新导出素材

```bash
python tools/extract_assets.py "路径/坦克大战.sb3" assets
```

需要 Python 3 + Pillow + ffmpeg。脚本会解出 .sb3 内全部造型（含从 SVG
造型中抽出内嵌位图并施加原始几何变换）、把 mp3 音效转为 ogg，并生成
manifest.json。图片产物逐字节可复现；ogg 因编码器非确定性每次略有差异。
导出后跑 `python tools/check_manifest.py` 确认 `src/Assets.cpp` 里的
手工元数据常量仍与 manifest 一致（build.bat 每次构建都会检查）。

## 测试

```bash
build.bat          # 含单测
tests/unit_tests   # 或: g++ tests/unit_tests.cpp -o unit_tests.exe -lsfml-system
ctest --test-dir build   # CMake 路线
```

覆盖：舞台↔窗口坐标换算往返、Scratch 方向体系（dirVector/pointDirection/
spriteRotation）、偏心碰撞盒判定（含 C1 回归：旋转中心右侧的空处不得命中；
竖直偏移的 y 向上符号约定）、旋转盒 AABB 与贴墙反弹边界、反弹朝向守卫的
行为级回归（出生方向 130~240° 的敌人 120 帧内必须离开顶部条带继续下行），
以及反弹当帧必须按**翻转后的**包围盒钳制（盒子同帧回到场内并贴墙，对应
scratch-vm 的 setDirection→keepInFence 顺序）。

## 与 Scratch 工程的对应关系

| Scratch 概念 | 本项目实现 |
|---|---|
| 舞台 480×360、绿旗 30fps | `common.hpp` 坐标换算；`main.cpp` 固定 1/30s 逻辑步长，窗口可缩放（内容 4:3 等比缩放居中，逻辑分辨率恒为 960×720） |
| 角色 = 精灵 + 造型 + 脚本 | `Costume`（贴图+旋转中心+偏心碰撞盒）+ `Player/Enemy/Missile/Bullet` 实体 |
| 克隆（敌方坦克/导弹/子弹） | `std::vector` 中动态增删实体 |
| 广播（开始游戏/被击中/敌人开炮/游戏结束） | `Phase` 状态机（Title/TitleMusic/Playing/PlayerDying/GameOverMusic/Stopped） |
| 碰到（按绘制像素） | 贴图 alpha 紧包围盒 + 内容中心相对旋转中心的偏移（敌方旋转中心在炮口，偏约 134 舞台单位），旋转盒判定（`stage::boxHitTest`） |
| 碰到边缘就反弹 | 旋转盒四角投影 AABB + 朝向守卫与翻转后钳制（`stage::bounceOffEdges`，同 scratch-vm 的 setDirection→keepInFence 顺序） |
| 变量“分数”监视器 | 左上角 `sf::Text`（画在结束画面之上，同原版监视器层级） |
| 角色1（操作提示文字） | 舞台顶部 `sf::Text`（原造型画布 291×21，旋转中心在画布下方 131.6 单位外，文字实际渲染于锚点上方 y≈+166） |
| 数值（每帧 5/2/10/5 步、1~5 秒生成、2 秒开炮、0.5 秒冷却、爆炸 0.1s×6 帧） | `Game.hpp` 顶部常量，逐条对应积木参数 |

### 与原版的已知差异

- Scratch“碰到”按精灵实际像素逐点判定，这里用不透明像素紧包围盒（含偏心
  修正）的旋转矩形近似，手感基本一致；
- Scratch“碰到边缘就反弹”按实际像素与运动朝向判定；这里用旋转盒 AABB +
  “仅当朝着越界边运动才镜像方向”守卫还原（含行为级回归测试）；
- **子弹克隆连锁增殖未复现**：scratch-vm 的广播会同样唤醒克隆体，原版每次
  “敌人开炮”广播会让每个在场子弹克隆再克隆自己，子弹数随时间倍增（后期
  弹幕海）。移植版固定每敌每 2 秒 1 颗——复现该行为会明显劣化手感，故
  主动偏离并在此记录；
- **原版“点击即拖拽角色”的瑕疵未复现**：工程里除提示文字（角色1）外所有
  角色都设了 draggable，而原版是“按下鼠标就开火”——在 Scratch 编辑器舞台
  /全屏模式下，点击落到可拖拽角色上会同时被判定为拖拽：最常见的是点到
  自己坦克时把炮塔拖走（与它每帧“移到车身”的脚本互相拉扯），且导弹克隆
  体是“移到炮塔 + 取炮塔方向”，会从被拖离的炮口射出；车身同理可被拖出
  WASD 活动范围（范围判定只在移动脚本内）。移植版窗口内没有“拖拽角色”
  的概念，点击只负责瞄准开火，此瑕疵不复现；
- 原版“停止全部”后项目静止，这里支持按 R 重开、Esc 退出；发射键在原版
  “按下鼠标”之外新增空格；炮塔在原版“永远面向鼠标”之外新增 ←/→ 键旋转
  （每秒 180°，接管期间暂停鼠标跟随，鼠标移动即交还）；
- 原版导弹冷却(0.5s)短于敌方爆炸时长(0.6s)，理论上可对同一正在爆炸的敌人
  二次得分；移植版跳过正在爆炸的敌人（视为已死），不可二次得分。类似的
  边角行为：原版结算期间玩家若再被子弹命中会重启爆炸动画与结算音乐，
  移植版不再响应（隐形车身无碰撞）；
- 随机数：Scratch 对整数参数返回整数随机，移植版用连续均匀分布，手感无差；
- 并行脚本行为已按原版还原：爆炸与整个结算音乐期间可继续 WASD 驾驶
  （隐形）车身、炮塔继续瞄准、可继续开火、导弹继续飞行计分、结束时全部
  敌方当场冻结。
