# NASM 统一与 loader stub 构建期汇编实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 全仓库统一为单一 NASM 汇编；把启动器中逐字节发射的 x86 loader stub 改为构建期汇编成 flat binary 并嵌入，删除全部 C 机器码发射逻辑。

**Architecture:** runtime hook 用 NASM 汇编为 win32 COFF 直接链接进 `CastleRuntime.dll`（MSVC 与 GNU 通用）；loader stub 用 `nasm -f bin` 汇编为 flat binary，经纯 CMake 脚本转成 C 数组编入启动器，启动器通过 `CreateRemoteThread` 的 `lpParameter` 向 stub 传数据块地址，stub 成为零运行期补丁的常量 blob。

**Tech Stack:** C11、CMake ≥ 3.20、NASM（全工具链统一）、MSVC（VS 18 2026）、MinGW（仅 CI）、MinHook v1.3.4。

**Spec:** `docs/superpowers/specs/2026-09-05-nasm-unification-design.md`

---

## 文件结构

| 文件 | 责任 | 动作 |
|---|---|---|
| `src/CastlePatches/runtime/runtime_hooks.asm` | 18 个 hook 入口（唯一汇编源） | 由 `runtime_hooks_nasm.asm` 改名，补 2 个 global |
| `src/CastlePatches/runtime/runtime_hooks_masm.asm` | — | 删除 |
| `src/CastlePatches/runtime/CMakeLists.txt` | runtime DLL 构建，恒用 NASM | 修改 |
| `src/CastlePatches/runtime_loader_stub.asm` | 远程加载 stub（x86，flat binary 源） | 新建 |
| `src/CastlePatches/runtime_loader_stub.inc` | stub 常量（数据块布局 + IAT 地址） | 新建 |
| `cmake/BinToC.cmake` | flat binary → C 数组转换脚本 | 新建 |
| `src/CastlePatches/CMakeLists.txt` | nasm 查找、blob 生成、集成 | 修改 |
| `cmake/CustomCompilerOptions.cmake` | 清理 ASM_MASM 排除项 | 修改 |
| `src/CastlePatches/main.c` | 删除发射器，改用嵌入 blob + lpParameter | 修改 |
| `AGENTS.md` | 汇编规则更新 | 修改 |
| `README.md` | 构建前置要求更新 | 修改 |
| `.github/workflows/release-castle.yml` | release 任务保障 nasm | 修改 |
| `.github/workflows/ci-mingw.yml` | MinGW+nasm 构建防回归 | 新建 |

## 验证基础命令

本机已确认 nasm 3.02 与 ndisasm 可用；无本机 MinGW（GNU 路径由 CI 验证）。
每个涉及代码的任务完成后执行（在仓库根目录的 Git Bash 中）：

```bash
cmake -S . -B build-win32 -G "Visual Studio 18 2026" -A Win32 && cmake --build build-win32 --config Release --parallel
cmake -S . -B build-x64 -G "Visual Studio 18 2026" -A x64 && cmake --build build-x64 --config Release --parallel
```

预期：两个构建都成功，产出 `build-win32/bin/Release/CastlePatches.exe` + `CastleRuntime.dll` 和 `build-x64/bin/Release/CastlePatches.exe` + `CastleRuntime.dll`。

---

### Task 1: hook 汇编单文件化，runtime 恒用 NASM

**Files:**
- Rename: `src/CastlePatches/runtime/runtime_hooks_nasm.asm` → `src/CastlePatches/runtime/runtime_hooks.asm`
- Delete: `src/CastlePatches/runtime/runtime_hooks_masm.asm`
- Modify: `src/CastlePatches/runtime/CMakeLists.txt:11-18`
- Modify: `src/CastlePatches/CMakeLists.txt:25-55`
- Modify: `cmake/CustomCompilerOptions.cmake`（8 处）

- [ ] **Step 1: 改名并修复 global 声明**

```bash
cd /e/Projects/DynastyPatches
git mv src/CastlePatches/runtime/runtime_hooks_nasm.asm src/CastlePatches/runtime/runtime_hooks.asm
git rm src/CastlePatches/runtime/runtime_hooks_masm.asm
```

