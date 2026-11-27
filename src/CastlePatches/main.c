#include <windows.h>
#include "../launcher/launcher_ui.h"
#include <commdlg.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "runtime_loader_stub_blob.h"
#include "window_scale.h"

#ifndef _countof
#define _countof(array) (sizeof(array) / sizeof((array)[0]))
#endif

/* The target executable and every injected instruction block are 32-bit x86.
   Keep patch addresses and byte counts explicitly 32-bit as well: using
   pointer-sized fields here makes the launcher data model-dependent when the
   launcher itself is built for x64.  The Win32 APIs still receive SIZE_T after
   the value has been widened at the call boundary. */
#define PATCH_LENGTH(array) ((uint32_t)(sizeof(array)))

enum { BASIC, RULES, DISPLAY };
enum {
    ID_NO_CD, ID_MINGYU_FIX, ID_RESISTANCE_FIX, ID_ANYWHERE_SAVE,
    ID_MAX_GROWTH, ID_MAX_LOOT, ID_EXP_MULTIPLIER, ID_MONEY_MULTIPLIER, ID_DYNAMIC_ENCOUNTER_RATE,
    ID_WINDOWED, ID_SCALE, ID_CURSOR_LOCK, ID_WIDESCREEN
};

static const LauncherPage pages[] = {
    {L"基础补丁", L"启动游戏时，所选补丁会写入本次游戏进程。"},
    {L"成长与遇敌", L"动态遇敌率热键：Ctrl+F10 恢复 100%，Ctrl+F11 降低，Ctrl+F12 提高。"},
    {L"画面设置", L"开启窗口模式后，可调整窗口倍率、锁定鼠标或启用宽屏地图。"},
};

static const wchar_t *const scales[] = {
    L"1×（640 × 480）", L"1.5×（960 × 720）", L"2×（1280 × 960）",
    L"2.25×（1440 × 1080）", L"2.5×（1600 × 1200）", L"3×（1920 × 1440）",
};
static const uint32_t scale_ini_values[] = {2, 3, 4, CASTLE_SCALE2_225, 5, 6};
_Static_assert(_countof(scales) == _countof(scale_ini_values), "Each scale needs an INI value");
static const LauncherRange scale_range = {0, _countof(scales) - 1, scales, scale_ini_values};
static const LauncherRange multiplier_range = {1, 10, NULL};

static LauncherOption settings[] = {
    [ID_NO_CD] = {BASIC, L"免 CD", L"Patches", L"NoCd", 1},
    [ID_MINGYU_FIX] = {BASIC, L"修复「冥狱杀阵」可被习得", L"Patches", L"MingyuFix", 1},
    [ID_RESISTANCE_FIX] = {BASIC, L"修复菜单后的抗性显示与实际值不一致", L"Patches", L"ResistanceFix", 1},
    [ID_ANYWHERE_SAVE] = {BASIC, L"随时存档", L"Patches", L"AnywhereSave", 1},
    [ID_MAX_GROWTH] = {RULES, L"最大成长", L"Patches", L"MaxGrowth"},
    [ID_MAX_LOOT] = {RULES, L"最大掉宝", L"Patches", L"MaxLoot"},
    [ID_EXP_MULTIPLIER] = {RULES, L"经验倍率", L"Patches", L"ExperienceMultiplier", 1, &multiplier_range},
    [ID_MONEY_MULTIPLIER] = {RULES, L"金钱倍率", L"Patches", L"MoneyMultiplier", 1, &multiplier_range},
    [ID_DYNAMIC_ENCOUNTER_RATE] = {RULES, L"动态调整遇敌率", L"Patches", L"DynamicEncounterRate"},
    [ID_WINDOWED] = {DISPLAY, L"窗口模式（修正鼠标坐标）", L"Display", L"Windowed"},
    [ID_SCALE] = {DISPLAY, L"窗口倍率", L"Display", L"Scale2", 0, &scale_range, &settings[ID_WINDOWED]},
    [ID_CURSOR_LOCK] = {DISPLAY, L"锁定鼠标在窗口内", L"Display", L"CursorLock", 0, NULL, &settings[ID_WINDOWED]},
    [ID_WIDESCREEN] = {DISPLAY, L"宽屏地图（864 × 480）", L"Display", L"Widescreen", 0, NULL, &settings[ID_WINDOWED]},
};

