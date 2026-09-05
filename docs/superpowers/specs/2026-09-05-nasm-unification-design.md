# NASM 统一与 loader stub 构建期汇编设计

日期：2026-09-05
状态：已批准（含 CI 可选项）

## 背景与目标

CastlePatches 启动器（`CastlePatches.exe`，可为 Win32 或 x64）中的
`BuildRuntimeLoaderStub`（`src/CastlePatches/main.c:489-647`）以约 77 处
`EmitByte` 调用逐字节发射一段 x86 远程加载 stub，配套
`EmitDword`/`EmitPushImm`/`PatchNearJump`/`PatchConditionalJump`
约 260 行，可读性差。同时 runtime hook 维护 MASM
（`runtime/runtime_hooks_masm.asm`）与 NASM（`runtime/runtime_hooks_nasm.asm`）
两套等价源文件，由 `runtime/CMakeLists.txt:11-18` 按 `if(MSVC)` 二选一，
双套维护已产生实际缺陷：NASM 版本缺少 `_FramePacingTimerSetupHook` 与
`_GameLogicTickHook` 的 `global` 声明（`runtime_hooks_nasm.asm:198,204`），
当前 MinGW 路径会链接失败。

目标：

1. 消除 C 代码中的逐字节机器码发射。
2. 全仓库统一为单一 NASM 汇编，同时支持 MSVC 与 GNU 工具链。
3. 行为保持不变：stub 的重试语义、注入流程、hook 行为均不改。

## 调研结论

### 能否用汇编替代机器码发射：可以

x64 启动器不能链接 x86 目标文件，MSVC x64 也无 inline 汇编，但可在构建期用
`nasm -f bin` 把 stub 汇编为 flat binary，再由纯 CMake 脚本转成 C 数组嵌入启动器；
启动器仅将其作为数据经 `WriteProcessMemory` 写入目标进程。

关键事实（已核实源码）：

- `CreateRemoteThread` 的 `lpParameter` 当前为 `NULL`（`main.c:731`），且 stub
  已按 stdcall 线程例程编写（`ret 4`，`main.c:594-598`）。改传数据块地址后，
  stub 从 `[ebp+8]` 取参，4 处运行期立即数补丁（4 个字符串指针）全部消失。
- 剩余 2 个立即数（`GET_MODULE_HANDLE_IAT_ADDRESS`、
  `GET_PROC_ADDRESS_IAT_ADDRESS`，`main.c:42-46`）是游戏固定基址下的编译期
  常量，可直接烘焙进汇编。
- 结论：stub 成为零运行期补丁的常量 blob；`ml`/`ml64` 无法干净产出 flat
  binary，此路径只有 NASM 可行。

### GNU+MSVC 通用汇编方案：NASM

- NASM `-f win32` 产出的 COFF 同时被 MSVC `link.exe` 与 GNU `ld` 接受；
  x86 cdecl 符号装饰两工具链一致（均为 `_name`），现有 NASM 文件符号已兼容。
- 行业标准做法：x264、ffmpeg、libjpeg-turbo 均以 NASM 通吃 MSVC+GNU。
- 排除项：MASM 不覆盖 GNU（MinGW 无 ml）；GAS 不覆盖真 MSVC（cl 无 GAS 语法
  汇编器，clang-cl 才支持）；手写 trampoline 受 AGENTS.md 禁止。
- 代价：MSVC 构建也需安装 nasm（winget/scoop/choco 一行命令）。

### 静态补丁表不动

`main.c` 28 个与 `runtime_dll.c` 45 个字节数组属于"校验后整块覆写"的数据表，
不属于逐字节发射，不在本次范围内。

## 设计

### 1. loader stub 汇编化

新增 `src/CastlePatches/runtime_loader_stub.asm`（NASM，`BITS 32`），逐指令
移植现有 stub 逻辑，控制流与语义完全不变：

- 序言在现有 `push ebp; mov ebp,esp; push esi` 基础上新增 `push ebx`，
  `mov ebx, [ebp+8]` 保存数据块基址（lpParameter）；`ebx` 为非易变寄存器，
  出口处按反序恢复，保持 WINAPI 约定。
- `esi` 仍为 400 次有界重试计数器；PAUSE 自旋内循环（`ecx=0x10000`）不变。
- IAT 槽位 -1/0 检查、`kernel32.dll`/`LoadLibraryW`/`CastleRuntimeStart`
  解析顺序、失败跳重试、重试耗尽返回 0、成功路径返回 `CastleRuntimeStart`
  状态码，全部保持。