在 `runtime_hooks.asm` 中做两处编辑：

编辑 A — 把错位在 global 块中的两个 extern 移到 extern 区。找到（原文件第 26-29 行附近）：

```nasm
extern _PrepareFrameTimer
extern _ShouldRunGameLogic

global _SurfaceFormatHook
```

替换为：

```nasm
extern _PrepareFrameTimer
extern _ShouldRunGameLogic
extern _g_original_timer_setup
extern _g_original_game_logic_tick

global _SurfaceFormatHook
```

编辑 B — 补全缺失的 global 并删除错位行。找到（原文件第 43-47 行附近）：

```nasm
global _EncounterInitialHook
global _EncounterRegenerationHook
extern _g_original_timer_setup
extern _g_original_game_logic_tick
```

替换为：

```nasm
global _EncounterInitialHook
global _EncounterRegenerationHook
global _FramePacingTimerSetupHook
global _GameLogicTickHook
```

- [ ] **Step 2: runtime/CMakeLists.txt 恒用 NASM**

找到（第 11-18 行）：

```cmake
if(MSVC)
    enable_language(ASM_MASM)
    set(CASTLE_RUNTIME_ASM runtime_hooks_masm.asm)
else()
    set(CMAKE_ASM_NASM_OBJECT_FORMAT win32)
    enable_language(ASM_NASM)
    set(CASTLE_RUNTIME_ASM runtime_hooks_nasm.asm)
endif()
```

替换为：

```cmake
# NASM is the single assembler for every toolchain: its win32 COFF output links
# with both MSVC link.exe and GNU ld.
set(CMAKE_ASM_NASM_OBJECT_FORMAT win32)
enable_language(ASM_NASM)
set(CASTLE_RUNTIME_ASM runtime_hooks.asm)
```

- [ ] **Step 3: 外层 CMakeLists 查找 nasm 并转发给 runtime 子构建**

在 `src/CastlePatches/CMakeLists.txt` 的 `configure_file(...)`（第 8 行）之后插入：

```cmake
# NASM assembles both the runtime hooks and the embedded loader stub.
find_program(NASM_EXECUTABLE NAMES nasm)
if(NOT NASM_EXECUTABLE)
    message(FATAL_ERROR
        "NASM is required for all toolchains. "
        "Install it with 'winget install nasm', 'scoop install nasm' or 'choco install nasm'.")
endif()
```

把第 29-42 行的分支：

```cmake
if(MSVC)
    list(APPEND RUNTIME_CONFIGURE_COMMAND -A Win32)
else()
    list(APPEND RUNTIME_CONFIGURE_COMMAND
        "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
        "-DCMAKE_C_FLAGS=-m32"
        "-DCMAKE_SHARED_LINKER_FLAGS=-m32"
    )
    if(CMAKE_ASM_NASM_COMPILER)
        list(APPEND RUNTIME_CONFIGURE_COMMAND
            "-DCMAKE_ASM_NASM_COMPILER:FILEPATH=${CMAKE_ASM_NASM_COMPILER}"
        )
    endif()
endif()
```

替换为：

```cmake
if(MSVC)
    list(APPEND RUNTIME_CONFIGURE_COMMAND -A Win32)
else()
    list(APPEND RUNTIME_CONFIGURE_COMMAND
        "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
        "-DCMAKE_C_FLAGS=-m32"
        "-DCMAKE_SHARED_LINKER_FLAGS=-m32"
    )
endif()
list(APPEND RUNTIME_CONFIGURE_COMMAND
    "-DCMAKE_ASM_NASM_COMPILER:FILEPATH=${NASM_EXECUTABLE}"
)
```

把 DEPENDS 列表（第 47-53 行）中的：

```cmake
    DEPENDS runtime/runtime_dll.c
            runtime/runtime_hooks_masm.asm
            runtime/runtime_hooks_nasm.asm
```

替换为：

```cmake
    DEPENDS runtime/runtime_dll.c
            runtime/runtime_hooks.asm
```

