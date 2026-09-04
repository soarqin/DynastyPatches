# 变更日志

本文件记录 CastlePatches 的重要变更。

格式遵循 [Keep a Changelog 1.1.0](https://keepachangelog.com/zh-CN/1.1.0/)，并采用[语义化版本](https://semver.org/lang/zh-CN/)。

## [未发布]

### 新增

- 支持在游戏运行期间通过组合键动态调整随机遇敌率。

### 修复

- 修复开启窗口与宽屏补丁时，从地图切换到全屏界面的一帧内鼠标同时显示在原位和左侧偏移位置的问题。
- 修复窗口模式下用 ALT+F4、关闭按钮或任务栏关闭游戏时，RPG.exe 偶发残留不退出的问题：关窗时若 Bink 视频仍在播放，先调用游戏自身的视频关闭例程释放 Bink 与 DirectSound 句柄，避免进程退出阶段在 DLL 拆卸中偶发阻塞；同时加入关闭看门狗，窗口关闭后若进程未在限定时间内退出则强制结束，杜绝残留。

## [0.1.0] - 2026-08-13

### 新增

- 提供 Castle 游戏补丁启动器，以及配套的 32 位运行时模块。
- 支持窗口模式、鼠标锁定、战斗结算倍率等运行时补丁。

[未发布]: https://github.com/soarqin/DynastyPatches/compare/castle-v0.1.0...HEAD
[0.1.0]: https://github.com/soarqin/DynastyPatches/releases/tag/castle-v0.1.0
