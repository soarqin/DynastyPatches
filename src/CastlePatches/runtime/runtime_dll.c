#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define GAME_WINDOW_HANDLE_ADDRESS ((uintptr_t)0x0046F384u)
#define SET_CURSOR_POS_IAT_ADDRESS ((uintptr_t)0x0046019Cu)
#define GET_CURSOR_POS_IAT_ADDRESS ((uintptr_t)0x00460204u)
#define EXPERIENCE_GAIN_HOOK_ADDRESS ((uintptr_t)0x00443856u)
#define EXPERIENCE_DISPLAY_HOOK_ADDRESS ((uintptr_t)0x004437F7u)
#define MONEY_GAIN_HOOK_ADDRESS ((uintptr_t)0x00439824u)
#define MONEY_DISPLAY_HOOK_ADDRESS ((uintptr_t)0x004439AFu)
#define ENCOUNTER_INITIAL_HOOK_ADDRESS ((uintptr_t)0x004093C7u)
#define ENCOUNTER_REGENERATION_HOOK_ADDRESS ((uintptr_t)0x00403761u)
#define ENCOUNTER_THRESHOLD_ADDRESS ((uintptr_t)0x0046F614u)
#define ENCOUNTER_PROGRESS_ADDRESS ((uintptr_t)0x0046F618u)
#define ENCOUNTER_MAP_SPECIAL_FLAG_ADDRESS ((uintptr_t)0x0046F624u)
#define ENCOUNTER_INIT_FUNCTION_ADDRESS ((uintptr_t)0x00403510u)
#define ENCOUNTER_NEXT_THRESHOLD_ADDRESS ((uintptr_t)0x0044B0D0u)
#define ENCOUNTER_INITIAL_RETURN_ADDRESS ((uintptr_t)0x004093D3u)
#define ENCOUNTER_REGENERATION_RETURN_ADDRESS ((uintptr_t)0x00403769u)
#define ENCOUNTER_REGENERATION_SKIP_ADDRESS ((uintptr_t)0x004037D7u)
#define ENCOUNTER_HOOK_ALLOCATION_SIZE 0x80u
#define ENCOUNTER_FEEDBACK_MESSAGE (WM_APP + 0x3A1u)
#define GAME_CLIENT_WIDTH 640
#define GAME_CLIENT_HEIGHT 480

typedef BOOL (WINAPI *GetCursorPosFn)(LPPOINT);
typedef BOOL (WINAPI *SetCursorPosFn)(int, int);
typedef int (__cdecl *EncounterInitFn)(int);

typedef size_t (*BuildGeneratedHookFn)(uint8_t *buffer, size_t capacity);

static uint32_t GetEncounterRate(void);

static HINSTANCE g_instance;
static GetCursorPosFn g_original_get_cursor_pos;
static SetCursorPosFn g_original_set_cursor_pos;
static volatile LONG g_cursor_hooks_installed;
static uint32_t g_scale2 = 2;
static uint32_t g_experience_multiplier = 1;
static uint32_t g_money_multiplier = 1;
static bool g_cursor_lock;
static HWND volatile g_window;
static uint8_t *g_hook_block;
static size_t g_hook_block_offset;
static bool g_dynamic_encounter_rate;
static bool g_windowed;
static bool g_encounter_map_enabled;
static wchar_t g_original_window_title[256];
static WNDPROC g_original_window_proc;
static volatile LONG g_encounter_rate_percent = 100;
static volatile LONG g_title_restore_due;
static volatile LONG g_window_proc_installed;

enum {
    WINDOW_PROC_NONE = 0,
    WINDOW_PROC_INSTALLING = 1,
    WINDOW_PROC_INSTALLED = 2,
    WINDOW_PROC_DESTROYED = 3,
};

static bool GetConfigPath(wchar_t *path, size_t capacity) {
    DWORD length = GetModuleFileNameW(g_instance, path, (DWORD)capacity);
    if (length == 0 || length >= capacity) return false;
    wchar_t *slash = wcsrchr(path, L'\\');
    if (slash == NULL) return false;
    slash[1] = L'\0';
    return wcscat_s(path, capacity, L"CastlePatches.ini") == 0;
}

static uint32_t LoadScale(void) {
    wchar_t path[MAX_PATH];
    if (!GetConfigPath(path, _countof(path))) return 2;
    int value = GetPrivateProfileIntW(L"Display", L"Scale2", 2, path);
    return value >= 2 && value <= 6 ? (uint32_t)value : 2;
}

static uint32_t LoadMultiplier(const wchar_t *key) {
    wchar_t path[MAX_PATH];
    if (!GetConfigPath(path, _countof(path))) return 1;
    int value = GetPrivateProfileIntW(L"Patches", key, 1, path);
    return value < 1 ? 1u : value > 10 ? 10u : (uint32_t)value;
}

static bool LoadBool(const wchar_t *key, bool default_value) {
    wchar_t path[MAX_PATH];
    if (!GetConfigPath(path, _countof(path))) return default_value;
    return GetPrivateProfileIntW(L"Display", key, default_value ? 1 : 0, path) != 0;
}

static bool LoadPatchBool(const wchar_t *key, bool default_value) {
    wchar_t path[MAX_PATH];
    if (!GetConfigPath(path, _countof(path))) return default_value;
    return GetPrivateProfileIntW(L"Patches", key, default_value ? 1 : 0, path) != 0;
}