- [ ] **Step 4: 清理 CustomCompilerOptions.cmake 的 ASM_MASM**

8 处（第 6、7、10、13、14、18、19、20 行）统一把：

```
$<NOT:$<OR:$<COMPILE_LANGUAGE:ASM_NASM>,$<COMPILE_LANGUAGE:ASM_MASM>>>
```

替换为：

```
$<NOT:$<COMPILE_LANGUAGE:ASM_NASM>>
```

（用编辑器对该文件执行 replace_all 即可。）

- [ ] **Step 5: 构建验证**

执行「验证基础命令」中的两条构建命令。
预期：均成功；`build-win32/runtime_x86` 子构建日志中出现 nasm 汇编 `runtime_hooks.asm`，不再出现 ml。

- [ ] **Step 6: 提交**

```bash
git add -A
git commit -m "refactor: unify runtime hooks on NASM and drop the MASM variant"
```

---

### Task 2: loader stub 汇编源 + blob 嵌入生成（main.c 尚未切换）

**Files:**
- Create: `src/CastlePatches/runtime_loader_stub.inc`
- Create: `src/CastlePatches/runtime_loader_stub.asm`
- Create: `cmake/BinToC.cmake`
- Modify: `src/CastlePatches/CMakeLists.txt`（add_executable 与自定义命令）

- [ ] **Step 1: 新建 `src/CastlePatches/runtime_loader_stub.inc`**

```nasm
; Constants for runtime_loader_stub.asm.
; Keep the STUB_* offsets in sync with the RUNTIME_STUB_* enum in main.c.
; The IAT addresses identify the supported RPG.exe image; they belong to the
; game version, not to the launcher or runtime module.
STUB_PATH_OFFSET                EQU 000h
STUB_KERNEL_NAME_OFFSET         EQU 220h
STUB_LOAD_NAME_OFFSET           EQU 240h
STUB_START_NAME_OFFSET          EQU 260h
GET_MODULE_HANDLE_IAT_ADDRESS   EQU 004600C8h
GET_PROC_ADDRESS_IAT_ADDRESS    EQU 00460090h
```

- [ ] **Step 2: 新建 `src/CastlePatches/runtime_loader_stub.asm`**

完整内容如下（逻辑逐指令移植自被删除前的 `BuildRuntimeLoaderStub`，语义不变）：