typedef struct PatchSite {
    uint32_t address;
    const uint8_t *expected;
    const uint8_t *replacement;
    uint32_t length;
} PatchSite;

typedef struct PatchGroup {
    const wchar_t *name;
    const PatchSite *sites;
    size_t count;
} PatchGroup;

typedef struct LaunchOptions {
    bool no_cd;
    bool mingyu_fix;
    bool resistance_fix;
    bool anywhere_save;
    bool max_growth;
    bool max_loot;
    bool windowed;
    bool cursor_lock;
    bool widescreen;
    uint32_t experience_multiplier;
    uint32_t money_multiplier;
    bool dynamic_encounter_rate;
} LaunchOptions;

static DWORD g_runtime_start_result;

static const wchar_t kConfigFileName[] = L"CastlePatches.ini";

static const uint8_t kNoCdDriveExpected[] = {0x83, 0xF8, 0x05, 0x75};
static const uint8_t kNoCdDriveReplacement[] = {0x83, 0xF8, 0x02, 0x7C};
static const uint8_t kNoCdAccessExpected[] = {0x74, 0x2E};
static const uint8_t kNoCdAccessReplacement[] = {0xEB, 0x2E};
static const PatchSite kNoCdSites[] = {
    {0x00402A3Fu, kNoCdDriveExpected, kNoCdDriveReplacement, PATCH_LENGTH(kNoCdDriveExpected)},
    {0x00402A62u, kNoCdAccessExpected, kNoCdAccessReplacement, PATCH_LENGTH(kNoCdAccessExpected)},
};

static const uint8_t kMingyuExpected[] = {0x83, 0xFE, 0x28};
static const uint8_t kMingyuReplacement[] = {0x83, 0xFE, 0x30};
static const PatchSite kMingyuSites[] = {
    {0x00423590u, kMingyuExpected, kMingyuReplacement, PATCH_LENGTH(kMingyuExpected)},
};

static const uint8_t kResistance1Expected[] = {0x89, 0x54, 0x39, 0x74};
static const uint8_t kResistance1Replacement[] = {0x89, 0x54, 0x39, 0x70};
static const uint8_t kResistance2Expected[] = {0x89, 0x54, 0x39, 0x78};
static const uint8_t kResistance2Replacement[] = {0x89, 0x54, 0x39, 0x74};
static const uint8_t kResistance3Expected[] = {0x89, 0x54, 0x39, 0x7C};
static const uint8_t kResistance3Replacement[] = {0x89, 0x54, 0x39, 0x78};
static const uint8_t kResistance4Expected[] = {0x89, 0x94, 0x39, 0x80, 0x00, 0x00, 0x00};
static const uint8_t kResistance4Replacement[] = {0x89, 0x94, 0x39, 0x7C, 0x00, 0x00, 0x00};
static const uint8_t kResistance5Expected[] = {0x89, 0x94, 0x39, 0x84, 0x00, 0x00, 0x00};
static const uint8_t kResistance5Replacement[] = {0x89, 0x94, 0x39, 0x80, 0x00, 0x00, 0x00};
static const uint8_t kResistance6Expected[] = {0x89, 0x84, 0x3A, 0x88, 0x00, 0x00, 0x00};
static const uint8_t kResistance6Replacement[] = {0x89, 0x84, 0x3A, 0x84, 0x00, 0x00, 0x00};
static const PatchSite kResistanceSites[] = {
    {0x00440E90u, kResistance1Expected, kResistance1Replacement, PATCH_LENGTH(kResistance1Expected)},
    {0x00440EB6u, kResistance2Expected, kResistance2Replacement, PATCH_LENGTH(kResistance2Expected)},
    {0x00440EDCu, kResistance3Expected, kResistance3Replacement, PATCH_LENGTH(kResistance3Expected)},
    {0x00440F02u, kResistance4Expected, kResistance4Replacement, PATCH_LENGTH(kResistance4Expected)},
    {0x00440F2Bu, kResistance5Expected, kResistance5Replacement, PATCH_LENGTH(kResistance5Expected)},
    {0x00440F54u, kResistance6Expected, kResistance6Replacement, PATCH_LENGTH(kResistance6Expected)},
};