static HWND ReadRuntimeWindow(void) {
    return (HWND)InterlockedCompareExchangePointer((PVOID volatile *)&g_window, NULL, NULL);
}

static void WriteRuntimeWindow(HWND window) {
    InterlockedExchangePointer((PVOID volatile *)&g_window, window);
}

static HWND ReadGameWindow(void) {
    return *(HWND *)(uintptr_t)GAME_WINDOW_HANDLE_ADDRESS;
}

static BOOL WINAPI RuntimeGetCursorPos(LPPOINT point) {
    if (g_original_get_cursor_pos == NULL || point == NULL || !g_original_get_cursor_pos(point)) return FALSE;
    HWND window = ReadRuntimeWindow();
    if (window == NULL) window = ReadGameWindow();
    if (window == NULL || !IsWindow(window) || !ScreenToClient(window, point)) return FALSE;
    point->x = (LONG)(((int64_t)point->x * 2) / (int64_t)g_scale2);
    point->y = (LONG)(((int64_t)point->y * 2) / (int64_t)g_scale2);
    return TRUE;
}

static BOOL WINAPI RuntimeSetCursorPos(int x, int y) {
    if (g_original_set_cursor_pos == NULL) return FALSE;
    HWND window = ReadRuntimeWindow();
    if (window == NULL) window = ReadGameWindow();
    if (window == NULL || !IsWindow(window)) return FALSE;
    POINT point = {
        (LONG)(((int64_t)x * g_scale2) / 2),
        (LONG)(((int64_t)y * g_scale2) / 2),
    };
    return ClientToScreen(window, &point) && g_original_set_cursor_pos(point.x, point.y);
}

static bool InstallCursorHooks(void) {
    if (InterlockedCompareExchange(&g_cursor_hooks_installed, 1, 0) != 0) return true;
    volatile SetCursorPosFn *set_iat = (volatile SetCursorPosFn *)(uintptr_t)SET_CURSOR_POS_IAT_ADDRESS;
    volatile GetCursorPosFn *get_iat = (volatile GetCursorPosFn *)(uintptr_t)GET_CURSOR_POS_IAT_ADDRESS;
    g_original_set_cursor_pos = *set_iat;
    g_original_get_cursor_pos = *get_iat;
    if (g_original_set_cursor_pos == NULL || g_original_get_cursor_pos == NULL) return false;

    uintptr_t start = SET_CURSOR_POS_IAT_ADDRESS;
    SIZE_T length = GET_CURSOR_POS_IAT_ADDRESS + sizeof(*get_iat) - start;
    DWORD old_protection = 0;
    if (!VirtualProtect((LPVOID)start, length, PAGE_READWRITE, &old_protection)) return false;
    *set_iat = RuntimeSetCursorPos;
    *get_iat = RuntimeGetCursorPos;
    DWORD ignored = 0;
    VirtualProtect((LPVOID)start, length, old_protection, &ignored);
    return true;
}

static bool WriteCode(uintptr_t address, const void *data, size_t length) {
    DWORD old_protection = 0;
    if (!VirtualProtect((LPVOID)address, length, PAGE_EXECUTE_READWRITE, &old_protection)) return false;
    memcpy((void *)address, data, length);
    DWORD ignored = 0;
    bool restored = VirtualProtect((LPVOID)address, length, old_protection, &ignored) != FALSE;
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)address, length);
    return restored;
}

