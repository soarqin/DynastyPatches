#include <windows.h>
#include "../launcher/launcher_ui.h"
#include <commdlg.h>

#include "file_identity.h"
#include "swordman.h"
#include "swordman_addresses.h"
#include "runtime_loader_stub_blob.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

enum { BASIC, COMBAT, DISPLAY };
enum {
    ID_NO_CD, ID_CHESTS, ID_SUMMON_ITEM_COPY, ID_SAVE_ANYTIME,
    ID_SPEED, ID_CAST_AFTER_MOVE, ID_ATTRIBUTES, ID_GROWTH,
    ID_RESIZABLE, ID_SCALE, ID_MOVIE, ID_SOFTWARE_MOVIE, ID_SMOOTH, ID_CLEANUP
};

static const LauncherPage pages[] = {
    {L"基础补丁", L"读取使用「随时中途存档」保存的进度时，也需开启此选项。"},
    {L"战斗与成长", L"成长方式决定角色升级时的属性增长。"},
    {L"画面设置", L"初始窗口分辨率独立于窗口模式。开启电影缩放后可选择软件电影解码。"},
};

static const wchar_t *const scales[] = {
    L"1×（640 × 400）", L"1.5×（960 × 600）", L"2×（1280 × 800）",
    L"2.5×（1600 × 1000）", L"3×（1920 × 1200）",
};
static const wchar_t *const growth_modes[] = {L"正常成长", L"最大成长", L"最小成长"};
static const LauncherRange scale_range = {2, 6, scales};
static const LauncherRange growth_range = {SWORDMAN_GROWTH_NORMAL, SWORDMAN_GROWTH_MINIMUM, growth_modes};

static LauncherOption settings[] = {
    [ID_NO_CD] = {BASIC, L"免 CD", L"Patches", L"NoCd"},
    [ID_CHESTS] = {BASIC, L"显示全部宝箱", L"Patches", L"Chests"},
    [ID_SUMMON_ITEM_COPY] = {BASIC, L"恢复召唤兽复制物品特性", L"Patches", L"SummonItemCopy"},
    [ID_SAVE_ANYTIME] = {BASIC, L"随时中途存档", L"Patches", L"SaveAnytime"},
    [ID_SPEED] = {COMBAT, L"加速行动", L"Patches", L"Speed"},
    [ID_CAST_AFTER_MOVE] = {COMBAT, L"移动后可施法", L"Patches", L"CastAfterMove"},
    [ID_ATTRIBUTES] = {COMBAT, L"属性上限 999", L"Patches", L"Attributes"},
    [ID_GROWTH] = {COMBAT, L"成长方式", L"Patches", L"Growth", SWORDMAN_GROWTH_NORMAL, &growth_range},
    [ID_RESIZABLE] = {DISPLAY, L"窗口模式", L"Patches", L"Resizable"},
    [ID_SCALE] = {DISPLAY, L"初始窗口分辨率", L"Display", L"Scale2", 2, &scale_range},
    [ID_MOVIE] = {DISPLAY, L"电影缩放", L"Patches", L"Movie"},
    [ID_SOFTWARE_MOVIE] = {DISPLAY, L"软件电影解码", L"Patches", L"SoftwareMovie", 0, NULL, &settings[ID_MOVIE], true},
    [ID_SMOOTH] = {DISPLAY, L"平滑电影画面", L"Patches", L"Smooth"},
    [ID_CLEANUP] = {DISPLAY, L"电影播放清理", L"Patches", L"MovieCleanup"},
};

enum {
    STUB_PATH = 0x00,
    STUB_KERNEL = 0x200,
    STUB_LOAD = 0x220,
    STUB_START = 0x240,
    STUB_OPTIONS = 0x260,
    STUB_GETMODULE = 0x26c,
    STUB_GETPROC = 0x270,
    STUB_DATA_SIZE = 0x280,
};

_Static_assert(STUB_OPTIONS + sizeof(SwordmanOptions) <= STUB_GETMODULE,
               "Loader options must not overlap API pointers");

static const wchar_t kConfigFileName[] = L"SwordmanPatches.ini";