static const uint8_t kAnywhereSaveExpected[] = {0x8B, 0x80, 0x80, 0x03, 0x00, 0x00};
static const uint8_t kAnywhereSaveReplacement[] = {0xB8, 0x01, 0x00, 0x00, 0x00, 0x90};
static const PatchSite kAnywhereSaveSites[] = {
    {0x0040A0C2u, kAnywhereSaveExpected, kAnywhereSaveReplacement, PATCH_LENGTH(kAnywhereSaveExpected)},
};

static const uint8_t kMaxLootExpected[] = {0x99, 0xB9, 0x64, 0x00, 0x00, 0x00, 0xF7, 0xF9};
static const uint8_t kMaxLootReplacement[] = {0x99, 0xBA, 0x01, 0x00, 0x00, 0x00, 0x90, 0x90};
static const PatchSite kMaxLootSites[] = {
    {0x00443A22u, kMaxLootExpected, kMaxLootReplacement, PATCH_LENGTH(kMaxLootExpected)},
};

static const uint8_t kMaxGrowth1Expected[] = {0x99, 0xB9, 0x03, 0x00, 0x00, 0x00, 0xF7, 0xF9};
static const uint8_t kMaxGrowth1Replacement[] = {0x99, 0xBA, 0x02, 0x00, 0x00, 0x00, 0x90, 0x90};
static const uint8_t kMaxGrowth2Expected[] = {0x99, 0xB9, 0x03, 0x00, 0x00, 0x00, 0x6A, 0x00, 0xF7, 0xF9};
static const uint8_t kMaxGrowth2Replacement[] = {0x99, 0xBA, 0x02, 0x00, 0x00, 0x00, 0x6A, 0x00, 0x90, 0x90};
static const uint8_t kMaxGrowth3Expected[] = {0xF7, 0xFE, 0x8B, 0xC2, 0x03, 0xC7};
static const uint8_t kMaxGrowth3Replacement[] = {0x8D, 0x44, 0x3E, 0xFF, 0x90, 0x90};
static const PatchSite kMaxGrowthSites[] = {
    {0x00443BB9u, kMaxGrowth1Expected, kMaxGrowth1Replacement, PATCH_LENGTH(kMaxGrowth1Expected)},
    {0x00443BFBu, kMaxGrowth2Expected, kMaxGrowth2Replacement, PATCH_LENGTH(kMaxGrowth2Expected)},
    {0x00443D7Du, kMaxGrowth3Expected, kMaxGrowth3Replacement, PATCH_LENGTH(kMaxGrowth3Expected)},
};

static const PatchGroup kNoCdGroup = {L"免 CD", kNoCdSites, _countof(kNoCdSites)};
static const PatchGroup kMingyuGroup = {L"修复冥狱杀阵", kMingyuSites, _countof(kMingyuSites)};
static const PatchGroup kResistanceGroup = {L"修复抗性", kResistanceSites, _countof(kResistanceSites)};
static const PatchGroup kAnywhereSaveGroup = {L"随时存档", kAnywhereSaveSites, _countof(kAnywhereSaveSites)};
static const PatchGroup kMaxGrowthGroup = {L"最大成长", kMaxGrowthSites, _countof(kMaxGrowthSites)};
static const PatchGroup kMaxLootGroup = {L"最大掉宝", kMaxLootSites, _countof(kMaxLootSites)};
static bool GetConfigPath(wchar_t *path, size_t capacity) {
    DWORD length = GetModuleFileNameW(NULL, path, (DWORD)capacity);
    if (length == 0 || length >= capacity) {
        return false;
    }
    wchar_t *slash = wcsrchr(path, L'\\');
    if (slash == NULL) {
        return false;
    }
    slash[1] = L'\0';
    return wcscat_s(path, capacity, kConfigFileName) == 0;
}