static uint8_t *AllocateHookCode(size_t length, uintptr_t hook_address) {
    const size_t allocation_size = 0x1000;
    size_t aligned_length = (length + 15u) & ~((size_t)15u);
    if (g_hook_block != NULL && g_hook_block_offset + aligned_length <= allocation_size) {
        uint8_t *code = g_hook_block + g_hook_block_offset;
        int64_t displacement = (int64_t)(uintptr_t)code - (int64_t)(hook_address + 5u);
        if (displacement >= INT32_MIN && displacement <= INT32_MAX) {
            g_hook_block_offset += aligned_length;
            return code;
        }
    }

    static const uintptr_t hints[] = {0x10000000u, 0x20000000u, 0x30000000u, 0x50000000u, 0x60000000u};
    for (size_t index = 0; index < _countof(hints); ++index) {
        uint8_t *code = VirtualAlloc((LPVOID)hints[index], allocation_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (code == NULL) continue;
        int64_t displacement = (int64_t)(uintptr_t)code - (int64_t)(hook_address + 5u);
        if (displacement >= INT32_MIN && displacement <= INT32_MAX) {
            g_hook_block = code;
            g_hook_block_offset = aligned_length;
            return code;
        }
        VirtualFree(code, 0, MEM_RELEASE);
    }
    return NULL;
}

static bool InstallHook(uintptr_t address,
                        const uint8_t *expected,
                        size_t expected_length,
                        const uint8_t *code,
                        size_t code_length,
                        size_t return_displacement_offset) {
    if (memcmp((const void *)address, expected, expected_length) != 0) return false;
    uint8_t *remote = AllocateHookCode(code_length, address);
    if (remote == NULL) return false;
    DWORD old_protection = 0;
    if (!VirtualProtect(remote, code_length, PAGE_READWRITE, &old_protection)) return false;
    memcpy(remote, code, code_length);

    int64_t return_displacement = (int64_t)(address + expected_length) -
                                  (int64_t)((uintptr_t)remote + return_displacement_offset + sizeof(int32_t));
    int64_t hook_displacement = (int64_t)(uintptr_t)remote - (int64_t)(address + 5u);
    if (return_displacement < INT32_MIN || return_displacement > INT32_MAX ||
        hook_displacement < INT32_MIN || hook_displacement > INT32_MAX) {
        VirtualProtect(remote, code_length, old_protection, &old_protection);
        return false;
    }
    memcpy(remote + return_displacement_offset, &(int32_t){(int32_t)return_displacement}, sizeof(int32_t));
    DWORD ignored = 0;
    if (!VirtualProtect(remote, code_length, PAGE_EXECUTE_READ, &ignored)) {
        return false;
    }

    uint8_t jump[16] = {0xE9};
    memcpy(jump + 1, &(int32_t){(int32_t)hook_displacement}, sizeof(int32_t));
    memset(jump + 5, 0x90, expected_length - 5);
    return WriteCode(address, jump, expected_length);
}

/*
static void __cdecl PresentProbeEnter(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    g_present_enter = now;
    if (g_present_previous.QuadPart != 0) {
        uint64_t interval = (uint64_t)(now.QuadPart - g_present_previous.QuadPart);
        g_present_interval_total += interval;
        if (interval < g_present_interval_min) g_present_interval_min = interval;
        if (interval > g_present_interval_max) g_present_interval_max = interval;
    }
    g_present_previous = now;
}

static double CounterMilliseconds(uint64_t ticks) {
    return (double)ticks * 1000.0 / (double)g_present_frequency.QuadPart;
}

static void __cdecl PresentProbeExit(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    uint64_t duration = (uint64_t)(now.QuadPart - g_present_enter.QuadPart);
    g_present_duration_total += duration;
    if (duration > g_present_duration_max) g_present_duration_max = duration;
    ++g_present_samples;
    if (g_present_samples < 100) return;

    uint32_t intervals = g_present_samples - 1u;
    wchar_t message[256];
    _snwprintf_s(message,
                 _countof(message),
                 _TRUNCATE,
                 L"CastleRuntime: present samples=%lu interval_ms(avg/min/max)=%.3f/%.3f/%.3f duration_ms(avg/max)=%.3f/%.3f\n",
                 (unsigned long)g_present_samples,
                 intervals != 0 ? CounterMilliseconds(g_present_interval_total) / intervals : 0.0,
                 CounterMilliseconds(g_present_interval_min == UINT64_MAX ? 0 : g_present_interval_min),
                 CounterMilliseconds(g_present_interval_max),
                 CounterMilliseconds(g_present_duration_total) / g_present_samples,
                 CounterMilliseconds(g_present_duration_max));
    OutputDebugStringW(message);

    g_present_interval_total = 0;
    g_present_duration_total = 0;
    g_present_interval_min = UINT64_MAX;
    g_present_interval_max = 0;
    g_present_duration_max = 0;
    g_present_samples = 0;
    g_present_previous.QuadPart = 0;
}

static bool InstallFramePacingDiagnostics(void) {
    static const uint8_t expected[] = {0xE8, 0xB1, 0x0A, 0x00, 0x00};
    uint8_t code[] = {
        0x9C, 0x60, 0xB8, 0, 0, 0, 0, 0xFF, 0xD0, 0x61, 0x9D,
        0xB8, 0, 0, 0, 0, 0xFF, 0xD0,
        0x9C, 0x60, 0xB8, 0, 0, 0, 0, 0xFF, 0xD0, 0x61, 0x9D,
        0xE9, 0, 0, 0, 0,
    };
    memcpy(code + 3, &(uint32_t){(uint32_t)(uintptr_t)PresentProbeEnter}, sizeof(uint32_t));
    memcpy(code + 12, &(uint32_t){(uint32_t)PRESENT_FUNCTION_ADDRESS}, sizeof(uint32_t));
    memcpy(code + 21, &(uint32_t){(uint32_t)(uintptr_t)PresentProbeExit}, sizeof(uint32_t));
    if (!QueryPerformanceFrequency(&g_present_frequency) || g_present_frequency.QuadPart == 0) return false;
    return InstallHook(PRESENT_CALL_HOOK_ADDRESS,
                       expected,
                       sizeof(expected),
                       code,
                       sizeof(code),
                       30);
}

static void LogDisplayEnvironment(void) {
    typedef HRESULT (WINAPI *DwmIsCompositionEnabledFn)(BOOL *enabled);
    BOOL composition_enabled = FALSE;
    HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
    if (dwmapi != NULL) {
        DwmIsCompositionEnabledFn is_composition_enabled =
            (DwmIsCompositionEnabledFn)(uintptr_t)GetProcAddress(dwmapi, "DwmIsCompositionEnabled");
        if (is_composition_enabled != NULL) is_composition_enabled(&composition_enabled);
        FreeLibrary(dwmapi);
    }

    DEVMODEW mode = {.dmSize = sizeof(mode)};
    BOOL have_mode = EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &mode);
    wchar_t message[192];
    _snwprintf_s(message,
                 _countof(message),
                 _TRUNCATE,
                 L"CastleRuntime: frame pacing diagnostics enabled; DWM=%ls display=%lux%lu %luHz %lubpp\n",
                 composition_enabled ? L"on" : L"off/unknown",
                 (unsigned long)(have_mode ? mode.dmPelsWidth : 0),
                 (unsigned long)(have_mode ? mode.dmPelsHeight : 0),
                 (unsigned long)(have_mode ? mode.dmDisplayFrequency : 0),
                 (unsigned long)(have_mode ? mode.dmBitsPerPel : 0));
    OutputDebugStringW(message);
}

*/
static bool IsEncounterStateInvalid(uint32_t threshold, uint32_t progress);

static uint32_t ScaleEncounterThreshold(uint32_t threshold, uint32_t rate) {
    if (rate == 0) return 0;
    uint64_t scaled = ((uint64_t)threshold * 100u + rate - 1u) / rate;
    if (scaled == 0) scaled = 1;
    return scaled > UINT32_MAX ? UINT32_MAX : (uint32_t)scaled;
}

static void ApplyEncounterThresholdRate(void) {
    uint32_t rate = (uint32_t)InterlockedCompareExchange(&g_encounter_rate_percent, 0, 0);
    volatile uint32_t *threshold = (volatile uint32_t *)ENCOUNTER_THRESHOLD_ADDRESS;
    volatile uint32_t *progress = (volatile uint32_t *)ENCOUNTER_PROGRESS_ADDRESS;
    if (IsEncounterStateInvalid(*threshold, *progress)) {
        OutputDebugStringW(L"CastleRuntime: invalid encounter state in threshold hook; resetting\n");
        *progress = 0;
        if (rate == 0 || !g_encounter_map_enabled) {
            *threshold = 0;
            return;
        }
        ((EncounterInitFn)ENCOUNTER_INIT_FUNCTION_ADDRESS)(1);
    }
    *threshold = ScaleEncounterThreshold(*threshold, rate);
}

static bool IsEncounterStateInvalid(uint32_t threshold, uint32_t progress) {
    if (threshold > 0x100000u || progress > 0x100000u) return true;
    if (threshold == 0) return progress != 0;
    return progress > threshold + 2u;
}

static void ResetInvalidEncounterState(void) {
    volatile uint32_t *threshold = (volatile uint32_t *)ENCOUNTER_THRESHOLD_ADDRESS;
    volatile uint32_t *progress = (volatile uint32_t *)ENCOUNTER_PROGRESS_ADDRESS;
    uint32_t rate = GetEncounterRate();
    OutputDebugStringW(L"CastleRuntime: invalid encounter state; resetting\n");
    *progress = 0;
    if (rate == 0 || !g_encounter_map_enabled) {
        *threshold = 0;
        return;
    }
    ((EncounterInitFn)ENCOUNTER_INIT_FUNCTION_ADDRESS)(1);
    ApplyEncounterThresholdRate();
}

static void ApplyEncounterInitialRate(uint32_t map_record) {
    g_encounter_map_enabled = *(const uint32_t *)(uintptr_t)(map_record + 0x378u) != 0;
    ApplyEncounterThresholdRate();
}

static bool EmitEncounterByte(uint8_t *buffer, size_t capacity, size_t *offset, uint8_t value) {
    if (*offset >= capacity) return false;
    buffer[(*offset)++] = value;
    return true;
}

static bool EmitEncounterDword(uint8_t *buffer, size_t capacity, size_t *offset, uint32_t value) {
    if (*offset > capacity - sizeof(value)) return false;
    memcpy(buffer + *offset, &value, sizeof(value));
    *offset += sizeof(value);
    return true;
}

static bool EmitEncounterAbsoluteCall(uint8_t *buffer,
                                      size_t capacity,
                                      size_t *offset,
                                      uintptr_t target) {
    return EmitEncounterByte(buffer, capacity, offset, 0xB8) &&
           EmitEncounterDword(buffer, capacity, offset, (uint32_t)target) &&
           EmitEncounterByte(buffer, capacity, offset, 0xFF) &&
           EmitEncounterByte(buffer, capacity, offset, 0xD0);
}

static bool EmitEncounterPushImm(uint8_t *buffer,
                                 size_t capacity,
                                 size_t *offset,
                                 uintptr_t value) {
    return EmitEncounterByte(buffer, capacity, offset, 0x68) &&
           EmitEncounterDword(buffer, capacity, offset, (uint32_t)value);
}

static size_t BuildEncounterInitialHook(uint8_t *buffer, size_t capacity) {
    size_t offset = 0;
    if (!EmitEncounterByte(buffer, capacity, &offset, 0x60) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x53) ||
        !EmitEncounterAbsoluteCall(buffer, capacity, &offset, (uintptr_t)&ApplyEncounterInitialRate) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x83) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0xC4) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x04) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x61) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x8B) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x8B) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x7C) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x03) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x00) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x00) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x51) ||
        !EmitEncounterAbsoluteCall(buffer, capacity, &offset, ENCOUNTER_NEXT_THRESHOLD_ADDRESS) ||
        !EmitEncounterPushImm(buffer, capacity, &offset, ENCOUNTER_INITIAL_RETURN_ADDRESS) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0xC3)) {
        return 0;
    }
    return offset;
}

