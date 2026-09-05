#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "runtime_loader_stub_blob.h"

#ifndef _countof
#define _countof(array) (sizeof(array) / sizeof((array)[0]))
#endif

/* The target executable and every injected instruction block are 32-bit x86.
   Keep patch addresses and byte counts explicitly 32-bit as well: using
   pointer-sized fields here makes the launcher data model-dependent when the
   launcher itself is built for x64.  The Win32 APIs still receive SIZE_T after
   the value has been widened at the call boundary. */
#define PATCH_LENGTH(array) ((uint32_t)(sizeof(array)))

enum {
    ID_NO_CD = 100,
    ID_MINGYU_FIX,
    ID_RESISTANCE_FIX,
    ID_ANYWHERE_SAVE,
    ID_MAX_GROWTH,
    ID_MAX_LOOT,
    ID_WINDOWED,
    ID_CURSOR_LOCK,
    ID_WIDESCREEN,
    ID_SCALE,
    ID_EXP_MULTIPLIER,
    ID_EXP_MULTIPLIER_SPIN,
    ID_MONEY_MULTIPLIER,
    ID_MONEY_MULTIPLIER_SPIN,
    ID_DYNAMIC_ENCOUNTER_RATE,
    ID_LAUNCH,
    ID_STATUS,
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
    /* Fixed-point scale in half-units: 2 = 1×, 3 = 1.5×, ... */
    uint32_t scale2;
    uint32_t experience_multiplier;
    uint32_t money_multiplier;
    bool dynamic_encounter_rate;
} LaunchOptions;

static HINSTANCE g_instance;
static HWND g_main_window;
static HWND g_scale_combo;
static HWND g_experience_multiplier_edit;
static HWND g_money_multiplier_edit;
static HWND g_status;
static HFONT g_font;
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
static void SetStatus(const wchar_t *message) {
    SetWindowTextW(g_status, message);
}

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

static int ScaleValueToIndex(uint32_t scale2) {
    static const uint32_t values[] = {2, 3, 4, 5, 6};
    for (size_t index = 0; index < _countof(values); ++index) {
        if (values[index] == scale2) {
            return (int)index;
        }
    }
    return 0;
}

static uint32_t ScaleIndexToValue(int index) {
    static const uint32_t values[] = {2, 3, 4, 5, 6};
    if (index < 0 || (size_t)index >= _countof(values)) {
        return values[0];
    }
    return values[index];
}

static bool LoadConfigBool(const wchar_t *key, bool default_value) {
    wchar_t config_path[MAX_PATH];
    if (!GetConfigPath(config_path, _countof(config_path))) {
        return default_value;
    }
    return GetPrivateProfileIntW(L"Patches", key, default_value ? 1 : 0, config_path) != 0;
}

static bool LoadConfigDisplayBool(const wchar_t *key, bool default_value) {
    wchar_t config_path[MAX_PATH];
    if (!GetConfigPath(config_path, _countof(config_path))) {
        return default_value;
    }
    return GetPrivateProfileIntW(L"Display", key, default_value ? 1 : 0, config_path) != 0;
}

static uint32_t LoadConfigScale(void) {
    wchar_t config_path[MAX_PATH];
    if (!GetConfigPath(config_path, _countof(config_path))) {
        return 2;
    }
    return ScaleIndexToValue(ScaleValueToIndex((uint32_t)GetPrivateProfileIntW(L"Display", L"Scale2", 2, config_path)));
}

static uint32_t ClampMultiplier(uint32_t value) {
    return value < 1 ? 1 : value > 10 ? 10 : value;
}

static uint32_t LoadConfigMultiplier(const wchar_t *key) {
    wchar_t config_path[MAX_PATH];
    if (!GetConfigPath(config_path, _countof(config_path))) {
        return 1;
    }
    return ClampMultiplier((uint32_t)GetPrivateProfileIntW(L"Patches", key, 1, config_path));
}

static uint32_t ReadMultiplier(HWND edit) {
    BOOL translated = FALSE;
    uint32_t value = (uint32_t)GetDlgItemInt(g_main_window, GetDlgCtrlID(edit), &translated, FALSE);
    return translated ? ClampMultiplier(value) : 1;
}