```nasm
; Remote loader stub for CastleRuntime.dll.
;
; This code runs as a CreateRemoteThread thread routine inside the suspended
; 32-bit game process. The launcher assembles it to a flat binary at build
; time (nasm -f bin, see src/CastlePatches/CMakeLists.txt) and embeds the
; bytes as a generated C array, so launchers of either bitness share it.
;
; Entry: WINAPI thread routine (stdcall). [ebp+8] receives the base address
; of the remote data block passed as lpParameter; see the STUB_* offsets in
; runtime_loader_stub.inc for its layout (strings are written by the launcher).
; Exit: eax is the CastleRuntimeStart return value (1 = success), or 0 when
; resolution fails or the bounded retry loop is exhausted.

BITS 32

%include "runtime_loader_stub.inc"

%define RETRY_COUNT 400
%define SPIN_COUNT  0x00010000

    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov ebx, [ebp+8]        ; ebx = data block base (lpParameter)
    mov esi, RETRY_COUNT    ; esi = bounded retry counter

retry_loop:
    ; The target's import slots can still be zero/FFFFFFFF while its
    ; suspended loader is finishing. Check each slot before calling through
    ; it; jumping through FFFFFFFF was the original startup crash.
    mov eax, [GET_MODULE_HANDLE_IAT_ADDRESS]
    cmp eax, -1
    je retry
    test eax, eax
    je retry
    lea ecx, [ebx + STUB_KERNEL_NAME_OFFSET]
    push ecx
    call eax                ; GetModuleHandleA("kernel32.dll")
    test eax, eax
    je retry
    mov edx, eax            ; edx = kernel32 module handle

    ; Resolve GetProcAddress before pushing its arguments so every retry
    ; path leaves the stack balanced.
    mov eax, [GET_PROC_ADDRESS_IAT_ADDRESS]
    cmp eax, -1
    je retry
    test eax, eax
    je retry
    lea ecx, [ebx + STUB_LOAD_NAME_OFFSET]
    push ecx
    push edx
    call eax                ; GetProcAddress(kernel32, "LoadLibraryW")
    test eax, eax
    je retry

    lea ecx, [ebx + STUB_PATH_OFFSET]
    push ecx
    call eax                ; LoadLibraryW(CastleRuntime.dll path)
    test eax, eax
    je retry
    mov edx, eax            ; edx = CastleRuntime module handle

    ; Resolve GetProcAddress again after LoadLibraryW returned.
    mov eax, [GET_PROC_ADDRESS_IAT_ADDRESS]
    cmp eax, -1
    je retry
    test eax, eax
    je retry
    lea ecx, [ebx + STUB_START_NAME_OFFSET]
    push ecx
    push edx
    call eax                ; GetProcAddress(CastleRuntime, "CastleRuntimeStart")
    test eax, eax
    je finish               ; Missing export: skip only the call, eax stays 0.
    call eax                ; CastleRuntimeStart()

finish:
    pop esi
    pop ebx
    pop ebp
    ret 4                   ; stdcall: callee discards lpParameter

retry:
    ; Never call Sleep through the target IAT here: this thread can run
    ; while the process is still completing loader initialization.
    ; A PAUSE-backed bounded spin is self-contained.
    dec esi
    jz exhausted
    pause
    mov ecx, SPIN_COUNT
spin_inner:
    pause
    dec ecx
    jnz spin_inner
    jmp retry_loop

exhausted:
    xor eax, eax
    pop esi
    pop ebx
    pop ebp
    ret 4
```

- [ ] **Step 3: 新建 `cmake/BinToC.cmake`**

```cmake
# Convert a flat binary into a C source/header pair that embeds it as an array.
# Usage:
#   cmake -DINPUT_FILE=<bin> -DOUTPUT_C=<c file> -DOUTPUT_H=<header> -P BinToC.cmake
if(NOT INPUT_FILE OR NOT OUTPUT_C OR NOT OUTPUT_H)
    message(FATAL_ERROR "BinToC.cmake requires INPUT_FILE, OUTPUT_C and OUTPUT_H.")
endif()

file(READ "${INPUT_FILE}" blob_hex HEX)
string(LENGTH "${blob_hex}" blob_hex_length)
math(EXPR blob_size "${blob_hex_length} / 2")
if(blob_size EQUAL 0)
    message(FATAL_ERROR "BinToC.cmake: ${INPUT_FILE} is empty.")
endif()

string(REGEX REPLACE "(..)" "0x\\1," blob_bytes "${blob_hex}")
string(REGEX REPLACE "((0x..,){12})" "\\1\n    " blob_bytes "${blob_bytes}")

get_filename_component(header_name "${OUTPUT_H}" NAME)
file(WRITE "${OUTPUT_C}"
"/* Auto-generated by cmake/BinToC.cmake. Do not edit. */
#include \"${header_name}\"

const unsigned char kRuntimeLoaderStub[] = {
    ${blob_bytes}
};

const unsigned int kRuntimeLoaderStubSize = ${blob_size}u;
")
file(WRITE "${OUTPUT_H}"
"/* Auto-generated by cmake/BinToC.cmake. Do not edit. */
#ifndef RUNTIME_LOADER_STUB_BLOB_H
#define RUNTIME_LOADER_STUB_BLOB_H

extern const unsigned char kRuntimeLoaderStub[];
extern const unsigned int kRuntimeLoaderStubSize;

#endif
")
```

- [ ] **Step 4: 外层 CMakeLists 集成 blob 生成**

在 Task 1 插入的 `find_program` 块之后（`add_executable` 之前）插入：