static size_t BuildEncounterRegenerationHook(uint8_t *buffer, size_t capacity) {
    size_t offset = 0;
    if (!EmitEncounterByte(buffer, capacity, &offset, 0x60) ||
        !EmitEncounterAbsoluteCall(buffer, capacity, &offset, (uintptr_t)&ApplyEncounterThresholdRate) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x61) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x38) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x1D) ||
        !EmitEncounterDword(buffer, capacity, &offset, (uint32_t)ENCOUNTER_MAP_SPECIAL_FLAG_ADDRESS) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x75) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x06) ||
        !EmitEncounterPushImm(buffer, capacity, &offset, ENCOUNTER_REGENERATION_SKIP_ADDRESS) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0xC3) ||
        !EmitEncounterPushImm(buffer, capacity, &offset, ENCOUNTER_REGENERATION_RETURN_ADDRESS) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0xC3)) {
        return 0;
    }
    return offset;
}

static bool InstallGeneratedHook(uintptr_t address,
                                 const uint8_t *expected,
                                 size_t expected_length,
                                 BuildGeneratedHookFn builder) {
    if (expected_length < 5u || expected_length > 16u ||
        memcmp((const void *)address, expected, expected_length) != 0) {
        SetLastError(ERROR_REVISION_MISMATCH);
        return false;
    }
    uint8_t *remote = AllocateHookCode(ENCOUNTER_HOOK_ALLOCATION_SIZE, address);
    if (remote == NULL) return false;

    uint8_t code[ENCOUNTER_HOOK_ALLOCATION_SIZE] = {0};
    size_t code_length = builder(code, sizeof(code));
    if (code_length == 0) {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return false;
    }

    DWORD old_protection = 0;
    if (!VirtualProtect(remote, code_length, PAGE_READWRITE, &old_protection)) return false;
    memcpy(remote, code, code_length);
    DWORD ignored = 0;
    if (!VirtualProtect(remote, code_length, PAGE_EXECUTE_READ, &ignored)) return false;

    int64_t displacement = (int64_t)(uintptr_t)remote - (int64_t)(address + 5u);
    if (displacement < INT32_MIN || displacement > INT32_MAX) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return false;
    }

    uint8_t jump[16] = {0xE9};
    memcpy(jump + 1, &(int32_t){(int32_t)displacement}, sizeof(int32_t));
    memset(jump + 5, 0x90, expected_length - 5u);
    return WriteCode(address, jump, expected_length);
}