static bool IsExistingFile(const wchar_t *path) {
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool ResolveRelativeGamePath(wchar_t *path, size_t capacity) {
    static const wchar_t *const candidates[] = {
        L".\\RPG.exe",
        L".\\exe\\RPG.exe",
        L".\\Castle\\exe\\RPG.exe",
    };

    for (size_t index = 0; index < _countof(candidates); ++index) {
        DWORD length = GetFullPathNameW(candidates[index], (DWORD)capacity, path, NULL);
        if (length == 0 || length >= capacity) {
            continue;
        }
        if (IsExistingFile(path)) {
            return true;
        }
    }
    return false;
}

static bool LoadSavedGamePath(wchar_t *path, size_t capacity) {
    wchar_t config_path[MAX_PATH];
    if (!GetConfigPath(config_path, _countof(config_path))) {
        return false;
    }
    DWORD length = GetPrivateProfileStringW(L"Game",
                                             L"Path",
                                             L"",
                                             path,
                                             (DWORD)capacity,
                                             config_path);
    return length > 0 && length < capacity && IsExistingFile(path);
}

static void SaveGamePath(const wchar_t *path) {
    wchar_t config_path[MAX_PATH];
    if (GetConfigPath(config_path, _countof(config_path))) {
        WritePrivateProfileStringW(L"Game", L"Path", path, config_path);
    }
}

static void ShowWin32Error(HWND owner, const wchar_t *action) {
    DWORD error = GetLastError();
    wchar_t system_message[512] = L"";
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL,
                   error,
                   0,
                   system_message,
                   _countof(system_message),
                   NULL);

    wchar_t message[768];
    _snwprintf_s(message,
                 _countof(message),
                 _TRUNCATE,
                 L"%ls 失败（错误 %lu）：%ls",
                 action,
                 (unsigned long)error,
                 system_message);
    MessageBoxW(owner, message, L"幽城幻剑录补丁工具", MB_OK | MB_ICONERROR);
}

static bool ReadProcessExact(HANDLE process, uint32_t address, void *buffer, uint32_t length) {
    SIZE_T read = 0;
    return ReadProcessMemory(process, (LPCVOID)(uintptr_t)address, buffer, (SIZE_T)length, &read) &&
           read == (SIZE_T)length;
}

static bool WriteProtected(HANDLE process, uint32_t address, const void *data, uint32_t length) {
    DWORD old_protection = 0;
    if (!VirtualProtectEx(process,
                          (LPVOID)(uintptr_t)address,
                          (SIZE_T)length,
                          PAGE_EXECUTE_READWRITE,
                          &old_protection)) {
        return false;
    }

    SIZE_T written = 0;
    bool written_ok = WriteProcessMemory(process,
                                         (LPVOID)(uintptr_t)address,
                                         data,
                                         (SIZE_T)length,
                                         &written) &&
                      written == (SIZE_T)length;
    DWORD ignored = 0;
    bool restored = VirtualProtectEx(process,
                                     (LPVOID)(uintptr_t)address,
                                     (SIZE_T)length,
                                     old_protection,
                                     &ignored) != FALSE;
    if (written_ok) {
        FlushInstructionCache(process, (LPCVOID)(uintptr_t)address, (SIZE_T)length);
    }
    return written_ok && restored;
}


static bool ApplyPatchGroup(HANDLE process, const PatchGroup *group) {
    uint8_t current[16];

    for (size_t index = 0; index < group->count; ++index) {
        const PatchSite *site = &group->sites[index];
        if (site->length > (uint32_t)sizeof(current) ||
            !ReadProcessExact(process, site->address, current, site->length) ||
            memcmp(current, site->expected, site->length) != 0) {
            SetLastError(ERROR_REVISION_MISMATCH);
            return false;
        }
    }

    for (size_t index = 0; index < group->count; ++index) {
        const PatchSite *site = &group->sites[index];
        if (!WriteProtected(process, site->address, site->replacement, site->length)) {
            return false;
        }
    }

    return true;
}


static bool GetLauncherDirectory(wchar_t *path, size_t capacity) {
    DWORD length = GetModuleFileNameW(NULL, path, (DWORD)capacity);
    if (length == 0 || length >= capacity) return false;
    wchar_t *slash = wcsrchr(path, L'\\');
    if (slash == NULL) return false;
    slash[1] = L'\0';
    return true;
}

/* Keep these offsets in sync with the STUB_* constants in
   runtime_loader_stub.inc; the stub reads its strings from this layout. */