```cmake
# The remote loader stub is 32-bit x86 code for the game process. Assemble it
# to a flat binary and embed it as a generated C array so the launcher works
# identically for Win32 and x64 builds.
set(LOADER_STUB_DIR "${CMAKE_CURRENT_BINARY_DIR}/loader_stub")
set(LOADER_STUB_BIN "${LOADER_STUB_DIR}/runtime_loader_stub.bin")
set(LOADER_STUB_C "${LOADER_STUB_DIR}/runtime_loader_stub_blob.c")
set(LOADER_STUB_H "${LOADER_STUB_DIR}/runtime_loader_stub_blob.h")
add_custom_command(
    OUTPUT "${LOADER_STUB_C}" "${LOADER_STUB_H}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${LOADER_STUB_DIR}"
    COMMAND "${NASM_EXECUTABLE}" -f bin
            "-i${CMAKE_CURRENT_SOURCE_DIR}/"
            -o "${LOADER_STUB_BIN}"
            "${CMAKE_CURRENT_SOURCE_DIR}/runtime_loader_stub.asm"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_FILE=${LOADER_STUB_BIN}"
            "-DOUTPUT_C=${LOADER_STUB_C}"
            "-DOUTPUT_H=${LOADER_STUB_H}"
            -P "${CMAKE_SOURCE_DIR}/cmake/BinToC.cmake"
    DEPENDS runtime_loader_stub.asm
            runtime_loader_stub.inc
            "${CMAKE_SOURCE_DIR}/cmake/BinToC.cmake"
    VERBATIM
)
```

把 `add_executable` 改为（新增最后一行源文件）：

```cmake
add_executable(CastlePatches WIN32
    main.c
    CastlePatches.rc
    "${CMAKE_CURRENT_BINARY_DIR}/CastlePatchesVersion.rc"
    "${LOADER_STUB_C}"
)
```

在 `target_compile_definitions(CastlePatches ...)` 之后任意位置加：

```cmake
target_include_directories(CastlePatches PRIVATE "${LOADER_STUB_DIR}")
```

- [ ] **Step 5: 构建并审阅反汇编**

执行「验证基础命令」的两条构建命令。然后：

```bash
ndisasm -b32 build-win32/loader_stub/runtime_loader_stub.bin
```

预期：构建成功；反汇编输出与 `runtime_loader_stub.asm` 逐条对应，重点核对：

1. 序言为 `push ebp; mov ebp,esp; push ebx; push esi; mov ebx,[ebp+8]; mov esi,0x190`（400）。
2. 两处 `mov eax,[0x4600c8]` / 三处 `mov eax,[0x460090]`（IAT 常量已烘焙）。
3. 每处 IAT 调用前都有 `cmp eax,-1` 与 `test eax,eax` 两道检查。
4. 四个 `lea ecx,[ebx+0x0]/[ebx+0x220]/[ebx+0x240]/[ebx+0x260]` + `push ecx`。
5. 三处 `call eax` 间接调用 + 最后一处 `CastleRuntimeStart` 调用前有 `test eax,eax; je`。
6. 两条出口路径均为 `pop esi; pop ebx; pop ebp; ret 4`；重试耗尽路径先 `xor eax,eax`。
7. 自旋循环为 `dec esi; jz …; pause; mov ecx,0x10000; pause; dec ecx; jnz …; jmp …`。

blob 大小应约 150 字节，小于旧的 0x1000 预留。

- [ ] **Step 6: 提交**

```bash
git add src/CastlePatches/runtime_loader_stub.asm src/CastlePatches/runtime_loader_stub.inc cmake/BinToC.cmake src/CastlePatches/CMakeLists.txt
git commit -m "feat: assemble the remote loader stub with NASM and embed it as a C array"
```

---

### Task 3: 一次性差分验证（临时产物，不提交）

此时旧 C 发射器仍在 `main.c` 中，新 blob 已生成，是两者对比的唯一窗口。

- [ ] **Step 1: 搭建临时 harness**

```bash
mkdir -p "$TEMP/castle_stub_diff"
```

创建 `$TEMP/castle_stub_diff/harness.c`：头部如下，随后**从当前 main.c 原样复制**第 42-46 行（IAT/capacity 枚举）、第 391-470 行（EmitByte 等全部辅助函数）、第 481-487 行（RUNTIME_STUB_* 枚举）、第 489-647 行（BuildRuntimeLoaderStub 整个函数）：