static DWORD InstallEncounterHooks(void) {
    static const uint8_t initial_expected[] = {
        0x8B, 0x8B, 0x7C, 0x03, 0x00, 0x00,
        0x51,
        0xE8, 0xFD, 0x1C, 0x04, 0x00,
    };
    static const uint8_t regeneration_expected[] = {
        0x38, 0x1D, 0x24, 0xF6, 0x46, 0x00,
        0x74, 0x6E,
    };

    if (memcmp((const void *)ENCOUNTER_INITIAL_HOOK_ADDRESS,
               initial_expected,
               sizeof(initial_expected)) != 0 ||
        memcmp((const void *)ENCOUNTER_REGENERATION_HOOK_ADDRESS,
               regeneration_expected,
               sizeof(regeneration_expected)) != 0) {
        SetLastError(ERROR_REVISION_MISMATCH);
        return 231;
    }

    if (!InstallGeneratedHook(ENCOUNTER_INITIAL_HOOK_ADDRESS,
                              initial_expected,
                              sizeof(initial_expected),
                              BuildEncounterInitialHook)) return 232;
    if (!InstallGeneratedHook(ENCOUNTER_REGENERATION_HOOK_ADDRESS,
                              regeneration_expected,
                              sizeof(regeneration_expected),
                              BuildEncounterRegenerationHook)) return 233;
    return 1;
}

