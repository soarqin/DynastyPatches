#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

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
    ID_SCALE,
    ID_LAUNCH,
    ID_STATUS,
};

enum {
    WINDOWED_STYLE_ADDRESS = 0x0040183Au,
    WINDOWED_RENDERER_MODE_ADDRESS = 0x00405C5Bu,
    SURFACE_FORMAT_HOOK_ADDRESS = 0x00405BA5u,
    GAME_WINDOW_HANDLE_ADDRESS = 0x0046F384u,
    GET_MODULE_HANDLE_IAT_ADDRESS = 0x004600C8u,
    GET_PROC_ADDRESS_IAT_ADDRESS = 0x00460090u,
    BINK_LOCK_DEFAULT_SURFACE_ADDRESS = 0x00406242u,
    BINK_UNLOCK_DEFAULT_SURFACE_ADDRESS = 0x004062C8u,
    BINK_SURFACE_FORMAT_ADDRESS = 0x00401BD3u,
    BINK_PITCH_ADDRESS = 0x00401C01u,
    BINK_UNLOCK_CALL_ADDRESS = 0x00401C1Fu,
    BINK_UNLOCK_FUNCTION_ADDRESS = 0x004062C0u,
    WINDOWED_PRESENT_FUNCTION_ADDRESS = 0x004064E0u,
    WINDOWED_MOVE_CALL_ADDRESS = 0x00405F17u,
    REMOTE_HOOK_CAPACITY = 0x1000,
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
    /* Fixed-point scale in half-units: 2 = 1×, 3 = 1.5×, ... */
    uint32_t scale2;
} LaunchOptions;

static HINSTANCE g_instance;
static HWND g_main_window;
static HWND g_scale_combo;
static HWND g_status;
static HFONT g_font;
static wchar_t g_game_path[MAX_PATH];

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

static const uint8_t kWindowedStyleExpected[] = {0x68, 0x00, 0x00, 0x00, 0x80};
static const uint8_t kWindowedStyleReplacement[] = {0x68, 0x00, 0x00, 0xCA, 0x00};
static const uint8_t kWindowedRendererExpected[] = {0x8A, 0x46, 0x30, 0x84, 0xC0};
static const uint8_t kWindowedRendererReplacement[] = {0x30, 0xC0, 0x88, 0x46, 0x30};
static const uint8_t kSurfaceFormatHookExpected[] = {0x8B, 0x10, 0x51, 0x50, 0xFF, 0x52, 0x18};
static const uint8_t kBinkLockDefaultSurfaceExpected[] = {0x8B, 0x69, 0x04};
static const uint8_t kBinkLockDefaultSurfaceReplacement[] = {0x8B, 0x69, 0x08};
static const uint8_t kBinkUnlockDefaultSurfaceExpected[] = {0x8B, 0x41, 0x04};
static const uint8_t kBinkUnlockDefaultSurfaceReplacement[] = {0x8B, 0x41, 0x08};
static const uint8_t kBinkSurfaceFormatExpected[] = {0xBF, 0x09, 0x00, 0x00, 0x00};
static const uint8_t kBinkSurfaceFormatReplacement[] = {0xBF, 0x0A, 0x00, 0x00, 0x00};
static const uint8_t kBinkPitchExpected[] = {0x8B, 0x49, 0x40};
static const uint8_t kBinkPitchReplacement[] = {0x8B, 0x49, 0x20};
static const uint8_t kBinkUnlockCallExpected[] = {0xE8, 0x9C, 0x46, 0x00, 0x00};
static const PatchSite kWindowedSites[] = {
    {WINDOWED_STYLE_ADDRESS, kWindowedStyleExpected, kWindowedStyleReplacement, PATCH_LENGTH(kWindowedStyleExpected)},
    {WINDOWED_RENDERER_MODE_ADDRESS, kWindowedRendererExpected, kWindowedRendererReplacement, PATCH_LENGTH(kWindowedRendererExpected)},
};

static const PatchGroup kNoCdGroup = {L"免 CD", kNoCdSites, _countof(kNoCdSites)};
static const PatchGroup kMingyuGroup = {L"修复冥狱杀阵", kMingyuSites, _countof(kMingyuSites)};
static const PatchGroup kResistanceGroup = {L"修复抗性", kResistanceSites, _countof(kResistanceSites)};
static const PatchGroup kAnywhereSaveGroup = {L"随时存档", kAnywhereSaveSites, _countof(kAnywhereSaveSites)};
static const PatchGroup kMaxGrowthGroup = {L"最大成长", kMaxGrowthSites, _countof(kMaxGrowthSites)};
static const PatchGroup kMaxLootGroup = {L"最大掉宝", kMaxLootSites, _countof(kMaxLootSites)};
static const PatchGroup kWindowedGroup = {L"窗口模式", kWindowedSites, _countof(kWindowedSites)};

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
    WritePrivateProfileStringW(L"Display", L"Windowed", options->windowed ? L"1" : L"0", config_path);
    WritePrivateProfileStringW(L"Display", L"CursorLock", options->cursor_lock ? L"1" : L"0", config_path);

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
    MessageBoxW(owner, message, L"Castle Patches", MB_OK | MB_ICONERROR);
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