- 字符串指针由 `lea eax, [ebx+偏移]` 得到；两个 IAT 地址为 `%include` 的常量。
- 全部跳转使用标签，由 NASM 自动选择 short/near 形式。

新增 `src/CastlePatches/runtime_loader_stub.inc`：数据块布局偏移
（0x00/0x220/0x240/0x260/0x300）与两个 IAT 地址；与 `main.c` 中对应常量
双向加保持同步注释（沿用 `runtime_hook_addresses.inc` 的既有模式）。

`main.c` 删除：`g_emit_overflow`、`EmitByte`、`EmitDword`、`EmitPushImm`、
`PatchNearJump`、`PatchConditionalJump`、`BuildRuntimeLoaderStub`、
`REMOTE_HOOK_CAPACITY`。

`InjectRuntimeDll` 调整：代码分配大小改用 blob 实际大小；
`CreateRemoteThread` 的 `lpParameter` 传 `remote_data`。其余流程不变：
代码/数据双分配分离、`WriteProcessMemory`、`VirtualProtectEx` →
`PAGE_EXECUTE_READ`、`FlushInstructionCache`、10 秒等待、退出码检查
（1 为成功）。

### 2. blob 嵌入机制

- 新增 `cmake/BinToC.cmake`：`file(READ ... HEX)` 读取 `.bin`，生成
  `runtime_loader_stub_blob.c`（`const unsigned char` 数组 +
  长度常量）与对应声明头 `runtime_loader_stub_blob.h`。纯 CMake，
  无额外工具依赖。
- 外层 `src/CastlePatches/CMakeLists.txt`：`find_program` 查找 nasm，找不到
  则 `FATAL_ERROR` 并附安装指引；`add_custom_command` 依次执行
  `nasm -f bin` 与 bin2c 脚本；生成的 `.c` 加入 `CastlePatches` 目标源。
- nasm 在外层构建中仅作为宿主工具，对 Win32/x64、MSVC/GNU 启动器构建一致。

### 3. runtime hook 单文件化

- `runtime/runtime_hooks_nasm.asm` 重命名为 `runtime/runtime_hooks.asm`，
  补全 `_FramePacingTimerSetupHook`、`_GameLogicTickHook` 的 `global`
  声明，整理错位的 extern/global 块；删除 `runtime_hooks_masm.asm`。
- `runtime/CMakeLists.txt`：移除 `if(MSVC)` 分支，恒
  `enable_language(ASM_NASM)`，`CMAKE_ASM_NASM_OBJECT_FORMAT win32`。
- `cmake/CustomCompilerOptions.cmake`：清理 `ASM_MASM` 相关排除项。

### 4. 文档

- `AGENTS.md` 运行时补丁规则改写：注入 x86 代码一律使用 NASM 独立源文件；
  loader stub 构建期汇编为 flat binary 嵌入启动器；禁止在 C 中发射指令字节
  （原"逐字节+逐指令注释"例外条款删除，例外场景已不存在）。
- `README.md`：构建前置要求加入 nasm（全工具链），移除 ml.exe 相关说明。

## 错误处理

- 构建期缺 nasm：configure 时 `FATAL_ERROR`，附安装指引。
- blob 生成失败或为空：构建失败，不产生半成品。
- 运行期检查保持现状：远程地址须落在 32 位范围内、写内存与属性修改逐步
  检查、线程超时与退出码语义不变。

## 验证

项目无测试框架，本次不新增。验证手段：

1. MSVC Win32 与 x64 两种启动器均完成 configure + build（AGENTS.md 要求）。
2. 本机具备 MinGW 时加验 GNU 路径构建。
3. `ndisasm -b32` 反汇编生成的 blob，逐指令对照设计核对。
4. 开发期用一次性差分工具对比旧 C 发射器输出与新 blob 的指令流
   （临时产物不进版本库）。
5. CI `.github/workflows/release-castle.yml` 增加 MinGW+nasm 构建任务，
   防止单一路径再次失去覆盖。

## 不做的事

- 不重构静态补丁表与 `castle_addresses.h`/`.inc` 地址三分现状。
- 不改变注入时序、重试次数、等待时长等任何运行期行为。
- 不新增测试框架、不自动启动游戏验证。