static void SaveOptions(const LaunchOptions *options) {
    wchar_t config_path[MAX_PATH];
    if (!GetConfigPath(config_path, _countof(config_path))) {
        return;
    }

    WritePrivateProfileStringW(L"Patches", L"NoCd", options->no_cd ? L"1" : L"0", config_path);
    WritePrivateProfileStringW(L"Patches", L"MingyuFix", options->mingyu_fix ? L"1" : L"0", config_path);
    WritePrivateProfileStringW(L"Patches", L"ResistanceFix", options->resistance_fix ? L"1" : L"0", config_path);
    WritePrivateProfileStringW(L"Patches", L"AnywhereSave", options->anywhere_save ? L"1" : L"0", config_path);
    WritePrivateProfileStringW(L"Patches", L"MaxGrowth", options->max_growth ? L"1" : L"0", config_path);
    WritePrivateProfileStringW(L"Patches", L"MaxLoot", options->max_loot ? L"1" : L"0", config_path);
    wchar_t multiplier[16];
    _snwprintf_s(multiplier, _countof(multiplier), _TRUNCATE, L"%lu", (unsigned long)options->experience_multiplier);
    WritePrivateProfileStringW(L"Patches", L"ExperienceMultiplier", multiplier, config_path);
    _snwprintf_s(multiplier, _countof(multiplier), _TRUNCATE, L"%lu", (unsigned long)options->money_multiplier);
    WritePrivateProfileStringW(L"Patches", L"MoneyMultiplier", multiplier, config_path);
    WritePrivateProfileStringW(L"Patches", L"DynamicEncounterRate", options->dynamic_encounter_rate ? L"1" : L"0", config_path);
    WritePrivateProfileStringW(L"Display", L"Windowed", options->windowed ? L"1" : L"0", config_path);
    WritePrivateProfileStringW(L"Display", L"CursorLock", options->cursor_lock ? L"1" : L"0", config_path);
    WritePrivateProfileStringW(L"Display", L"Widescreen", options->widescreen ? L"1" : L"0", config_path);

    wchar_t scale[16];
    _snwprintf_s(scale, _countof(scale), _TRUNCATE, L"%lu", (unsigned long)options->scale2);
    WritePrivateProfileStringW(L"Display", L"Scale2", scale, config_path);
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

static bool IsChecked(int control_id) {
    return IsDlgButtonChecked(g_main_window, control_id) == BST_CHECKED;
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
        SetStatus(L"未选择可启动的 RPG.exe。等待启动。");
        return;
    }

    LaunchOptions options = {
        .no_cd = IsChecked(ID_NO_CD),
        .mingyu_fix = IsChecked(ID_MINGYU_FIX),
        .resistance_fix = IsChecked(ID_RESISTANCE_FIX),
        .anywhere_save = IsChecked(ID_ANYWHERE_SAVE),
        .max_growth = IsChecked(ID_MAX_GROWTH),
        .max_loot = IsChecked(ID_MAX_LOOT),
        .windowed = IsChecked(ID_WINDOWED),
        .cursor_lock = IsChecked(ID_CURSOR_LOCK),
        .widescreen = IsChecked(ID_WIDESCREEN),
        .scale2 = ScaleIndexToValue((int)SendMessageW(g_scale_combo, CB_GETCURSEL, 0, 0)),
        .experience_multiplier = ReadMultiplier(g_experience_multiplier_edit),
        .money_multiplier = ReadMultiplier(g_money_multiplier_edit),
        .dynamic_encounter_rate = IsChecked(ID_DYNAMIC_ENCOUNTER_RATE),
    };
    SaveOptions(&options);

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
    SetStatus(status);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
}

static void UpdateWindowedControls(HWND window) {
    EnableWindow(g_scale_combo, IsChecked(ID_WINDOWED));
    EnableWindow(GetDlgItem(window, ID_CURSOR_LOCK), IsChecked(ID_WINDOWED));
    EnableWindow(GetDlgItem(window, ID_WIDESCREEN), IsChecked(ID_WINDOWED));
    InvalidateRect(window, NULL, TRUE);
}

static HWND AddControl(HWND parent,
                       const wchar_t *class_name,
                       const wchar_t *text,
                       DWORD style,
                       int x,
                       int y,
                       int width,
                       int height,
                       int id) {
    HWND control = CreateWindowExW(0,
                                   class_name,
                                   text,
                                   WS_CHILD | WS_VISIBLE | style,
                                   x,
                                   y,
                                   width,
                                   height,
                                   parent,
                                   (HMENU)(INT_PTR)id,
                                   g_instance,
                                   NULL);
    SendMessageW(control, WM_SETFONT, (WPARAM)g_font, TRUE);
    return control;
}