static bool ExistingFile(const wchar_t *path) {
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static bool LauncherDirectory(wchar_t *path, size_t capacity) {
    DWORD length = GetModuleFileNameW(NULL, path, (DWORD)capacity);
    if (length == 0 || length >= capacity) return false;
    wchar_t *slash = wcsrchr(path, L'\\');
    if (slash == NULL) return false;
    slash[1] = L'\0';
    return true;
}

static bool ConfigPath(wchar_t *path, size_t capacity) {
    if (!LauncherDirectory(path, capacity)) return false;
    return wcscat_s(path, capacity, kConfigFileName) == 0;
}

static bool ResolveRelativeGamePath(wchar_t *path, size_t capacity) {
    static const wchar_t *const candidates[] = {
        L".\\Sm_win2.exe",
        L".\\exe\\Sm_win2.exe",
        L".\\Swordman\\Sm_win2.exe",
        L".\\exe\\Swordman\\Sm_win2.exe",
    };

    for (size_t index = 0; index < _countof(candidates); ++index) {
        DWORD length = GetFullPathNameW(candidates[index], (DWORD)capacity, path, NULL);
        if (length == 0 || length >= capacity) continue;
        if (ExistingFile(path)) return true;
    }
    return false;
}

static bool LoadSavedGamePath(wchar_t *path, size_t capacity) {
    wchar_t config_path[MAX_PATH];
    if (!ConfigPath(config_path, _countof(config_path))) return false;
    DWORD length = GetPrivateProfileStringW(L"Game", L"Path", L"", path,
                                            (DWORD)capacity, config_path);
    return length > 0 && length < capacity && ExistingFile(path);
}

static void SaveGamePath(const wchar_t *path) {
    wchar_t config_path[MAX_PATH];
    if (ConfigPath(config_path, _countof(config_path))) {
        WritePrivateProfileStringW(L"Game", L"Path", path, config_path);
    }
}

static bool ReadWholeFile(const wchar_t *path, uint8_t **data, size_t *size) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER length;
    bool success = GetFileSizeEx(file, &length) && length.QuadPart > 0 &&
                   (uint64_t)length.QuadPart <= SIZE_MAX;
    uint8_t *copy = NULL;
    if (success) {
        copy = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)length.QuadPart);
        DWORD read = 0;
        success = copy != NULL && ReadFile(file, copy, (DWORD)length.QuadPart, &read, NULL) &&
                  read == (DWORD)length.QuadPart;
    }
    CloseHandle(file);
    if (!success) {
        HeapFree(GetProcessHeap(), 0, copy);
        return false;
    }
    *data = copy;
    *size = (size_t)length.QuadPart;
    return true;
}

static const uint8_t *RvaToFile(const uint8_t *file, size_t size,
                                const IMAGE_NT_HEADERS32 *nt, uint32_t rva) {
    const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        uint32_t start = section[index].VirtualAddress;
        uint32_t span = section[index].SizeOfRawData > section[index].Misc.VirtualSize
                            ? section[index].SizeOfRawData : section[index].Misc.VirtualSize;
        if (rva >= start && rva - start < span) {
            uint64_t offset = (uint64_t)section[index].PointerToRawData + rva - start;
            return offset < size ? file + offset : NULL;
        }
    }
    return NULL;
}

static bool FindImportSlot(const wchar_t *path, const char *module, const char *name,
                           uint32_t *slot_address) {
    uint8_t *file = NULL;
    size_t size = 0;
    bool found = false;
    if (!ReadWholeFile(path, &file, &size)) return false;
    if (size < sizeof(IMAGE_DOS_HEADER)) goto done;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)file;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        (size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS32) > size) goto done;
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(file + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.ImageBase != SWORDMAN_IMAGE_BASE) goto done;
    IMAGE_DATA_DIRECTORY directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    const IMAGE_IMPORT_DESCRIPTOR *imports = (const IMAGE_IMPORT_DESCRIPTOR *)RvaToFile(file, size, nt, directory.VirtualAddress);
    if (imports == NULL) goto done;
    for (; imports->Name != 0; ++imports) {
        const char *dll = (const char *)RvaToFile(file, size, nt, imports->Name);
        if (dll == NULL || _stricmp(dll, module) != 0) continue;
        uint32_t names_rva = imports->OriginalFirstThunk != 0 ? imports->OriginalFirstThunk : imports->FirstThunk;
        const IMAGE_THUNK_DATA32 *names = (const IMAGE_THUNK_DATA32 *)RvaToFile(file, size, nt, names_rva);
        if (names == NULL) goto done;
        for (uint32_t index = 0; names[index].u1.AddressOfData != 0; ++index) {
            if (IMAGE_SNAP_BY_ORDINAL32(names[index].u1.Ordinal)) continue;
            const IMAGE_IMPORT_BY_NAME *entry = (const IMAGE_IMPORT_BY_NAME *)RvaToFile(file, size, nt, names[index].u1.AddressOfData);
            if (entry != NULL && strcmp((const char *)entry->Name, name) == 0) {
                *slot_address = SWORDMAN_IMAGE_BASE + imports->FirstThunk + index * sizeof(uint32_t);
                found = true;
                goto done;
            }
        }
    }
done:
    HeapFree(GetProcessHeap(), 0, file);
    return found;
}