```c
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Paste the four regions from main.c here, verbatim. */

int main(void) {
    uint8_t buffer[REMOTE_HOOK_CAPACITY] = {0};
    size_t size = BuildRuntimeLoaderStub(buffer, 0x00100000u, 0x00020000u);
    if (size == 0) {
        fprintf(stderr, "emitter overflow\n");
        return 1;
    }
    FILE *out = fopen("legacy_stub.bin", "wb");
    if (out == NULL) return 1;
    fwrite(buffer, 1, size, out);
    fclose(out);
    printf("legacy stub: %zu bytes\n", size);
    return 0;
}
```

创建 `$TEMP/castle_stub_diff/CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.20)
project(stub_harness C)
set(CMAKE_C_STANDARD 11)
add_executable(harness harness.c)
```

- [ ] **Step 2: 编译运行，对比反汇编**

```bash
cd "$TEMP/castle_stub_diff"
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 && cmake --build build --config Release
./build/Release/harness.exe
ndisasm -b32 legacy_stub.bin > legacy.txt
ndisasm -b32 /e/Projects/DynastyPatches/build-win32/loader_stub/runtime_loader_stub.bin > new.txt
diff legacy.txt new.txt
```

预期 diff **只**包含以下三类差异，其余逐条一致：

1. 新 blob 序言多 `push ebx` 与 `mov ebx,[ebp+8]`，两条出口各多一个 `pop ebx`。
2. 旧 blob 的四处 `push dword 0x20000/0x20220/0x20240/0x20260`（= data_address + 各偏移）在新 blob 中变为 `lea ecx,[ebx+0x0/0x220/0x240/0x260]` + `push ecx`。
3. 跳转编码可能有 short↔near 差异（旧代码尽量收缩为 short；NASM 默认前向跳转用 near），操作数相对位置保持等价。

若出现任何其他差异，停下来排查，不要继续 Task 4。

- [ ] **Step 3: 清理临时产物**

```bash
rm -rf "$TEMP/castle_stub_diff"
```

不提交任何内容。

---

### Task 4: main.c 切换为嵌入 blob，删除发射器

**Files:**
- Modify: `src/CastlePatches/main.c`（第 15-19 行注释保留；第 42-46 行、391-470 行、481-487 行、489-647 行、666-733 行区域）

- [ ] **Step 1: 删除发射器与已迁移的常量**

依次删除（删除前用 grep 确认 `REMOTE_HOOK_CAPACITY`、`GET_MODULE_HANDLE_IAT_ADDRESS`、`GET_PROC_ADDRESS_IAT_ADDRESS` 只出现在这些区域）：

1. 第 42-46 行的整个枚举（两个 IAT 地址已迁入 `runtime_loader_stub.inc`，`REMOTE_HOOK_CAPACITY` 由 blob 大小取代）。
2. 第 391-409 行的 `g_emit_overflow`、`EmitByte`、`EmitDword`。
3. 第 411-465 行的 `PatchNearJump`、`PatchConditionalJump`。
4. 第 467-470 行的 `EmitPushImm`。
5. 第 489-647 行的 `BuildRuntimeLoaderStub`。

保留第 481-487 行的 `RUNTIME_STUB_*` 枚举，并在其上方加注释：

```c
/* Keep these offsets in sync with the STUB_* constants in
   runtime_loader_stub.inc; the stub reads its strings from this layout. */
```

- [ ] **Step 2: 引入 blob 头文件**

在第 9 行 `#include <wchar.h>` 之后加：

```c
#include "runtime_loader_stub_blob.h"
```

- [ ] **Step 3: 改写 InjectRuntimeDll 的代码写入与线程创建**

把原来的代码分配（第 671-675 行）改为按 blob 实际大小分配：

```c
    LPVOID remote_code = VirtualAllocEx(process,
                                        NULL,
                                        kRuntimeLoaderStubSize,
                                        MEM_RESERVE | MEM_COMMIT,
                                        PAGE_READWRITE);
```