static void CreateControls(HWND window) {
    g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    AddControl(window, L"STATIC", L"运行时补丁", SS_LEFT, 18, 18, 140, 22, -1);
    AddControl(window, L"BUTTON", L"免 CD", BS_AUTOCHECKBOX, 18, 44, 260, 24, ID_NO_CD);
    AddControl(window, L"BUTTON", L"修复「冥狱杀阵」可被习得", BS_AUTOCHECKBOX, 18, 70, 300, 24, ID_MINGYU_FIX);
    AddControl(window, L"BUTTON", L"修复菜单后的抗性显示与实际值不一致", BS_AUTOCHECKBOX, 18, 96, 320, 24, ID_RESISTANCE_FIX);
    AddControl(window, L"BUTTON", L"随时存档", BS_AUTOCHECKBOX, 18, 122, 260, 24, ID_ANYWHERE_SAVE);
    AddControl(window, L"BUTTON", L"最大成长", BS_AUTOCHECKBOX, 18, 148, 260, 24, ID_MAX_GROWTH);
    AddControl(window, L"BUTTON", L"最大掉宝", BS_AUTOCHECKBOX, 18, 174, 260, 24, ID_MAX_LOOT);
    AddControl(window, L"BUTTON", L"动态调整遇敌率（Ctrl+F10/F11/F12）", BS_AUTOCHECKBOX, 18, 204, 300, 24, ID_DYNAMIC_ENCOUNTER_RATE);
    AddControl(window, L"STATIC", L"经验倍率", SS_LEFT, 18, 232, 80, 22, -1);
    g_experience_multiplier_edit = AddControl(window, L"EDIT", L"", ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER, 92, 230, 44, 20, ID_EXP_MULTIPLIER);
    HWND experience_spin = AddControl(window, UPDOWN_CLASSW, L"", UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS, 136, 230, 18, 20, ID_EXP_MULTIPLIER_SPIN);
    SendMessageW(experience_spin, UDM_SETBUDDY, (WPARAM)g_experience_multiplier_edit, 0);
    SendMessageW(experience_spin, UDM_SETRANGE32, 1, 10);
    SetDlgItemInt(window, ID_EXP_MULTIPLIER, LoadConfigMultiplier(L"ExperienceMultiplier"), FALSE);
    AddControl(window, L"STATIC", L"金钱倍率", SS_LEFT, 170, 232, 80, 22, -1);
    g_money_multiplier_edit = AddControl(window, L"EDIT", L"", ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER, 244, 230, 44, 20, ID_MONEY_MULTIPLIER);
    HWND money_spin = AddControl(window, UPDOWN_CLASSW, L"", UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS, 288, 230, 18, 20, ID_MONEY_MULTIPLIER_SPIN);
    SendMessageW(money_spin, UDM_SETBUDDY, (WPARAM)g_money_multiplier_edit, 0);
    SendMessageW(money_spin, UDM_SETRANGE32, 1, 10);
    SetDlgItemInt(window, ID_MONEY_MULTIPLIER, LoadConfigMultiplier(L"MoneyMultiplier"), FALSE);

    AddControl(window, L"STATIC", L"显示模式", SS_LEFT, 360, 18, 140, 22, -1);
    AddControl(window, L"BUTTON", L"窗口模式（修正鼠标坐标）", BS_AUTOCHECKBOX, 360, 44, 220, 24, ID_WINDOWED);
    AddControl(window, L"BUTTON", L"锁定鼠标在窗口内", BS_AUTOCHECKBOX, 360, 70, 220, 24, ID_CURSOR_LOCK);
    AddControl(window, L"BUTTON", L"宽屏地图（864 × 480）", BS_AUTOCHECKBOX, 360, 130, 220, 24, ID_WIDESCREEN);
    AddControl(window, L"STATIC", L"窗口倍率", SS_LEFT, 360, 104, 90, 22, -1);
    g_scale_combo = AddControl(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 430, 100, 140, 140, ID_SCALE);
    SendMessageW(g_scale_combo, CB_ADDSTRING, 0, (LPARAM)L"1×（640 × 480）");
    SendMessageW(g_scale_combo, CB_ADDSTRING, 0, (LPARAM)L"1.5×（960 × 720）");
    SendMessageW(g_scale_combo, CB_ADDSTRING, 0, (LPARAM)L"2×（1280 × 960）");
    SendMessageW(g_scale_combo, CB_ADDSTRING, 0, (LPARAM)L"2.5×（1600 × 1200）");
    SendMessageW(g_scale_combo, CB_ADDSTRING, 0, (LPARAM)L"3×（1920 × 1440）");
    SendMessageW(g_scale_combo, CB_SETCURSEL, (WPARAM)ScaleValueToIndex(LoadConfigScale()), 0);
    EnableWindow(g_scale_combo, FALSE);

    AddControl(window, L"STATIC",
               L"启动时创建已挂起的进程，仅向该进程内存写入补丁。",
               SS_LEFT,
               18,
                262,
               552,
               22,
               -1);
    AddControl(window, L"BUTTON", L"应用补丁并启动游戏", BS_DEFPUSHBUTTON, 360, 292, 210, 32, ID_LAUNCH);
    g_status = AddControl(window, L"STATIC", L"等待启动。", SS_LEFT, 18, 300, 330, 22, ID_STATUS);

    CheckDlgButton(window, ID_NO_CD, LoadConfigBool(L"NoCd", true) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, ID_MINGYU_FIX, LoadConfigBool(L"MingyuFix", true) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, ID_RESISTANCE_FIX, LoadConfigBool(L"ResistanceFix", true) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, ID_ANYWHERE_SAVE, LoadConfigBool(L"AnywhereSave", true) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, ID_MAX_GROWTH, LoadConfigBool(L"MaxGrowth", false) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, ID_MAX_LOOT, LoadConfigBool(L"MaxLoot", false) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, ID_DYNAMIC_ENCOUNTER_RATE, LoadConfigBool(L"DynamicEncounterRate", false) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, ID_WINDOWED, LoadConfigDisplayBool(L"Windowed", false) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, ID_CURSOR_LOCK, LoadConfigDisplayBool(L"CursorLock", false) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, ID_WIDESCREEN, LoadConfigDisplayBool(L"Widescreen", false) ? BST_CHECKED : BST_UNCHECKED);
    UpdateWindowedControls(window);
}