static DWORD InstallSettlementHooks(void) {
    static const uint8_t experience_gain_expected[] = {0x03, 0xC3, 0x89, 0x47, 0x24};
    static const uint8_t experience_display_expected[] = {0x8B, 0x0D, 0xB0, 0xB2, 0x46, 0x00, 0x53};
    static const uint8_t money_gain_expected[] = {0x8B, 0x91, 0xD8, 0x5D, 0x00, 0x00, 0x03, 0xD0};
    static const uint8_t money_display_expected[] = {0x8B, 0x08, 0x8B, 0x50, 0x08, 0x51, 0x55, 0x52, 0x8D, 0x84, 0x24, 0xB4, 0x00, 0x00, 0x00};

    if (g_experience_multiplier != 1 &&
        (memcmp((const void *)EXPERIENCE_GAIN_HOOK_ADDRESS, experience_gain_expected, sizeof(experience_gain_expected)) != 0 ||
         memcmp((const void *)EXPERIENCE_DISPLAY_HOOK_ADDRESS, experience_display_expected, sizeof(experience_display_expected)) != 0)) return 211;
    if (g_money_multiplier != 1 &&
        (memcmp((const void *)MONEY_GAIN_HOOK_ADDRESS, money_gain_expected, sizeof(money_gain_expected)) != 0 ||
         memcmp((const void *)MONEY_DISPLAY_HOOK_ADDRESS, money_display_expected, sizeof(money_display_expected)) != 0)) return 221;

    /* 00443856: 03 C3             add eax, ebx
       00443858: 89 47 24          mov [edi+24h], eax
       The patched code begins with EAX = the old experience value. */
    static const uint8_t experience_gain_hook[] = {
        0x51,                         /* 00: push ecx */
        0x8B, 0xCB,                   /* 01: mov ecx, ebx */
        0x0F, 0xAF, 0x0D,             /* 03: imul ecx, dword ptr [imm32] */
        0, 0, 0, 0,                   /* 06: &g_experience_multiplier */
        0x03, 0xC1,                   /* 0A: add eax, ecx */
        0x59,                         /* 0C: pop ecx */
        0x89, 0x47, 0x24,             /* 0D: mov [edi+24h], eax */
        0xE9,                         /* 10: jmp rel32 */
        0, 0, 0, 0,                   /* 11: 0044385B - next instruction */
    };
    /* 004437F7: 8B 0D B0 B2 46 00 mov ecx, [46B2B0h]
       004437FD: 53                push ebx
       Replays the color lookup and replaces only the sprintf experience argument. */
    static const uint8_t experience_display_hook[] = {
        0x8B, 0x0D, 0xB0, 0xB2, 0x46, 0x00, /* 00: mov ecx, [0046B2B0h] */
        0x8B, 0xC3,                   /* 06: mov eax, ebx */
        0x0F, 0xAF, 0x05,             /* 08: imul eax, dword ptr [imm32] */
        0, 0, 0, 0,                   /* 0B: &g_experience_multiplier */
        0x50,                         /* 0F: push eax */
        0xE9,                         /* 10: jmp rel32 */
        0, 0, 0, 0,                   /* 11: 004437FE - next instruction */
    };
    /* 00439824: 8B 91 D8 5D 00 00 mov edx, [ecx+5DD8h]
       0043982A: 03 D0             add edx, eax
       Preserve EAX while adding EAX * multiplier to the stored money total. */
    static const uint8_t money_gain_hook[] = {
        0x8B, 0x91, 0xD8, 0x5D, 0x00, 0x00, /* 00: mov edx, [ecx+5DD8h] */
        0x50,                         /* 06: push eax */
        0x0F, 0xAF, 0x05,             /* 07: imul eax, dword ptr [imm32] */
        0, 0, 0, 0,                   /* 0A: &g_money_multiplier */
        0x03, 0xD0,                   /* 0E: add edx, eax */
        0x58,                         /* 10: pop eax */
        0xE9,                         /* 11: jmp rel32 */
        0, 0, 0, 0,                   /* 12: 0043982C - next instruction */
    };
    /* 004439AF: 8B 08             mov ecx, [eax]
       004439B1: 8B 50 08          mov edx, [eax+8]
       004439B4: 51 55 52          push ecx; push ebp; push edx
       Replays the setup and replaces only the pushed money display value. */
    static const uint8_t money_display_hook[] = {
        0x8B, 0x08,                   /* 00: mov ecx, [eax] */
        0x8B, 0x50, 0x08,             /* 02: mov edx, [eax+8] */
        0x51,                         /* 05: push ecx */
        0x8B, 0xC5,                   /* 06: mov eax, ebp */
        0x0F, 0xAF, 0x05,             /* 08: imul eax, dword ptr [imm32] */
        0, 0, 0, 0,                   /* 0B: &g_money_multiplier */
        0x50,                         /* 0F: push eax */
        0x52,                         /* 10: push edx */
        0x8D, 0x84, 0x24, 0xB4, 0x00, 0x00, 0x00, /* 11: lea eax, [esp+1BCh] */
        0xE9,                         /* 18: jmp rel32 */
        0, 0, 0, 0,                   /* 19: 004439BE - next instruction */
    };

    uint8_t experience_gain_code[sizeof(experience_gain_hook)];
    uint8_t experience_display_code[sizeof(experience_display_hook)];
    uint8_t money_gain_code[sizeof(money_gain_hook)];
    uint8_t money_display_code[sizeof(money_display_hook)];
    memcpy(experience_gain_code, experience_gain_hook, sizeof(experience_gain_code));
    memcpy(experience_display_code, experience_display_hook, sizeof(experience_display_code));
    memcpy(money_gain_code, money_gain_hook, sizeof(money_gain_code));
    memcpy(money_display_code, money_display_hook, sizeof(money_display_code));
    memcpy(experience_gain_code + 6, &(uint32_t){(uint32_t)(uintptr_t)&g_experience_multiplier}, sizeof(uint32_t));
    memcpy(experience_display_code + 11, &(uint32_t){(uint32_t)(uintptr_t)&g_experience_multiplier}, sizeof(uint32_t));
    memcpy(money_gain_code + 10, &(uint32_t){(uint32_t)(uintptr_t)&g_money_multiplier}, sizeof(uint32_t));
    memcpy(money_display_code + 11, &(uint32_t){(uint32_t)(uintptr_t)&g_money_multiplier}, sizeof(uint32_t));

    if (g_experience_multiplier != 1 &&
        !InstallHook(EXPERIENCE_GAIN_HOOK_ADDRESS, experience_gain_expected, sizeof(experience_gain_expected), experience_gain_code, sizeof(experience_gain_code), 17)) return 212;
    if (g_experience_multiplier != 1 &&
        !InstallHook(EXPERIENCE_DISPLAY_HOOK_ADDRESS, experience_display_expected, sizeof(experience_display_expected), experience_display_code, sizeof(experience_display_code), 17)) return 213;
    if (g_money_multiplier != 1 &&
        !InstallHook(MONEY_GAIN_HOOK_ADDRESS, money_gain_expected, sizeof(money_gain_expected), money_gain_code, sizeof(money_gain_code), 18)) return 222;
    if (g_money_multiplier != 1 &&
        !InstallHook(MONEY_DISPLAY_HOOK_ADDRESS, money_display_expected, sizeof(money_display_expected), money_display_code, sizeof(money_display_code), 25)) return 223;
    return 1;
}