把原来的代码构建与写入块（原第 706-714 行）：

```c
    uint8_t code[REMOTE_HOOK_CAPACITY] = {0};
    size_t code_size = BuildRuntimeLoaderStub(code, code_address, data_address);
    if (code_size == 0 ||
        !WriteProcessMemory(process, remote_code, code, code_size, &written) || written != code_size) {
        VirtualFreeEx(process, remote_data, 0, MEM_RELEASE);
        VirtualFreeEx(process, remote_code, 0, MEM_RELEASE);
        if (code_size == 0) SetLastError(ERROR_BUFFER_OVERFLOW);
        return false;
    }
```

替换为：

```c
    if (!WriteProcessMemory(process, remote_code, kRuntimeLoaderStub, kRuntimeLoaderStubSize, &written) ||
        written != kRuntimeLoaderStubSize) {
        VirtualFreeEx(process, remote_data, 0, MEM_RELEASE);
        VirtualFreeEx(process, remote_code, 0, MEM_RELEASE);
        return false;
    }
```

把 `VirtualProtectEx`（原第 716-720 行）的长度参数改为 `kRuntimeLoaderStubSize`，`FlushInstructionCache`（第 725 行）同样改为 `kRuntimeLoaderStubSize`。

把 `CreateRemoteThread`（原第 727-733 行）改为传数据块地址：

```c
    /* Pass the data block as lpParameter; the stub reads it from [ebp+8]. */
    HANDLE thread = CreateRemoteThread(process,
                                       NULL,
                                       0,
                                       (LPTHREAD_START_ROUTINE)(uintptr_t)code_address,
                                       remote_data,
                                       0,
                                       NULL);
```

- [ ] **Step 4: 构建验证**

执行「验证基础命令」的两条构建命令。
预期：均成功，无警告（MSVC 为 /W4）；`main.c` 中不再出现 `EmitByte`、`0xE9`、`0xCC` 等发射痕迹。

- [ ] **Step 5: 提交**

```bash
git add src/CastlePatches/main.c
git commit -m "refactor: replace the byte-by-byte loader stub emitter with the embedded NASM blob"
```

---

### Task 5: 文档更新

**Files:**
- Modify: `AGENTS.md:31-33`
- Modify: `README.md:35`

- [ ] **Step 1: 更新 AGENTS.md 汇编规则**

把：

```
- Put injected x86 instruction blocks in standalone MASM/NASM sources compiled by `ml` or `nasm`; do not assemble instruction bytes in C.
- If an x86 block must be emitted by an x64 launcher and cannot be built as an assembly object, format the byte sequence by instruction and annotate every instruction in the source.
```

替换为：

```
- Put injected x86 instruction blocks in standalone NASM sources; do not assemble instruction bytes in C. The runtime DLL links its NASM object directly; the launcher's remote loader stub is assembled at build time with `nasm -f bin` and embedded as a generated C array, so launchers of either bitness share the same code path.
```

- [ ] **Step 2: 更新 README 构建前置要求**

把 `README.md` 第 35 行：

```
Visual Studio 构建使用 `ml.exe` 编译 runtime 的 x86 汇编；使用其他 CMake 工具链时需要提供 `nasm`。
```

替换为：

```
所有工具链都使用 [NASM](https://www.nasm.us/) 编译 runtime 的 x86 汇编和启动器内嵌的加载 stub；可通过 `winget install nasm` 或 `scoop install nasm` 安装。
```

- [ ] **Step 3: 提交**

```bash
git add AGENTS.md README.md
git commit -m "docs: require NASM on every toolchain and drop the ml.exe path"
```

---

### Task 6: CI 覆盖

**Files:**
- Modify: `.github/workflows/release-castle.yml`（checkout 步骤之后）
- Create: `.github/workflows/ci-mingw.yml`

- [ ] **Step 1: release 工作流保障 nasm**

在 `release-castle.yml` 的 `Validate release tag` 步骤之前插入：

