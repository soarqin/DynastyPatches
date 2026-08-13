# Dynasty Patches

Dynasty Patches 是一组面向汉堂经典游戏的补丁工具。当前已包含《天地劫序传 幽城幻剑录》(Castle: The Forbidden Divines) 的 `CastlePatches`，用于选择补丁并启动游戏，不会修改原始游戏文件。

## 项目结构

- `src/CastlePatches/`：Castle 补丁工具及其运行时模块。
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

输出文件位于 `build/Release/`：

- `CastlePatches.exe`：Castle 补丁启动器。
- `CastleRuntime.dll`：窗口模式所需的配套文件。

也可以构建 64 位启动器：

```powershell
cmake -S . -B build-x64 -G "Visual Studio 18 2026" -A x64
cmake --build build-x64 --config Release
```

目标游戏为 32 位程序，因此 64 位启动器仍会同时生成兼容的 `CastleRuntime.dll`。

## 使用说明

将 `CastlePatches.exe` 与 `CastleRuntime.dll` 放在同一目录，启动后选择需要的补丁并点击启动按钮。程序会优先查找当前目录下的 `RPG.exe`、`exe/RPG.exe` 和 `Castle/exe/RPG.exe`；均未找到时提供文件选择。

启动器只修改新建游戏进程的内存，不会写回磁盘上的原始 `RPG.exe`。窗口模式下可选择窗口倍率，并启用鼠标锁定。

## 许可

本项目采用 MIT License，详见 `LICENSE`。