static void ResizeGameWindow(HWND window) {
    LONG client_width = (LONG)(((uint64_t)GAME_CLIENT_WIDTH * g_scale2 + 1u) / 2u);
    LONG client_height = (LONG)(((uint64_t)GAME_CLIENT_HEIGHT * g_scale2 + 1u) / 2u);
    RECT rect = {0, 0, client_width, client_height};
    if (!AdjustWindowRectEx(&rect, (DWORD)GetWindowLongA(window, GWL_STYLE), GetMenu(window) != NULL,
                            (DWORD)GetWindowLongA(window, GWL_EXSTYLE))) return;
    LONG width = rect.right - rect.left;
    LONG height = rect.bottom - rect.top;
    RECT work_area = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    LONG x = work_area.left + ((work_area.right - work_area.left) - width) / 2;
    LONG y = work_area.top + ((work_area.bottom - work_area.top) - height) / 2;
    MoveWindow(window, x, y, width, height, TRUE);
}

static const uint32_t kEncounterRates[] = {0, 50, 75, 100, 150, 200, 300};

static size_t FindEncounterRateIndex(uint32_t rate) {
    for (size_t index = 0; index < _countof(kEncounterRates); ++index) {
        if (kEncounterRates[index] == rate) return index;
    }
    return 3;
}

static uint32_t GetEncounterRate(void) {
    uint32_t rate = (uint32_t)InterlockedCompareExchange(&g_encounter_rate_percent, 0, 0);
    size_t index = FindEncounterRateIndex(rate);
    if (kEncounterRates[index] != rate) {
        OutputDebugStringW(L"CastleRuntime: invalid encounter rate; restoring 100%\n");
        InterlockedExchange(&g_encounter_rate_percent, 100);
        return 100;
    }
    return rate;
}

static uint32_t ScaleEncounterStateValue(uint32_t value,
                                         uint32_t old_rate,
                                         uint32_t new_rate,
                                         bool round_up) {
    uint64_t scaled = (uint64_t)value * old_rate;
    if (round_up) scaled += new_rate - 1u;
    scaled /= new_rate;
    return scaled > UINT32_MAX ? UINT32_MAX : (uint32_t)scaled;
}

static void ShowEncounterRateFeedback(uint32_t rate, uint32_t old_rate) {
    uint32_t threshold = *(volatile uint32_t *)ENCOUNTER_THRESHOLD_ADDRESS;
    uint32_t progress = *(volatile uint32_t *)ENCOUNTER_PROGRESS_ADDRESS;
    wchar_t message[192];
    _snwprintf_s(message,
                 _countof(message),
                 _TRUNCATE,
                 L"CastleRuntime: encounter rate %lu%% -> %lu%%, threshold=%lu, progress=%lu\n",
                 (unsigned long)old_rate,
                 (unsigned long)rate,
                 (unsigned long)threshold,
                 (unsigned long)progress);
    OutputDebugStringW(message);

    if (g_windowed) {
        HWND window = ReadRuntimeWindow();
        wchar_t title[256];
        _snwprintf_s(title,
                     _countof(title),
                     _TRUNCATE,
                     L"遇敌率：%lu%%",
                     (unsigned long)rate);
        if (window != NULL) SetWindowTextW(window, title);
        InterlockedExchange(&g_title_restore_due, (LONG)(GetTickCount() + 1500u));
    } else {
        MessageBeep(MB_OK);
    }
}

static void SetEncounterRate(uint32_t requested_rate) {
    uint32_t old_rate = GetEncounterRate();
    uint32_t rate = kEncounterRates[FindEncounterRateIndex(requested_rate)];
    volatile uint32_t *threshold = (volatile uint32_t *)ENCOUNTER_THRESHOLD_ADDRESS;
    volatile uint32_t *progress = (volatile uint32_t *)ENCOUNTER_PROGRESS_ADDRESS;

    if (IsEncounterStateInvalid(*threshold, *progress) ||
        (old_rate == 0 && *threshold != 0)) {
        ResetInvalidEncounterState();
    }

    if (old_rate == rate) {
        ShowEncounterRateFeedback(rate, old_rate);
        return;
    }

    if (rate == 0) {
        *threshold = 0;
        *progress = 0;
        InterlockedExchange(&g_encounter_rate_percent, 0);
    } else if (old_rate == 0) {
        InterlockedExchange(&g_encounter_rate_percent, (LONG)rate);
        if (g_encounter_map_enabled) {
            ((EncounterInitFn)ENCOUNTER_INIT_FUNCTION_ADDRESS)(1);
            ApplyEncounterThresholdRate();
        } else {
            *threshold = 0;
            *progress = 0;
        }
    } else {
        uint32_t old_threshold = *threshold;
        uint32_t old_progress = *progress;
        if (old_threshold != 0) {
            uint32_t new_threshold = ScaleEncounterStateValue(old_threshold, old_rate, rate, true);
            uint32_t new_progress = ScaleEncounterStateValue(old_progress, old_rate, rate, false);
            if (new_threshold == 0) new_threshold = 1;
            if (new_progress >= new_threshold) new_progress = new_threshold - 1;
            *threshold = new_threshold;
            *progress = new_progress;
        }
        InterlockedExchange(&g_encounter_rate_percent, (LONG)rate);
    }

    ShowEncounterRateFeedback(rate, old_rate);
}