static bool g_emit_overflow;

static size_t EmitByte(uint8_t *buffer, size_t offset, uint8_t value) {
    if (offset >= REMOTE_HOOK_CAPACITY) {
        g_emit_overflow = true;
        return REMOTE_HOOK_CAPACITY;
    }
    buffer[offset] = value;
    return offset + 1;
}

static size_t EmitDword(uint8_t *buffer, size_t offset, uint32_t value) {
    if (offset > REMOTE_HOOK_CAPACITY - sizeof(value)) {
        g_emit_overflow = true;
        return REMOTE_HOOK_CAPACITY;
    }
    memcpy(buffer + offset, &value, sizeof(value));
    return offset + sizeof(value);
}

static bool CalculateRel32(uint32_t source_after_instruction,
                           uint32_t target,
                           int32_t *displacement_out) {
    int64_t displacement = (int64_t)(uint64_t)target -
                           (int64_t)(uint64_t)source_after_instruction;
    if (displacement < INT32_MIN || displacement > INT32_MAX) {
        return false;
    }
    *displacement_out = (int32_t)displacement;
    return true;
}

static void PatchNearJump(uint8_t *buffer, size_t displacement_offset, size_t target_offset) {
    /* The emitter reserves the five-byte E9 form.  Use EB when the final
       distance fits in int8, and turn the unused tail into NOPs.  Otherwise
       retain the long form and validate that the displacement is representable
       instead of silently truncating it. */
    size_t instruction_offset = displacement_offset - 1u;
    int64_t short_displacement = (int64_t)target_offset - (int64_t)(instruction_offset + 2u);
    if (short_displacement >= INT8_MIN && short_displacement <= INT8_MAX) {
        buffer[instruction_offset] = 0xEB;
        buffer[instruction_offset + 1u] = (uint8_t)(int8_t)short_displacement;
        for (size_t index = instruction_offset + 2u; index < displacement_offset + 4u; ++index) {
            buffer[index] = 0x90;
        }
        return;
    }

    int64_t displacement = (int64_t)target_offset - (int64_t)(displacement_offset + 4u);
    if (displacement < INT32_MIN || displacement > INT32_MAX) {
        /* All labels in this trampoline must share one allocation.  Leave a
           deterministic trap rather than emitting a wrapped jump if that
           invariant is ever violated. */
        buffer[instruction_offset] = 0xCC;
        return;
    }
    buffer[instruction_offset] = 0xE9;
    memcpy(buffer + displacement_offset, &(int32_t){(int32_t)displacement}, sizeof(int32_t));
}

static void PatchConditionalJump(uint8_t *buffer,
                                  size_t displacement_offset,
                                  size_t target_offset,
                                  uint8_t short_opcode,
                                  uint8_t long_opcode) {
    /* The emitter reserves the six-byte 0F xx form.  Shrinking to a short
       Jcc is safe because the remaining bytes are unreachable NOPs. */
    size_t instruction_offset = displacement_offset - 2u;
    int64_t short_displacement = (int64_t)target_offset - (int64_t)(instruction_offset + 2u);
    if (short_displacement >= INT8_MIN && short_displacement <= INT8_MAX) {
        buffer[instruction_offset] = short_opcode;
        buffer[instruction_offset + 1u] = (uint8_t)(int8_t)short_displacement;
        for (size_t index = instruction_offset + 2u; index < displacement_offset + 4u; ++index) {
            buffer[index] = 0x90;
        }
        return;
    }

    int64_t displacement = (int64_t)target_offset - (int64_t)(displacement_offset + 4u);
    if (displacement < INT32_MIN || displacement > INT32_MAX) {
        buffer[instruction_offset] = 0xCC;
        return;
    }
    buffer[instruction_offset] = 0x0F;
    buffer[instruction_offset + 1u] = long_opcode;
    memcpy(buffer + displacement_offset, &(int32_t){(int32_t)displacement}, sizeof(int32_t));
}

static void EmitPushImm(uint8_t *buffer, size_t *offset, uint32_t value) {
    *offset = EmitByte(buffer, *offset, 0x68);
    *offset = EmitDword(buffer, *offset, value);
}

static void EmitCallAbs(uint8_t *buffer, size_t *offset, uint32_t address) {
    *offset = EmitByte(buffer, *offset, 0xFF);
    *offset = EmitByte(buffer, *offset, 0x15);
    *offset = EmitDword(buffer, *offset, address);
}