enum {
    RUNTIME_STUB_PATH_OFFSET = 0x00,
    RUNTIME_STUB_KERNEL_NAME_OFFSET = 0x220,
    RUNTIME_STUB_LOAD_NAME_OFFSET = 0x240,
    RUNTIME_STUB_START_NAME_OFFSET = 0x260,
    RUNTIME_STUB_DATA_SIZE = 0x300,
};


static bool InjectRuntimeDll(HANDLE process) {
    wchar_t launcher_dir[MAX_PATH];
    if (!GetLauncherDirectory(launcher_dir, _countof(launcher_dir))) {
        SetLastError(ERROR_BAD_PATHNAME);
        return false;
    }
    wchar_t dll_path[MAX_PATH];
    if (wcscpy_s(dll_path, _countof(dll_path), launcher_dir) != 0 ||
        wcscat_s(dll_path, _countof(dll_path), L"CastleRuntime.dll") != 0) {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return false;
    }
    if (!IsExistingFile(dll_path)) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return false;
    }

    LPVOID remote_data = VirtualAllocEx(process,
                                        NULL,
                                        RUNTIME_STUB_DATA_SIZE,
                                        MEM_RESERVE | MEM_COMMIT,
                                        PAGE_READWRITE);
    LPVOID remote_code = VirtualAllocEx(process,
                                        NULL,
                                        kRuntimeLoaderStubSize,
                                        MEM_RESERVE | MEM_COMMIT,
                                        PAGE_READWRITE);
    if (remote_data == NULL || remote_code == NULL ||
        (uintptr_t)remote_data > UINT32_MAX || (uintptr_t)remote_code > UINT32_MAX) {
        if (remote_data != NULL) VirtualFreeEx(process, remote_data, 0, MEM_RELEASE);
        if (remote_code != NULL) VirtualFreeEx(process, remote_code, 0, MEM_RELEASE);
        SetLastError(ERROR_NOT_SUPPORTED);
        return false;
    }

    uint32_t code_address = (uint32_t)(uintptr_t)remote_code;
    uint8_t data[RUNTIME_STUB_DATA_SIZE] = {0};
    size_t path_bytes = (wcslen(dll_path) + 1u) * sizeof(wchar_t);
    if (path_bytes > RUNTIME_STUB_KERNEL_NAME_OFFSET) {
        VirtualFreeEx(process, remote_data, 0, MEM_RELEASE);
        VirtualFreeEx(process, remote_code, 0, MEM_RELEASE);
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return false;
    }
    memcpy(data + RUNTIME_STUB_PATH_OFFSET, dll_path, path_bytes);
    memcpy(data + RUNTIME_STUB_KERNEL_NAME_OFFSET, "kernel32.dll", sizeof("kernel32.dll"));
    memcpy(data + RUNTIME_STUB_LOAD_NAME_OFFSET, "LoadLibraryW", sizeof("LoadLibraryW"));
    memcpy(data + RUNTIME_STUB_START_NAME_OFFSET, "CastleRuntimeStart", sizeof("CastleRuntimeStart"));

    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote_data, data, sizeof(data), &written) || written != sizeof(data)) {
        VirtualFreeEx(process, remote_data, 0, MEM_RELEASE);
        VirtualFreeEx(process, remote_code, 0, MEM_RELEASE);
        return false;
    }

    if (!WriteProcessMemory(process, remote_code, kRuntimeLoaderStub, kRuntimeLoaderStubSize, &written) ||
        written != kRuntimeLoaderStubSize) {
        VirtualFreeEx(process, remote_data, 0, MEM_RELEASE);
        VirtualFreeEx(process, remote_code, 0, MEM_RELEASE);
        return false;
    }
    DWORD old_protection = 0;
    if (!VirtualProtectEx(process,
                          remote_code,
                          kRuntimeLoaderStubSize,
                          PAGE_EXECUTE_READ,
                          &old_protection)) {
        VirtualFreeEx(process, remote_data, 0, MEM_RELEASE);
        VirtualFreeEx(process, remote_code, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(process, remote_code, kRuntimeLoaderStubSize);

    /* Pass the data block as lpParameter; the stub reads it from [ebp+8]. */
    HANDLE thread = CreateRemoteThread(process,
                                       NULL,
                                       0,
                                       (LPTHREAD_START_ROUTINE)(uintptr_t)code_address,
                                       remote_data,
                                       0,
                                       NULL);
    if (thread == NULL) {
        VirtualFreeEx(process, remote_data, 0, MEM_RELEASE);
        VirtualFreeEx(process, remote_code, 0, MEM_RELEASE);
        return false;
    }

    DWORD wait_result = WaitForSingleObject(thread, 10000);
    DWORD exit_code = 0;
    if (wait_result == WAIT_OBJECT_0) {
        if (!GetExitCodeThread(thread, &exit_code)) {
            DWORD error = GetLastError();
            CloseHandle(thread);
            /* The remote thread has terminated, so both temporary mappings
               are safe to release even when querying its exit code failed. */
            VirtualFreeEx(process, remote_data, 0, MEM_RELEASE);
            VirtualFreeEx(process, remote_code, 0, MEM_RELEASE);
            SetLastError(error);
            return false;
        }
    } else if (wait_result == WAIT_TIMEOUT) {
        /* The loader stub only waits while import slots are unresolved.  Do
           not resume the game with a half-installed runtime and do not leave
           this as a silent success: its thread is still executing, so the
           caller must terminate the suspended process instead of freeing its
           code/data mappings underneath it. */
        CloseHandle(thread);
        SetLastError(ERROR_TIMEOUT);
        return false;
    } else {
        DWORD error = GetLastError();
        CloseHandle(thread);
        SetLastError(error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE);
        return false;
    }
    CloseHandle(thread);
    g_runtime_start_result = exit_code;
    if (wait_result == WAIT_OBJECT_0 && exit_code != 1) {
        VirtualFreeEx(process, remote_data, 0, MEM_RELEASE);
        VirtualFreeEx(process, remote_code, 0, MEM_RELEASE);
        SetLastError(ERROR_DLL_INIT_FAILED);
        return false;
    }
    VirtualFreeEx(process, remote_data, 0, MEM_RELEASE);
    VirtualFreeEx(process, remote_code, 0, MEM_RELEASE);
    return true;
}

static bool ApplySelectedPatches(HANDLE process,
                                 const LaunchOptions *options) {
    const PatchGroup *groups[6];
    size_t count = 0;

    if (options->no_cd) groups[count++] = &kNoCdGroup;
    if (options->mingyu_fix) groups[count++] = &kMingyuGroup;
    if (options->resistance_fix) groups[count++] = &kResistanceGroup;
    if (options->anywhere_save) groups[count++] = &kAnywhereSaveGroup;
    if (options->max_growth) groups[count++] = &kMaxGrowthGroup;
    if (options->max_loot) groups[count++] = &kMaxLootGroup;
    for (size_t index = 0; index < count; ++index) {
        /* These patches are intentionally best-effort.  A disk image may
           already contain one of them from an earlier offline patcher; the
           runtime process can still start with the remaining selections. */
        (void)ApplyPatchGroup(process, groups[index]);
    }

    if (options->widescreen && !options->windowed) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    return true;
}

static bool CreateGameProcess(const wchar_t *path, PROCESS_INFORMATION *process_info) {
    wchar_t current_directory[MAX_PATH];
    wcsncpy_s(current_directory, _countof(current_directory), path, _TRUNCATE);
    wchar_t *last_slash = wcsrchr(current_directory, L'\\');
    if (last_slash == NULL) {
        SetLastError(ERROR_BAD_PATHNAME);
        return false;
    }
    *last_slash = L'\0';

    STARTUPINFOW startup_info;
    ZeroMemory(&startup_info, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);
    ZeroMemory(process_info, sizeof(*process_info));

    return CreateProcessW(path,
                          NULL,
                          NULL,
                          NULL,
                          FALSE,
                          CREATE_SUSPENDED,
                          NULL,
                          current_directory,
                          &startup_info,
                          process_info) != FALSE;
}

static bool SelectGamePath(HWND owner, wchar_t *path, size_t capacity) {
    if (LoadSavedGamePath(path, capacity) || ResolveRelativeGamePath(path, capacity)) {
        return true;
    }

    path[0] = L'\0';
    OPENFILENAMEW dialog;
    ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"RPG.exe\0RPG.exe\0Executable files\0*.exe\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = (DWORD)capacity;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) {
        return false;
    }

    wchar_t full_path[MAX_PATH];
    DWORD length = GetFullPathNameW(path, _countof(full_path), full_path, NULL);
    if (length == 0 || length >= _countof(full_path) || !IsExistingFile(full_path)) {
        return false;
    }
    if (wcsncpy_s(path, capacity, full_path, _TRUNCATE) != 0) {
        return false;
    }
    SaveGamePath(path);
    return true;
}