static bool CreateGame(const wchar_t *path, PROCESS_INFORMATION *info) {
    wchar_t directory[MAX_PATH];
    if (wcsncpy_s(directory, _countof(directory), path, _TRUNCATE) != 0) return false;
    wchar_t *slash = wcsrchr(directory, L'\\');
    if (slash == NULL) return false;
    *slash = L'\0';
    STARTUPINFOW startup = {sizeof(startup)};
    ZeroMemory(info, sizeof(*info));
    return CreateProcessW(path, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, directory,
                          &startup, info) != FALSE;
}

static bool InjectRuntime(HANDLE process, const wchar_t *dll_path,
                          const SwordmanOptions *options, const wchar_t *game_path) {
    uint32_t get_module_slot, get_proc_slot;
    if (!FindImportSlot(game_path, "KERNEL32.dll", "GetModuleHandleA", &get_module_slot) ||
        !FindImportSlot(game_path, "KERNEL32.dll", "GetProcAddress", &get_proc_slot)) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return false;
    }
    uint8_t data[STUB_DATA_SIZE] = {0};
    size_t path_bytes = (wcslen(dll_path) + 1u) * sizeof(wchar_t);
    if (path_bytes > STUB_KERNEL) {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return false;
    }
    memcpy(data + STUB_PATH, dll_path, path_bytes);
    memcpy(data + STUB_KERNEL, "kernel32.dll", sizeof("kernel32.dll"));
    memcpy(data + STUB_LOAD, "LoadLibraryW", sizeof("LoadLibraryW"));
    memcpy(data + STUB_START, "SwordmanRuntimeStart", sizeof("SwordmanRuntimeStart"));
    memcpy(data + STUB_OPTIONS, options, sizeof(*options));
    *(uint32_t *)(data + STUB_GETMODULE) = get_module_slot;
    *(uint32_t *)(data + STUB_GETPROC) = get_proc_slot;

    LPVOID remote_data = VirtualAllocEx(process, NULL, sizeof(data), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    LPVOID remote_code = VirtualAllocEx(process, NULL, kRuntimeLoaderStubSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (remote_data == NULL || remote_code == NULL || (uintptr_t)remote_code > UINT32_MAX) {
        if (remote_data != NULL) VirtualFreeEx(process, remote_data, 0, MEM_RELEASE);
        if (remote_code != NULL) VirtualFreeEx(process, remote_code, 0, MEM_RELEASE);
        SetLastError(ERROR_NOT_SUPPORTED);
        return false;
    }
    SIZE_T written;
    bool success = WriteProcessMemory(process, remote_data, data, sizeof(data), &written) && written == sizeof(data) &&
                   WriteProcessMemory(process, remote_code, kRuntimeLoaderStub, kRuntimeLoaderStubSize, &written) &&
                   written == kRuntimeLoaderStubSize;
    DWORD old_protection;
    success = success && VirtualProtectEx(process, remote_code, kRuntimeLoaderStubSize, PAGE_EXECUTE_READ, &old_protection);
    FlushInstructionCache(process, remote_code, kRuntimeLoaderStubSize);
    HANDLE thread = success ? CreateRemoteThread(process, NULL, 0,
        (LPTHREAD_START_ROUTINE)(uintptr_t)remote_code, remote_data, 0, NULL) : NULL;
    if (thread == NULL) {
        VirtualFreeEx(process, remote_data, 0, MEM_RELEASE);
        VirtualFreeEx(process, remote_code, 0, MEM_RELEASE);
        return false;
    }
    DWORD wait = WaitForSingleObject(thread, 10000), code = 0;
    bool thread_done = wait == WAIT_OBJECT_0;
    if (!thread_done) {
        /* The stub may still be executing from remote_code. Kill the new
           process and wait for that thread before releasing its mappings. */
        TerminateProcess(process, 1);
        thread_done = WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0;
    }
    bool got_exit_code = thread_done && GetExitCodeThread(thread, &code);
    success = got_exit_code && code == ERROR_SUCCESS;
    DWORD error = success ? ERROR_SUCCESS : (wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_DLL_INIT_FAILED);
    if (got_exit_code && code != ERROR_SUCCESS) error = code;
    CloseHandle(thread);
    if (thread_done) {
        VirtualFreeEx(process, remote_data, 0, MEM_RELEASE);
        VirtualFreeEx(process, remote_code, 0, MEM_RELEASE);
    }
    SetLastError(error);
    return success;
}

static void ShowError(HWND owner, const wchar_t *action) {
    DWORD error = GetLastError();
    wchar_t system_message[512] = L"";
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, error, 0, system_message, _countof(system_message), NULL);
    wchar_t message[768];
    _snwprintf_s(message, _countof(message), _TRUNCATE,
                 L"%ls失败（错误 %lu）：%ls", action, (unsigned long)error, system_message);
    MessageBoxW(owner, message, L"天地劫：神魔至尊传补丁工具", MB_OK | MB_ICONERROR);
}