static size_t BuildSurfaceFormatHook(uint8_t *buffer,
                                     uint32_t remote_address,
                                     uint32_t return_address) {
    g_emit_overflow = false;
    size_t offset = 0;
    offset = EmitByte(buffer, offset, 0x60); /* pushad */
    /* PUSHAD final stack layout is EDI, ESI, EBP, original ESP, EBX, EDX,
       ECX, EAX; the saved ECX is therefore at [ESP+18h]. */
    offset = EmitByte(buffer, offset, 0x8B); offset = EmitByte(buffer, offset, 0x44); offset = EmitByte(buffer, offset, 0x24); offset = EmitByte(buffer, offset, 0x18);
    /* sub_405B30 is shared by the Back and OffScreen allocations.  Its
       descriptor carries DDSCAPS_OFFSCREENPLAIN (0x40, optionally with the
       SYSTEMMEMORY bit 0x800) in DDSURFACEDESC2.ddsCaps.dwCaps at +68h.
       Restrict the format override to that class:
       callers using this helper for any other surface must retain the
       original driver-selected format.  The Z-buffer is created directly in
       sub_405BD0 and never reaches this hook. */
    offset = EmitByte(buffer, offset, 0xF6); offset = EmitByte(buffer, offset, 0x40); offset = EmitByte(buffer, offset, 0x68); offset = EmitByte(buffer, offset, 0x40);
    size_t skip_format_offset = offset;
    /* Use the rel32 form deliberately.  The hook body may grow as additional
       guards are added, so a short branch would silently
       become unsafe once the displacement exceeded 127 bytes. */
    offset = EmitByte(buffer, offset, 0x0F); offset = EmitByte(buffer, offset, 0x84);
    offset = EmitDword(buffer, offset, 0u);
    /* DDSURFACEDESC2.dwFlags |= DDSD_PIXELFORMAT. */
    offset = EmitByte(buffer, offset, 0x81); offset = EmitByte(buffer, offset, 0x48); offset = EmitByte(buffer, offset, 0x04); offset = EmitDword(buffer, offset, 0x1000);
    /* DDPIXELFORMAT starts at descriptor +0x48. Force RGB565. */
    offset = EmitByte(buffer, offset, 0xC7); offset = EmitByte(buffer, offset, 0x40); offset = EmitByte(buffer, offset, 0x48); offset = EmitDword(buffer, offset, 0x20);
    offset = EmitByte(buffer, offset, 0xC7); offset = EmitByte(buffer, offset, 0x40); offset = EmitByte(buffer, offset, 0x4C); offset = EmitDword(buffer, offset, 0x40);
    offset = EmitByte(buffer, offset, 0xC7); offset = EmitByte(buffer, offset, 0x40); offset = EmitByte(buffer, offset, 0x54); offset = EmitDword(buffer, offset, 16);
    offset = EmitByte(buffer, offset, 0xC7); offset = EmitByte(buffer, offset, 0x40); offset = EmitByte(buffer, offset, 0x58); offset = EmitDword(buffer, offset, 0xF800);
    offset = EmitByte(buffer, offset, 0xC7); offset = EmitByte(buffer, offset, 0x40); offset = EmitByte(buffer, offset, 0x5C); offset = EmitDword(buffer, offset, 0x07E0);
    offset = EmitByte(buffer, offset, 0xC7); offset = EmitByte(buffer, offset, 0x40); offset = EmitByte(buffer, offset, 0x60); offset = EmitDword(buffer, offset, 0x001F);
    /* Patch the branch using buffer-relative offsets.  The remote base
       cancels out because source and destination are in the same block. */
    int32_t skip_format_displacement = (int32_t)offset -
                                       (int32_t)(skip_format_offset + 6u);
    memcpy(buffer + skip_format_offset + 2u,
           &skip_format_displacement,
           sizeof(skip_format_displacement));
    offset = EmitByte(buffer, offset, 0x61); /* popad */
    offset = EmitByte(buffer, offset, 0x8B); offset = EmitByte(buffer, offset, 0x10);
    offset = EmitByte(buffer, offset, 0x51);
    offset = EmitByte(buffer, offset, 0x50);
    offset = EmitByte(buffer, offset, 0xFF); offset = EmitByte(buffer, offset, 0x52); offset = EmitByte(buffer, offset, 0x18);
    offset = EmitByte(buffer, offset, 0xE9);
    int32_t displacement = (int32_t)return_address - (int32_t)(remote_address + (uint32_t)offset + 4u);
    offset = EmitDword(buffer, offset, (uint32_t)displacement);
    return g_emit_overflow ? 0 : offset;
}