```yaml
      - name: Ensure NASM
        shell: pwsh
        run: |
          if (-not (Get-Command nasm -ErrorAction SilentlyContinue)) {
            choco install nasm -y
          }
          nasm -v
```

- [ ] **Step 2: 新建 MinGW 构建工作流**

新建 `.github/workflows/ci-mingw.yml`：

```yaml
name: Build CastlePatches (MinGW)

on:
  push:
    branches: [master]
  workflow_dispatch:

permissions:
  contents: read

jobs:
  build-mingw:
    runs-on: windows-latest
    steps:
      - name: Checkout source
        uses: actions/checkout@v4

      - name: Set up MSYS2 (MINGW32)
        uses: msys2/setup-msys2@v2
        with:
          msystem: MINGW32
          update: true
          install: >-
            mingw-w64-i686-gcc
            mingw-w64-i686-cmake
            mingw-w64-i686-ninja
            nasm

      - name: Configure MinGW build
        shell: msys2 {0}
        run: cmake -S . -B build-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release

      - name: Build MinGW
        shell: msys2 {0}
        run: cmake --build build-mingw --parallel
```

i686 工具链原生为 32 位，外层构建向 runtime 子构建转发的 `-m32` 对其兼容；
该任务同时覆盖 GNU 路径与 NASM 链接，防止单一路径再次失去检验。

- [ ] **Step 3: 提交**

```bash
git add .github/workflows/release-castle.yml .github/workflows/ci-mingw.yml
git commit -m "ci: build the MinGW variant and ensure NASM on the release runner"
```

注意：本机无 MinGW，该工作流无法在本地预演；合并后在下一次 push 到 master 时观察结果，若失败按日志修正（最常见为 MSYS2 包名或 PATH 问题）。

---

### Task 7: 最终全量验证

- [ ] **Step 1: 干净环境全量构建**

```bash
rm -rf build-win32 build-x64
cmake -S . -B build-win32 -G "Visual Studio 18 2026" -A Win32 && cmake --build build-win32 --config Release --parallel
cmake -S . -B build-x64 -G "Visual Studio 18 2026" -A x64 && cmake --build build-x64 --config Release --parallel
```

预期：从零 configure 到链接全部成功；两个架构的 `CastlePatches.exe` 与
`CastleRuntime.dll` 均产出（DLL 始终为 32 位）。

- [ ] **Step 2: 残留检查**

```bash
grep -rn "EmitByte\|EmitDword\|PatchNearJump\|PatchConditionalJump\|REMOTE_HOOK_CAPACITY\|ASM_MASM\|runtime_hooks_masm" src cmake AGENTS.md README.md
```

预期：无输出（全部清除）。

- [ ] **Step 3: 交付用户实机验证**

按 AGENTS.md 不自动启动游戏。告知用户：将 `build-win32/bin/Release/`
下的 `CastlePatches.exe` 与 `CastleRuntime.dll` 放到游戏目录，勾选任一
需要运行时的功能（如窗口模式）启动游戏，确认补丁生效且无不遇敌/崩溃
回归；重点验证注入路径（loader stub）与 frame pacing 两个 hook
（此前 NASM global 缺失，MinGW 下从未成功链接过）。

---

## Self-Review 记录

- Spec 覆盖：设计的四部分（stub 汇编化/blob 嵌入/hook 单文件化/文档）
  分别对应 Task 2+4、Task 2、Task 1、Task 5；验证五项对应各 Task 的构建
  验证、Task 3 差分、Task 2 Step 5 反汇编审阅、Task 6 CI。无缺口。
- 占位符扫描：harness 的「从 main.c 复制四个区域」给出了精确行号范围，
  属有意的一次性操作说明，非占位符。其余代码均完整给出。
- 类型一致性：`kRuntimeLoaderStub` / `kRuntimeLoaderStubSize` 在
  BinToC.cmake（生成端）与 main.c（消费端）拼写一致；
  `runtime_loader_stub_blob.h` 文件名在生成端与 include 端一致；
  stub 内符号（retry_loop/retry/spin_inner/exhausted/finish）自洽。