static LRESULT CALLBACK RuntimeWindowProcedure(HWND window,
                                               UINT message,
                                               WPARAM w_param,
                                               LPARAM l_param) {
    if (message == ENCOUNTER_FEEDBACK_MESSAGE) {
        if (InterlockedCompareExchange(&g_window_proc_installed, 0, 0) == WINDOW_PROC_INSTALLED &&
            InterlockedCompareExchange(&g_title_restore_due, 0, 0) == 0) {
            SetWindowTextW(window, g_original_window_title);
        }
        return 0;
    }

    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) &&
        (GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
        (l_param & (1u << 30)) == 0) {
        size_t index = FindEncounterRateIndex(GetEncounterRate());
        if (w_param == VK_F10) {
            SetEncounterRate(100);
            return 0;
        }
        if (w_param == VK_F11) {
            index = index == 0 ? _countof(kEncounterRates) - 1u : index - 1u;
            SetEncounterRate(kEncounterRates[index]);
            return 0;
        }
        if (w_param == VK_F12) {
            index = (index + 1u) % _countof(kEncounterRates);
            SetEncounterRate(kEncounterRates[index]);
            return 0;
        }
    }

    if (message == WM_NCDESTROY) {
        InterlockedExchange(&g_window_proc_installed, WINDOW_PROC_DESTROYED);
        WriteRuntimeWindow(NULL);
    }
    if (g_original_window_proc == NULL) return DefWindowProcW(window, message, w_param, l_param);
    LRESULT result = CallWindowProcW(g_original_window_proc, window, message, w_param, l_param);
    return result;
}

static bool InstallEncounterWindowProcedure(void) {
    HWND window = ReadRuntimeWindow();
    if (window == NULL || !IsWindow(window)) return false;
    InterlockedExchange(&g_window_proc_installed, WINDOW_PROC_INSTALLING);
    GetWindowTextW(window, g_original_window_title, (int)_countof(g_original_window_title));
    g_original_window_proc = DefWindowProcW;
    SetLastError(ERROR_SUCCESS);
    WNDPROC original = (WNDPROC)SetWindowLongPtrW(window,
                                                  GWLP_WNDPROC,
                                                  (LONG_PTR)RuntimeWindowProcedure);
    if (original == NULL && GetLastError() != ERROR_SUCCESS) {
        InterlockedExchange(&g_window_proc_installed, WINDOW_PROC_NONE);
        return false;
    }
    g_original_window_proc = original;
    return InterlockedCompareExchange(&g_window_proc_installed,
                                      WINDOW_PROC_INSTALLED,
                                      WINDOW_PROC_INSTALLING) == WINDOW_PROC_INSTALLING;
}

static void ProcessEncounterTitleFeedback(void) {
    if (InterlockedCompareExchange(&g_window_proc_installed, 0, 0) != WINDOW_PROC_INSTALLED || !g_windowed) return;
    HWND window = ReadRuntimeWindow();
    if (window == NULL) return;
    LONG due = InterlockedCompareExchange(&g_title_restore_due, 0, 0);
    if (due == 0 || (LONG)(GetTickCount() - (DWORD)due) < 0) return;
    if (PostMessageW(window, ENCOUNTER_FEEDBACK_MESSAGE, 0, 0)) {
        InterlockedCompareExchange(&g_title_restore_due, 0, due);
    }
}

static DWORD WINAPI RuntimeWorker(void *parameter) {
    (void)parameter;
    g_scale2 = LoadScale();
    g_windowed = LoadBool(L"Windowed", false);
    g_cursor_lock = LoadBool(L"CursorLock", false);
    for (int attempt = 0; attempt < 400; ++attempt) {
        HWND window = ReadGameWindow();
        if (window != NULL && IsWindow(window)) {
            WriteRuntimeWindow(window);
            break;
        }
        Sleep(25);
    }
    if (ReadRuntimeWindow() == NULL && g_dynamic_encounter_rate) {
        OutputDebugStringW(L"CastleRuntime: game window was not created within 10 seconds; continuing to wait\n");
        for (;;) {
            HWND window = ReadGameWindow();
            if (window != NULL && IsWindow(window)) {
                WriteRuntimeWindow(window);
                break;
            }
            Sleep(25);
        }
    }
    if (ReadRuntimeWindow() == NULL) return 0;
    if (g_dynamic_encounter_rate && !InstallEncounterWindowProcedure()) {
        OutputDebugStringW(L"CastleRuntime: failed to install encounter hotkey window procedure\n");
        g_dynamic_encounter_rate = false;
    }
    for (int attempt = 0; attempt < 20; ++attempt) {
        HWND window = ReadRuntimeWindow();
        if (window == NULL || !IsWindow(window)) break;
        if (g_windowed) {
            ResizeGameWindow(window);
            InstallCursorHooks();
        }
        ProcessEncounterTitleFeedback();
        Sleep(100);
    }
    while (g_dynamic_encounter_rate) {
        HWND window = ReadRuntimeWindow();
        if (window == NULL || !IsWindow(window)) break;
        ProcessEncounterTitleFeedback();
        Sleep(25);
    }
    return 0;
}

__declspec(dllexport) DWORD WINAPI CastleRuntimeStart(void) {
    g_experience_multiplier = LoadMultiplier(L"ExperienceMultiplier");
    g_money_multiplier = LoadMultiplier(L"MoneyMultiplier");
    g_dynamic_encounter_rate = LoadPatchBool(L"DynamicEncounterRate", false);
    DWORD settlement_result = InstallSettlementHooks();
    if (settlement_result != 1) return settlement_result;
    if (g_dynamic_encounter_rate) {
        DWORD encounter_result = InstallEncounterHooks();
        if (encounter_result != 1) return encounter_result;
    }
    if (!LoadBool(L"Windowed", false) && !g_dynamic_encounter_rate) return 1;
    HANDLE thread = CreateThread(NULL, 0, RuntimeWorker, NULL, 0, NULL);
    if (thread == NULL) return 0;
    CloseHandle(thread);
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