static bool InstallSurfaceFormatHook(HANDLE process) {
    uint8_t current[sizeof(kSurfaceFormatHookExpected)];
    if (!ReadProcessExact(process, SURFACE_FORMAT_HOOK_ADDRESS, current, sizeof(current)) ||
        memcmp(current, kSurfaceFormatHookExpected, sizeof(current)) != 0) {
        SetLastError(ERROR_REVISION_MISMATCH);
        return false;
    }

    uint8_t hook[REMOTE_HOOK_CAPACITY] = {0};
    LPVOID remote_memory = VirtualAllocEx(process, NULL, sizeof(hook), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (remote_memory == NULL || (uintptr_t)remote_memory > UINT32_MAX) {
        if (remote_memory != NULL) VirtualFreeEx(process, remote_memory, 0, MEM_RELEASE);
        SetLastError(ERROR_NOT_SUPPORTED);
        return false;
    }

    uint32_t remote_address = (uint32_t)(uintptr_t)remote_memory;
    size_t hook_size = BuildSurfaceFormatHook(hook,
                                               remote_address,
                                               SURFACE_FORMAT_HOOK_ADDRESS + sizeof(kSurfaceFormatHookExpected));
    if (hook_size == 0) {
        VirtualFreeEx(process, remote_memory, 0, MEM_RELEASE);
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return false;
    }
    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote_memory, hook, hook_size, &written) || written != hook_size) {
        VirtualFreeEx(process, remote_memory, 0, MEM_RELEASE);
        return false;
    }

    DWORD old_protection = 0;
    if (!VirtualProtectEx(process, remote_memory, sizeof(hook), PAGE_EXECUTE_READ, &old_protection)) {
        VirtualFreeEx(process, remote_memory, 0, MEM_RELEASE);
        return false;
    }

    uint8_t jump[sizeof(kSurfaceFormatHookExpected)] = {0xE9, 0, 0, 0, 0, 0x90, 0x90};
    int32_t displacement = (int32_t)remote_address - (int32_t)(SURFACE_FORMAT_HOOK_ADDRESS + 5u);
    memcpy(jump + 1, &displacement, sizeof(displacement));
    if (!WriteProtected(process, SURFACE_FORMAT_HOOK_ADDRESS, jump, sizeof(jump))) {
        VirtualFreeEx(process, remote_memory, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(process, remote_memory, hook_size);
    return true;
}

/* The original movie path always locks renderer+4 (Primary) and uses the
   Primary pitch.  That is valid in exclusive full-screen mode, but under
   DDSCL_NORMAL the Primary uses the desktop format while our game buffers
   are RGB565.  Render Bink into renderer+8 (Back), then reuse the game's own
   windowed Back-to-Primary presentation routine. */
static size_t BuildBinkPresentHook(uint8_t *buffer,
                                   uint32_t remote_address) {
    g_emit_overflow = false;
    size_t offset = 0;

    /* void __thiscall hook(renderer*, IDirectDrawSurface4* ignored_default).
       Preserve ESI because sub_401BA0 keeps its movie object there. */
    offset = EmitByte(buffer, offset, 0x56);                         /* push esi */
    offset = EmitByte(buffer, offset, 0x8B); offset = EmitByte(buffer, offset, 0xF1); /* mov esi,ecx */
    /* At entry [ESP] is the return address and [ESP+4] is arg0.  After
       PUSH ESI, the wrapped sub_4062C0 argument is at [ESP+8]. */
    offset = EmitByte(buffer, offset, 0xFF); offset = EmitByte(buffer, offset, 0x74);
    offset = EmitByte(buffer, offset, 0x24); offset = EmitByte(buffer, offset, 0x08); /* push [esp+8] */
    offset = EmitByte(buffer, offset, 0xE8);
    int32_t unlock_displacement = 0;
    if (!CalculateRel32(remote_address + (uint32_t)offset + 4u,
                        BINK_UNLOCK_FUNCTION_ADDRESS,
                        &unlock_displacement)) {
        return 0;
    }
    offset = EmitDword(buffer, offset, (uint32_t)unlock_displacement);
    offset = EmitByte(buffer, offset, 0x8B); offset = EmitByte(buffer, offset, 0xCE); /* mov ecx,esi */
    offset = EmitByte(buffer, offset, 0xE8);
    int32_t present_displacement = 0;
    if (!CalculateRel32(remote_address + (uint32_t)offset + 4u,
                        WINDOWED_PRESENT_FUNCTION_ADDRESS,
                        &present_displacement)) {
        return 0;
    }
    offset = EmitDword(buffer, offset, (uint32_t)present_displacement);
    offset = EmitByte(buffer, offset, 0x5E);                         /* pop esi */
    offset = EmitByte(buffer, offset, 0xC2); offset = EmitByte(buffer, offset, 0x04);
    offset = EmitByte(buffer, offset, 0x00);                         /* ret 4 */
    return g_emit_overflow ? 0 : offset;
}

static bool InstallBinkWindowedHook(HANDLE process) {
    const PatchSite fixed_sites[] = {
        {BINK_LOCK_DEFAULT_SURFACE_ADDRESS,
         kBinkLockDefaultSurfaceExpected,
         kBinkLockDefaultSurfaceReplacement,
         PATCH_LENGTH(kBinkLockDefaultSurfaceExpected)},
        {BINK_UNLOCK_DEFAULT_SURFACE_ADDRESS,
         kBinkUnlockDefaultSurfaceExpected,
         kBinkUnlockDefaultSurfaceReplacement,
         PATCH_LENGTH(kBinkUnlockDefaultSurfaceExpected)},
        {BINK_SURFACE_FORMAT_ADDRESS,
         kBinkSurfaceFormatExpected,
         kBinkSurfaceFormatReplacement,
         PATCH_LENGTH(kBinkSurfaceFormatExpected)},
        {BINK_PITCH_ADDRESS,
         kBinkPitchExpected,
         kBinkPitchReplacement,
         PATCH_LENGTH(kBinkPitchExpected)},
    };
    const PatchGroup fixed_group = {
        L"Bink 窗口渲染目标",
        fixed_sites,
        _countof(fixed_sites),
    };

    uint8_t current_call[sizeof(kBinkUnlockCallExpected)];
    if (!ReadProcessExact(process,
                          BINK_UNLOCK_CALL_ADDRESS,
                          current_call,
                          sizeof(current_call)) ||
        memcmp(current_call,
               kBinkUnlockCallExpected,
               sizeof(current_call)) != 0) {
        SetLastError(ERROR_REVISION_MISMATCH);
        return false;
    }

    /* Validate all fixed sites before allocating or changing any code. */
    for (size_t index = 0; index < fixed_group.count; ++index) {
        const PatchSite *site = &fixed_group.sites[index];
        uint8_t current[8];
        if (site->length > (uint32_t)sizeof(current) ||
            !ReadProcessExact(process, site->address, current, site->length) ||
            memcmp(current, site->expected, site->length) != 0) {
            SetLastError(ERROR_REVISION_MISMATCH);
            return false;
        }
    }

    uint8_t hook[REMOTE_HOOK_CAPACITY] = {0};
    LPVOID remote_memory = VirtualAllocEx(process,
                                          NULL,
                                          sizeof(hook),
                                          MEM_RESERVE | MEM_COMMIT,
                                          PAGE_READWRITE);
    if (remote_memory == NULL || (uintptr_t)remote_memory > UINT32_MAX) {
        if (remote_memory != NULL) VirtualFreeEx(process, remote_memory, 0, MEM_RELEASE);
        SetLastError(ERROR_NOT_SUPPORTED);
        return false;
    }

    uint32_t remote_address = (uint32_t)(uintptr_t)remote_memory;
    size_t hook_size = BuildBinkPresentHook(hook, remote_address);
    SIZE_T written = 0;
    if (hook_size == 0 ||
        !WriteProcessMemory(process, remote_memory, hook, hook_size, &written) ||
        written != hook_size) {
        VirtualFreeEx(process, remote_memory, 0, MEM_RELEASE);
        if (hook_size == 0) SetLastError(ERROR_BUFFER_OVERFLOW);
        return false;
    }

    DWORD old_protection = 0;
    if (!VirtualProtectEx(process,
                          remote_memory,
                          sizeof(hook),
                          PAGE_EXECUTE_READ,
                          &old_protection)) {
        VirtualFreeEx(process, remote_memory, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(process, remote_memory, hook_size);

    if (!ApplyPatchGroup(process, &fixed_group)) {
        VirtualFreeEx(process, remote_memory, 0, MEM_RELEASE);
        return false;
    }

    uint8_t call[sizeof(kBinkUnlockCallExpected)] = {0xE8, 0, 0, 0, 0};
    int32_t displacement = 0;
    if (!CalculateRel32(BINK_UNLOCK_CALL_ADDRESS + (uint32_t)sizeof(call),
                        remote_address,
                        &displacement)) {
        VirtualFreeEx(process, remote_memory, 0, MEM_RELEASE);
        SetLastError(ERROR_NOT_SUPPORTED);
        return false;
    }
    memcpy(call + 1, &displacement, sizeof(displacement));
    if (!WriteProtected(process,
                        BINK_UNLOCK_CALL_ADDRESS,
                        call,
                        sizeof(call))) {
        VirtualFreeEx(process, remote_memory, 0, MEM_RELEASE);
        return false;
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

enum {
    RUNTIME_STUB_PATH_OFFSET = 0x00,
    RUNTIME_STUB_KERNEL_NAME_OFFSET = 0x220,
    RUNTIME_STUB_LOAD_NAME_OFFSET = 0x240,
    RUNTIME_STUB_START_NAME_OFFSET = 0x260,
    RUNTIME_STUB_DATA_SIZE = 0x300,
};

static size_t BuildRuntimeLoaderStub(uint8_t *buffer,
                                     uint32_t remote_address,
                                     uint32_t data_address) {
    g_emit_overflow = false;
    size_t offset = 0;
    size_t loop_offset = 0;
    size_t jump_no_kernel;
    size_t jump_no_loader;
    size_t jump_retry;
    size_t retry_exhausted_offset;

    offset = EmitByte(buffer, offset, 0x55); /* push ebp */
    offset = EmitByte(buffer, offset, 0x8B); offset = EmitByte(buffer, offset, 0xEC);
    /* ESI is used as the bounded retry counter.  A remote thread entry is
       still a normal WINAPI callee, so preserve this nonvolatile register
       across both the success and exhaustion returns. */
    offset = EmitByte(buffer, offset, 0x56); /* push esi */
    /* The loader is a standalone remote thread, so ESI is available as a
       bounded retry counter.  Keeping the retry finite lets the caller
       reclaim the temporary code/data allocations even if the target exits
       or its imports never become usable. */
    offset = EmitByte(buffer, offset, 0xBE); /* mov esi, 400 */
    offset = EmitDword(buffer, offset, 400u);
    loop_offset = offset;

    /* The target's import slots can still be zero/FFFF while its suspended
       loader is finishing.  Check the slot before calling it; jumping through
       FFFFFFFF here was the original low-probability startup crash. */
    offset = EmitByte(buffer, offset, 0xA1);
    offset = EmitDword(buffer, offset, GET_MODULE_HANDLE_IAT_ADDRESS);
    offset = EmitByte(buffer, offset, 0x83);
    offset = EmitByte(buffer, offset, 0xF8);
    offset = EmitByte(buffer, offset, 0xFF);
    offset = EmitByte(buffer, offset, 0x0F);
    offset = EmitByte(buffer, offset, 0x84);
    jump_no_kernel = offset; offset += 4u;
    offset = EmitByte(buffer, offset, 0x85);
    offset = EmitByte(buffer, offset, 0xC0);
    offset = EmitByte(buffer, offset, 0x0F);
    offset = EmitByte(buffer, offset, 0x84);
    size_t jump_zero_kernel = offset; offset += 4u;
    EmitPushImm(buffer, &offset, data_address + RUNTIME_STUB_KERNEL_NAME_OFFSET);
    offset = EmitByte(buffer, offset, 0xFF);
    offset = EmitByte(buffer, offset, 0xD0);
    offset = EmitByte(buffer, offset, 0x85); offset = EmitByte(buffer, offset, 0xC0);
    offset = EmitByte(buffer, offset, 0x0F); offset = EmitByte(buffer, offset, 0x84);
    size_t jump_no_module = offset; offset += 4u;

    /* Resolve GetProcAddress before pushing its arguments so every retry path
       leaves the stack balanced. */
    offset = EmitByte(buffer, offset, 0x89); offset = EmitByte(buffer, offset, 0xC2); /* mov edx,eax */
    offset = EmitByte(buffer, offset, 0xA1);
    offset = EmitDword(buffer, offset, GET_PROC_ADDRESS_IAT_ADDRESS);
    offset = EmitByte(buffer, offset, 0x83);
    offset = EmitByte(buffer, offset, 0xF8);
    offset = EmitByte(buffer, offset, 0xFF);
    offset = EmitByte(buffer, offset, 0x0F);
    offset = EmitByte(buffer, offset, 0x84);
    jump_no_loader = offset; offset += 4u;
    offset = EmitByte(buffer, offset, 0x85);
    offset = EmitByte(buffer, offset, 0xC0);
    offset = EmitByte(buffer, offset, 0x0F);
    offset = EmitByte(buffer, offset, 0x84);
    size_t jump_zero_loader = offset; offset += 4u;
    EmitPushImm(buffer, &offset, data_address + RUNTIME_STUB_LOAD_NAME_OFFSET);
    offset = EmitByte(buffer, offset, 0x52); /* push kernel32 */
    offset = EmitByte(buffer, offset, 0xFF);
    offset = EmitByte(buffer, offset, 0xD0);
    offset = EmitByte(buffer, offset, 0x85); offset = EmitByte(buffer, offset, 0xC0);
    offset = EmitByte(buffer, offset, 0x0F); offset = EmitByte(buffer, offset, 0x84);
    size_t jump_no_load_library = offset; offset += 4u;

    EmitPushImm(buffer, &offset, data_address + RUNTIME_STUB_PATH_OFFSET);
    offset = EmitByte(buffer, offset, 0xFF); offset = EmitByte(buffer, offset, 0xD0); /* call LoadLibraryW */
    offset = EmitByte(buffer, offset, 0x85); offset = EmitByte(buffer, offset, 0xC0);
    offset = EmitByte(buffer, offset, 0x0F); offset = EmitByte(buffer, offset, 0x84);
    size_t jump_no_runtime_module = offset; offset += 4u;
    /* Resolve and call CastleRuntimeStart after LoadLibrary has returned. */
    offset = EmitByte(buffer, offset, 0x89); offset = EmitByte(buffer, offset, 0xC2); /* mov edx,eax */
    /* Resolve GetProcAddress again after LoadLibraryW returned. */
    offset = EmitByte(buffer, offset, 0xA1);
    offset = EmitDword(buffer, offset, GET_PROC_ADDRESS_IAT_ADDRESS);
    offset = EmitByte(buffer, offset, 0x83);
    offset = EmitByte(buffer, offset, 0xF8);
    offset = EmitByte(buffer, offset, 0xFF);
    offset = EmitByte(buffer, offset, 0x0F);
    offset = EmitByte(buffer, offset, 0x84);
    size_t jump_no_loader_start = offset; offset += 4u;
    offset = EmitByte(buffer, offset, 0x85);
    offset = EmitByte(buffer, offset, 0xC0);
    offset = EmitByte(buffer, offset, 0x0F);
    offset = EmitByte(buffer, offset, 0x84);
    size_t jump_zero_loader_start = offset; offset += 4u;
    EmitPushImm(buffer, &offset, data_address + RUNTIME_STUB_START_NAME_OFFSET);
    offset = EmitByte(buffer, offset, 0x52); /* push module handle */
    offset = EmitByte(buffer, offset, 0xFF);
    offset = EmitByte(buffer, offset, 0xD0);
    offset = EmitByte(buffer, offset, 0x85); offset = EmitByte(buffer, offset, 0xC0);
    /* If the export is missing, skip only the indirect call.  The previous
       displacement (04h) also skipped `pop ebp`, so the thread returned by
       popping the saved frame pointer as its return address. */
    offset = EmitByte(buffer, offset, 0x74); offset = EmitByte(buffer, offset, 0x02);
    offset = EmitByte(buffer, offset, 0xFF); offset = EmitByte(buffer, offset, 0xD0);
    offset = EmitByte(buffer, offset, 0x5E); /* pop esi */
    offset = EmitByte(buffer, offset, 0x5D); /* pop ebp */
    /* CreateRemoteThread invokes an LPTHREAD_START_ROUTINE (WINAPI/stdcall),
       so discard its single lpParameter argument on return. */
    offset = EmitByte(buffer, offset, 0xC2);
    offset = EmitByte(buffer, offset, 0x04);
    offset = EmitByte(buffer, offset, 0x00); /* ret 4 */

    size_t retry_offset = offset;
    /* Never call Sleep through the target IAT here.  This thread can run
       while the process is still completing loader initialization, and an
       unresolved IAT slot is exactly the low-probability crash we are
       avoiding.  A PAUSE-backed bounded spin is self-contained. */
    offset = EmitByte(buffer, offset, 0x4E); /* dec esi */
    offset = EmitByte(buffer, offset, 0x0F);
    offset = EmitByte(buffer, offset, 0x84); /* jz exhausted */
    size_t jump_exhausted = offset; offset += 4u;
    offset = EmitByte(buffer, offset, 0xF3); /* pause */
    offset = EmitByte(buffer, offset, 0x90);
    offset = EmitByte(buffer, offset, 0xB9); /* mov ecx, 0x00010000 */
    offset = EmitDword(buffer, offset, 0x00010000u);
    size_t delay_inner_offset = offset;
    offset = EmitByte(buffer, offset, 0xF3);
    offset = EmitByte(buffer, offset, 0x90);
    offset = EmitByte(buffer, offset, 0x49); /* dec ecx */
    offset = EmitByte(buffer, offset, 0x75); /* jnz delay_inner */
    int32_t delay_inner_displacement = (int32_t)delay_inner_offset -
                                       (int32_t)(offset + 1u);
    offset = EmitByte(buffer, offset, (uint8_t)(int8_t)delay_inner_displacement);
    offset = EmitByte(buffer, offset, 0xE9);
    jump_retry = offset; offset += 4u;

    retry_exhausted_offset = offset;
    offset = EmitByte(buffer, offset, 0x31); offset = EmitByte(buffer, offset, 0xC0); /* xor eax,eax */
    offset = EmitByte(buffer, offset, 0x5E); /* pop esi */
    offset = EmitByte(buffer, offset, 0x5D); /* pop ebp */
    offset = EmitByte(buffer, offset, 0xC2);
    offset = EmitByte(buffer, offset, 0x04);
    offset = EmitByte(buffer, offset, 0x00); /* ret 4 */

    PatchConditionalJump(buffer, jump_no_kernel, retry_offset, 0x74, 0x84);
    PatchConditionalJump(buffer, jump_zero_kernel, retry_offset, 0x74, 0x84);
    PatchConditionalJump(buffer, jump_no_module, retry_offset, 0x74, 0x84);
    PatchConditionalJump(buffer, jump_no_loader, retry_offset, 0x74, 0x84);
    PatchConditionalJump(buffer, jump_zero_loader, retry_offset, 0x74, 0x84);
    PatchConditionalJump(buffer, jump_no_load_library, retry_offset, 0x74, 0x84);
    PatchConditionalJump(buffer, jump_no_runtime_module, retry_offset, 0x74, 0x84);
    PatchConditionalJump(buffer, jump_no_loader_start, retry_offset, 0x74, 0x84);
    PatchConditionalJump(buffer, jump_zero_loader_start, retry_offset, 0x74, 0x84);
    /* This branch uses the same six-byte 0F 84 form as the other retry
       guards; keep it with the generic conditional-jump patching path. */
    PatchConditionalJump(buffer, jump_exhausted, retry_exhausted_offset, 0x74, 0x84);
    PatchNearJump(buffer, jump_retry, loop_offset);
    (void)remote_address;
    return g_emit_overflow ? 0 : offset;
}

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
                                        REMOTE_HOOK_CAPACITY,
                                        MEM_RESERVE | MEM_COMMIT,
                                        PAGE_READWRITE);
    if (remote_data == NULL || remote_code == NULL ||
        (uintptr_t)remote_data > UINT32_MAX || (uintptr_t)remote_code > UINT32_MAX) {
        if (remote_data != NULL) VirtualFreeEx(process, remote_data, 0, MEM_RELEASE);
        if (remote_code != NULL) VirtualFreeEx(process, remote_code, 0, MEM_RELEASE);
        SetLastError(ERROR_NOT_SUPPORTED);
        return false;
    }

    uint32_t data_address = (uint32_t)(uintptr_t)remote_data;
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

    uint8_t code[REMOTE_HOOK_CAPACITY] = {0};
    size_t code_size = BuildRuntimeLoaderStub(code, code_address, data_address);
    if (code_size == 0 ||
        !WriteProcessMemory(process, remote_code, code, code_size, &written) || written != code_size) {
        VirtualFreeEx(process, remote_data, 0, MEM_RELEASE);
        VirtualFreeEx(process, remote_code, 0, MEM_RELEASE);
        if (code_size == 0) SetLastError(ERROR_BUFFER_OVERFLOW);
        return false;
    }
    DWORD old_protection = 0;
    if (!VirtualProtectEx(process,
                          remote_code,
                          REMOTE_HOOK_CAPACITY,
                          PAGE_EXECUTE_READ,
                          &old_protection)) {
        VirtualFreeEx(process, remote_data, 0, MEM_RELEASE);
        VirtualFreeEx(process, remote_code, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(process, remote_code, code_size);

    HANDLE thread = CreateRemoteThread(process,
                                       NULL,
                                       0,
                                       (LPTHREAD_START_ROUTINE)(uintptr_t)code_address,
                                       NULL,
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
    if (wait_result == WAIT_OBJECT_0 && exit_code == 0) {
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

    if (!options->windowed) {
        return true;
    }

    /* Windowed mode is different: its exact code sites are prerequisites for
       the surface and Bink hooks below, so a mismatch must abort.  Cursor,
       resize and clipping are installed by the target-process runtime module. */
    if (!ApplyPatchGroup(process, &kWindowedGroup)) {
        return false;
    }

    /* The windowed Primary surface must use the desktop's native format;
       DirectDraw rejects a caller-supplied RGB565 pixel format for a Primary
       surface under DDSCL_NORMAL.  Only the shared Back/OffScreen helper is
       eligible for the guarded format hook below. */
    if (!InstallSurfaceFormatHook(process) ||
        !InstallBinkWindowedHook(process)) {
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
        .scale2 = ScaleIndexToValue((int)SendMessageW(g_scale_combo, CB_GETCURSEL, 0, 0)),
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

    if (options.windowed && !InjectRuntimeDll(process_info.hProcess)) {
        DWORD error = GetLastError();
        TerminateProcess(process_info.hProcess, 1);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        SetLastError(error);
        ShowWin32Error(owner, L"注入目标进程运行时模块");
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

    AddControl(window, L"STATIC", L"显示模式", SS_LEFT, 360, 18, 140, 22, -1);
    AddControl(window, L"BUTTON", L"窗口模式（修正鼠标坐标）", BS_AUTOCHECKBOX, 360, 44, 220, 24, ID_WINDOWED);
    AddControl(window, L"BUTTON", L"锁定鼠标在窗口内", BS_AUTOCHECKBOX, 360, 70, 220, 24, ID_CURSOR_LOCK);
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
               210,
               552,
               22,
               -1);
    AddControl(window, L"BUTTON", L"应用补丁并启动游戏", BS_DEFPUSHBUTTON, 360, 240, 210, 32, ID_LAUNCH);
    g_status = AddControl(window, L"STATIC", L"等待启动。", SS_LEFT, 18, 248, 330, 22, ID_STATUS);

    CheckDlgButton(window, ID_NO_CD, LoadConfigBool(L"NoCd", true) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, ID_MINGYU_FIX, LoadConfigBool(L"MingyuFix", true) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, ID_RESISTANCE_FIX, LoadConfigBool(L"ResistanceFix", true) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, ID_ANYWHERE_SAVE, LoadConfigBool(L"AnywhereSave", true) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, ID_MAX_GROWTH, LoadConfigBool(L"MaxGrowth", false) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, ID_MAX_LOOT, LoadConfigBool(L"MaxLoot", false) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, ID_WINDOWED, LoadConfigDisplayBool(L"Windowed", false) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, ID_CURSOR_LOCK, LoadConfigDisplayBool(L"CursorLock", false) ? BST_CHECKED : BST_UNCHECKED);
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
                                  L"Castle Patches",
                                  WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                  CW_USEDEFAULT,
                                  CW_USEDEFAULT,
                                  604,
                                  320,
                                  NULL,
                                  NULL,
                                  instance,
                                  NULL);
    if (window == NULL) {
        return 1;
    }
    g_main_window = window;

    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return (int)message.wParam;
}