static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
        case WM_CREATE:
            g_main_window = window;
            CreateControls(window);
            return 0;
        case WM_COMMAND:
            if (HIWORD(w_param) == BN_CLICKED) {
                switch (LOWORD(w_param)) {
                    case ID_WINDOWED:
                        UpdateWindowedControls(window);
                        return 0;
                    case ID_LAUNCH:
                        Launch(window);
                        return 0;
                    default:
                        break;
                }
            }
            return 0;
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)w_param;
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = (HDC)w_param;
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, GetSysColor(COLOR_WINDOW));
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        case WM_CTLCOLORBTN: {
            HDC dc = (HDC)w_param;
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, w_param, l_param);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous_instance, PWSTR command_line, int show_command) {
    (void)previous_instance;
    (void)command_line;
    g_instance = instance;

    INITCOMMONCONTROLSEX common_controls;
    common_controls.dwSize = sizeof(common_controls);
    common_controls.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&common_controls);

    WNDCLASSW window_class;
    ZeroMemory(&window_class, sizeof(window_class));
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    /* Keep the parent and static-control backgrounds on the same system
       color.  Edit/list controls are explicitly painted white in
       WM_CTLCOLOREDIT/WM_CTLCOLORLISTBOX above. */
    window_class.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    window_class.lpszClassName = L"CastlePatchesWindow";
    window_class.lpfnWndProc = WindowProcedure;

    if (RegisterClassW(&window_class) == 0) {
        return 1;
    }

    HWND window = CreateWindowExW(WS_EX_APPWINDOW,
                                   window_class.lpszClassName,
                                   L"《天地劫序传 幽城幻剑录》补丁工具",
                                   WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                   0,
                                   0,
                                   604,
                                   372,
                                  NULL,
                                  NULL,
                                  instance,
                                  NULL);
    if (window == NULL) {
        return 1;
    }
    g_main_window = window;

    RECT work_area = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    RECT window_rect = {0};
    GetWindowRect(window, &window_rect);
    int width = window_rect.right - window_rect.left;
    int height = window_rect.bottom - window_rect.top;
    int x = work_area.left + ((work_area.right - work_area.left) - width) / 2;
    int y = work_area.top + ((work_area.bottom - work_area.top) - height) / 2;
    SetWindowPos(window, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return (int)message.wParam;
}
