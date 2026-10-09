# Dynasty Patches

Dynasty Patches 是一组面向汉堂经典游戏的补丁工具。当前包含《天地劫序传 幽城幻剑录》(Castle: The Forbidden Divines) 的 `CastlePatches`，以及《天地劫 神魔至尊传》的 `SwordmanPatches`。两个工具都只修改新建游戏进程的内存，不会修改原始游戏文件。

## 项目结构

- `src/CastlePatches/`：Castle 补丁工具及其运行时模块。
- `src/SwordmanPatches/`：Swordman 补丁工具及其运行时模块；当前只支持 `Sm_win2.exe` 的已校验版本。
- `analysis/`：逆向分析过程中整理的函数、调用关系和验证记录。
- `tools/`：IDA Pro 9.4 / idalib 辅助脚本。
- `ANALYSIS.md`：Castle 补丁位点和功能对应关系。

后续其他汉堂游戏的工具可继续放在 `src/` 下，与 `CastlePatches` 并列维护。

## 编译环境

- Windows
- CMake 3.20 或更高版本
- Visual Studio 生成器
- 支持 C11 的编译器

## 编译

在项目根目录执行：

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release
```

输出文件位于 `build/bin/Release/`（或对应配置目录）：

- `CastlePatches.exe`：Castle 补丁启动器。
- `CastleRuntime.dll`：目标游戏进程内的 32 位运行时模块；启用窗口模式、结算倍率或动态遇敌率时使用。

- `SwordmanPatches.exe`：Swordman 补丁启动器。
- `SwordmanRuntime.dll`：目标游戏进程内的 32 位运行时模块。

所有 x86 汇编（runtime hook 和启动器内嵌的加载 stub）统一使用 [NASM](https://www.nasm.us/) 编译，可通过 `winget install nasm` 或 `scoop install nasm` 安装。仅支持 Visual Studio（MSVC）工具链；GNU/MinGW 构建不受支持（UCRT64 等 64 位工具链无法编译 32 位 runtime，MSYS2 的 32 位环境正在淘汰）。

也可以构建 64 位启动器：

```powershell
cmake -S . -B build-x64 -G "Visual Studio 18 2026" -A x64
cmake --build build-x64 --config Release
```

目标游戏为 32 位程序，因此 64 位启动器仍会同时生成兼容的 `CastleRuntime.dll`。

## 发布

CastlePatches 的版本由 `cmake/CastlePatchesVersion.cmake` 统一管理，并写入启动器和运行时 DLL 的 Windows 版本资源。变更记录位于 `src/CastlePatches/CHANGELOG.md`。

推送格式为 `castle-v<版本号>` 的标签会触发 CastlePatches 的发布工作流。工作流仅构建 Win32 版本，并将 `CastlePatches.exe`、`CastleRuntime.dll`、`README.md`、`LICENSE` 和该工具的 `CHANGELOG.md` 打包为 ZIP 附件。其他工具应分别维护自己的变更日志和发布工作流，并使用独立的标签前缀。

## 使用说明

将 `CastlePatches.exe` 与 `CastleRuntime.dll` 放在同一目录，启动后选择需要的补丁并点击启动按钮。程序会优先查找当前目录下的 `RPG.exe`、`exe/RPG.exe` 和 `Castle/exe/RPG.exe`；均未找到时提供文件选择。

将 `SwordmanPatches.exe` 与 `SwordmanRuntime.dll` 放在同一目录，选择 SHA-256 为 `2ad46144eaca95b92a7d28ad946687df09fb0c3155cf3ccafa8381fba53afbb2` 的 `Sm_win2.exe` 后点击启动。启动器会先校验完整文件，再创建挂起进程并注入运行时；不匹配的 `Swordman.exe`、`Sm_win1.exe` 和其他修改版会被拒绝。

「初始窗口分辨率」提供 `1x（640 × 400）`、`1.5x（960 × 600）`、`2x（1280 × 800）`、`2.5x（1600 × 1000）` 和 `3x（1920 × 1200）`，默认 `1x`。尺寸指客户区，仅在窗口创建时应用，画面由游戏自带功能缩放。该选项独立于「窗口模式」，保存在 `SwordmanPatches.ini` 的 `[Display] Scale2` 中，值 `2` 至 `6` 对应五档倍率；缺失或无效值按 `1x` 处理。

Swordman 选项还包括「移动后可施法」和「恢复召唤兽复制物品特性」。前者放宽移动后的施法条件，后者保留召唤兽复制物品时的源物品记录；两个选项默认关闭。

「随时中途存档」默认关闭。启用后，在我方回合中，角色行动后仍可通过原有系统菜单保存 `GAMEDATA.GCF`；读取中途存档时保留角色的行动状态和移动力，后续正常回合仍恢复行动力。读取这类存档时也需启用该选项。位点和验证说明见 [Swordman 中途存档分析](src/SwordmanPatches/SAVE_ANALYSIS.md)。

Castle 启用「宽屏模式」时，地图显示完整横向视野；菜单、战斗和结算保持中央 `4:3` 布局，两侧显示进入界面前的当前地图，并叠加灰度和 50% 暗化效果。填充区域不响应游戏输入。地图缓存尚不可用或绘图表面不可写时，两侧使用黑边。

地图进入菜单或商店时，画面逐渐过渡为灰度暗化地图；关闭菜单时保留界面元素向两侧展开的动画及菜单背景，动画结束后从灰度地图直接恢复彩色。战斗使用游戏原有过渡特效；地图之间切换、读档和视频继续使用原有切换效果。没有原生淡入淡出的菜单路径使用 200 毫秒渐变。

启动器只修改新建游戏进程的内存，不会写回磁盘上的原始 `RPG.exe`。窗口模式下可选择窗口倍率，并启用鼠标锁定。启用「动态调整遇敌率」后，可在游戏中使用以下组合键调整随机遇敌：

- `Ctrl+F10`：恢复 `100%`。
- `Ctrl+F11`：降低一档。
- `Ctrl+F12`：提高一档。

遇敌率档位为 `0%`、`50%`、`75%`、`100%`、`150%`、`200%` 和 `300%`。`0%` 只关闭随机遇敌，不影响剧情战和固定战。窗口模式下会短暂显示当前档位；全屏模式下播放提示音。调整只对当前游戏进程有效。

全屏模式下的热键接收取决于游戏窗口是否转发键盘消息；该行为需要在目标游戏版本上实机确认。

## 许可

本项目采用 MIT License，详见 `LICENSE`。

## 第三方组件

- [MinHook](https://github.com/TsudaKageyu/minhook)：v1.3.4，通过其原生 CMake target 和 `MH_CreateHook`、`MH_CreateHookApi` 及 trampoline 提供运行时函数和 API hook。MinHook 采用 BSD 2-Clause License。
- [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake)：v0.40.2，用于获取和配置第三方依赖。CPM.cmake 采用 MIT License。