static bool SelectGamePath(HWND owner, wchar_t *path, size_t capacity) {
    if (LoadSavedGamePath(path, capacity) || ResolveRelativeGamePath(path, capacity)) {
        return true;
    }

    path[0] = L'\0';
    OPENFILENAMEW dialog = {sizeof(dialog)};
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"Sm_win2.exe\0Sm_win2.exe\0可执行文件\0*.exe\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = (DWORD)capacity;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) return false;

    wchar_t full_path[MAX_PATH];
    DWORD length = GetFullPathNameW(path, _countof(full_path), full_path, NULL);
    if (length == 0 || length >= _countof(full_path) || !ExistingFile(full_path)) return false;
    if (wcsncpy_s(path, capacity, full_path, _TRUNCATE) != 0) return false;
    SaveGamePath(path);
    return true;
}

static SwordmanOptions ReadOptions(void) {
    static const uint32_t flags[] = {
        [ID_NO_CD] = SWORDMAN_NO_CD, [ID_CHESTS] = SWORDMAN_CHESTS,
        [ID_SUMMON_ITEM_COPY] = SWORDMAN_SUMMON_ITEM_COPY, [ID_SAVE_ANYTIME] = SWORDMAN_SAVE_ANYTIME,
        [ID_SPEED] = SWORDMAN_SPEED, [ID_CAST_AFTER_MOVE] = SWORDMAN_CAST_AFTER_MOVE,
        [ID_ATTRIBUTES] = SWORDMAN_ATTRIBUTE_999, [ID_RESIZABLE] = SWORDMAN_RESIZABLE,
        [ID_MOVIE] = SWORDMAN_MOVIE, [ID_SOFTWARE_MOVIE] = SWORDMAN_SOFTWARE_MOVIE,
        [ID_SMOOTH] = SWORDMAN_SMOOTH, [ID_CLEANUP] = SWORDMAN_MOVIE_CLEANUP,
    };
    SwordmanOptions options = {.growth = settings[ID_GROWTH].value, .scale2 = settings[ID_SCALE].value};
    for (size_t i = 0; i < _countof(flags); ++i) {
        if (settings[i].value) options.flags |= flags[i];
    }
    return options;
}

static void Launch(HWND owner) {
    wchar_t path[MAX_PATH];
    if (!SelectGamePath(owner, path, _countof(path))) {
        LauncherSetStatus(L"未选择 Sm_win2.exe。");
        return;
    }

    SwordmanOptions options = ReadOptions();
    LauncherSaveOptions();
    if (!SwordmanHashFile(path, kSwordmanImageHash)) {
        SetLastError(ERROR_REVISION_MISMATCH);
        ShowError(owner, L"校验 Sm_win2.exe");
        return;
    }

    wchar_t directory[MAX_PATH], dll_path[MAX_PATH];
    if (!LauncherDirectory(directory, _countof(directory)) ||
        wcscpy_s(dll_path, _countof(dll_path), directory) != 0 ||
        wcscat_s(dll_path, _countof(dll_path), L"SwordmanRuntime.dll") != 0 ||
        !ExistingFile(dll_path)) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        ShowError(owner, L"查找 SwordmanRuntime.dll");
        return;
    }

    PROCESS_INFORMATION info;
    if (!CreateGame(path, &info)) {
        ShowError(owner, L"创建已挂起的游戏进程");
        return;
    }
    bool success = InjectRuntime(info.hProcess, dll_path, &options, path);
    if (success && ResumeThread(info.hThread) != (DWORD)-1) {
        wchar_t status[160];
        _snwprintf_s(status, _countof(status), _TRUNCATE,
                     L"已向 PID %lu 写入所选补丁并启动游戏。原始 Sm_win2.exe 未被修改。",
                     (unsigned long)info.dwProcessId);
        LauncherSetStatus(status);
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
        return;
    }

    DWORD error = GetLastError();
    TerminateProcess(info.hProcess, 1);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    SetLastError(error);
    ShowError(owner, L"注入 SwordmanRuntime.dll");
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show) {
    (void)previous;
    (void)command_line;
    static const LauncherSpec launcher = {
        L"SwordmanPatchesWindow", L"《天地劫：神魔至尊传》补丁工具", L"神魔至尊传",
        L"SwordmanPatches  /  v" LAUNCHER_VERSION, pages, _countof(pages), settings, _countof(settings),
        ConfigPath, Launch,
    };
    return LauncherRun(instance, show, &launcher);
}
