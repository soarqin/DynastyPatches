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
#define GAME_CLIENT_WIDTH 640
#define GAME_CLIENT_HEIGHT 480

typedef BOOL (WINAPI *GetCursorPosFn)(LPPOINT);
typedef BOOL (WINAPI *SetCursorPosFn)(int, int);

static HINSTANCE g_instance;
static GetCursorPosFn g_original_get_cursor_pos;
static SetCursorPosFn g_original_set_cursor_pos;
static volatile LONG g_cursor_hooks_installed;
static uint32_t g_scale2 = 2;
static uint32_t g_experience_multiplier = 1;
static uint32_t g_money_multiplier = 1;
static bool g_cursor_lock;
static HWND g_window;
static uint8_t *g_hook_block;
static size_t g_hook_block_offset;

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

static HWND ReadGameWindow(void) {
    return *(HWND *)(uintptr_t)GAME_WINDOW_HANDLE_ADDRESS;
}

static BOOL WINAPI RuntimeGetCursorPos(LPPOINT point) {
    if (g_original_get_cursor_pos == NULL || point == NULL || !g_original_get_cursor_pos(point)) return FALSE;
    HWND window = g_window != NULL ? g_window : ReadGameWindow();
    if (window == NULL || !IsWindow(window) || !ScreenToClient(window, point)) return FALSE;
    point->x = (LONG)(((int64_t)point->x * 2) / (int64_t)g_scale2);
    point->y = (LONG)(((int64_t)point->y * 2) / (int64_t)g_scale2);
    return TRUE;
}

static BOOL WINAPI RuntimeSetCursorPos(int x, int y) {
    if (g_original_set_cursor_pos == NULL) return FALSE;
    HWND window = g_window != NULL ? g_window : ReadGameWindow();
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

static DWORD WINAPI RuntimeWorker(void *parameter) {
    (void)parameter;
    g_scale2 = LoadScale();
    g_cursor_lock = LoadBool(L"CursorLock", false);
    for (int attempt = 0; attempt < 400; ++attempt) {
        HWND window = ReadGameWindow();
        if (window != NULL && IsWindow(window)) {
            g_window = window;
            break;
        }
        Sleep(25);
    }
    if (g_window == NULL) return 0;
    for (int attempt = 0; attempt < 20 && IsWindow(g_window); ++attempt) {
        ResizeGameWindow(g_window);
        InstallCursorHooks();
        Sleep(100);
    }
    return 0;
}

__declspec(dllexport) DWORD WINAPI CastleRuntimeStart(void) {
    g_experience_multiplier = LoadMultiplier(L"ExperienceMultiplier");
    g_money_multiplier = LoadMultiplier(L"MoneyMultiplier");
    DWORD settlement_result = InstallSettlementHooks();
    if (settlement_result != 1) return settlement_result;
    if (!LoadBool(L"Windowed", false)) return 1;
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