static void Launch(HWND owner) {
    wchar_t path[MAX_PATH];
    if (!SelectGamePath(owner, path, _countof(path))) {
        LauncherSetStatus(L"未选择 RPG.exe。");
        return;
    }

    LauncherSaveOptions();
    LaunchOptions options = {
        .no_cd = settings[ID_NO_CD].value != 0,
        .mingyu_fix = settings[ID_MINGYU_FIX].value != 0,
        .resistance_fix = settings[ID_RESISTANCE_FIX].value != 0,
        .anywhere_save = settings[ID_ANYWHERE_SAVE].value != 0,
        .max_growth = settings[ID_MAX_GROWTH].value != 0,
        .max_loot = settings[ID_MAX_LOOT].value != 0,
        .windowed = settings[ID_WINDOWED].value != 0,
        .cursor_lock = settings[ID_CURSOR_LOCK].value != 0,
        .widescreen = settings[ID_WIDESCREEN].value != 0,
        .experience_multiplier = settings[ID_EXP_MULTIPLIER].value,
        .money_multiplier = settings[ID_MONEY_MULTIPLIER].value,
        .dynamic_encounter_rate = settings[ID_DYNAMIC_ENCOUNTER_RATE].value != 0,
    };

    PROCESS_INFORMATION process_info;
    if (!CreateGameProcess(path, &process_info)) {
        ShowWin32Error(owner, L"创建已挂起的游戏进程");
        return;
    }

    if (!ApplySelectedPatches(process_info.hProcess, &options)) {
        DWORD error = GetLastError();
        TerminateProcess(process_info.hProcess, 1);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        SetLastError(error);
        ShowWin32Error(owner, L"验证或写入运行时补丁");
        return;
    }

    if ((options.windowed || options.widescreen || options.experience_multiplier != 1 || options.money_multiplier != 1 ||
         options.dynamic_encounter_rate) &&
        !InjectRuntimeDll(process_info.hProcess)) {
        DWORD error = GetLastError();
        TerminateProcess(process_info.hProcess, 1);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        SetLastError(error);
        wchar_t action[128];
        _snwprintf_s(action,
                     _countof(action),
                     _TRUNCATE,
                     L"注入目标进程运行时模块（运行时状态 %lu）",
                     (unsigned long)g_runtime_start_result);
        ShowWin32Error(owner, action);
        return;
    }

    /* All one-time writes and the runtime module are now resident in the
       target process.  After this call the launcher does not monitor, resize,
       clip, or otherwise communicate with the game. */
    if (ResumeThread(process_info.hThread) == (DWORD)-1) {
        DWORD error = GetLastError();
        TerminateProcess(process_info.hProcess, 1);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        SetLastError(error);
        ShowWin32Error(owner, L"恢复游戏进程");
        return;
    }

    wchar_t status[160];
    _snwprintf_s(status,
                 _countof(status),
                 _TRUNCATE,
                 L"已向 PID %lu 写入所选补丁并启动游戏。原始 RPG.exe 未被修改。",
                 (unsigned long)process_info.dwProcessId);
    LauncherSetStatus(status);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show) {
    (void)previous;
    (void)command_line;
    static const LauncherSpec launcher = {
        L"CastlePatchesWindow", L"《天地劫序传 幽城幻剑录》补丁工具", L"幽城幻剑录",
        L"CastlePatches  /  v" LAUNCHER_VERSION, pages, _countof(pages), settings, _countof(settings),
        GetConfigPath, Launch,
    };
    return LauncherRun(instance, show, &launcher);
}
