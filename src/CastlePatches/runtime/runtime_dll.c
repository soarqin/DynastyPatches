#include <windows.h>
#include <ddraw.h>
#include <dbghelp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define GAME_WINDOW_HANDLE_ADDRESS ((uintptr_t)0x0046F384u)
#define RENDERER_GLOBAL_ADDRESS ((uintptr_t)0x0089F6C0u)
#define RENDERER_WIDTH_HOOK_ADDRESS ((uintptr_t)0x0040157Fu)
#define MAP_RENDER_HOOK_ADDRESS ((uintptr_t)0x0040B050u)
#define MAP_CAMERA_LEFT_ADDRESS ((uintptr_t)0x0044B383u)
#define MAP_CAMERA_RIGHT_ADDRESS ((uintptr_t)0x0044B3B4u)
#define MAP_CAMERA_LEFT_REPOSITION_ADDRESS ((uintptr_t)0x0044B38Fu)
#define MAP_CAMERA_RIGHT_REPOSITION_ADDRESS ((uintptr_t)0x0044B3C0u)
#define DIALOG_TEXT_NAME_CALL_ADDRESS ((uintptr_t)0x004048E6u)
#define DIALOG_TEXT_NAME_RETURN_ADDRESS ((uintptr_t)0x004048EBu)
#define DIALOG_TEXT_BODY_CALL_ADDRESS ((uintptr_t)0x004049FFu)
#define DIALOG_TEXT_BODY_RETURN_ADDRESS ((uintptr_t)0x00404A04u)
#define GLYPH_BLIT_HOOK_ADDRESS ((uintptr_t)0x0044E93Du)
#define GLYPH_BLIT_CONTINUE_ADDRESS ((uintptr_t)0x0044E94Cu)
#define MAP_UI_SPRITE_HOOK_ADDRESS ((uintptr_t)0x00407510u)
#define MAP_UI_SPRITE_RETURN_ADDRESS ((uintptr_t)0x0040751Cu)
#define PRESENT_FUNCTION_ADDRESS ((uintptr_t)0x004064E0u)
#define BINK_FRAME_FUNCTION_ADDRESS ((uintptr_t)0x00401BA0u)
#define EFFECT_STRIDE_1_ADDRESS ((uintptr_t)0x00406E11u)
#define EFFECT_STRIDE_2_ADDRESS ((uintptr_t)0x00406E4Fu)
#define EFFECT_STRIDE_3_ADDRESS ((uintptr_t)0x00406E82u)
#define EFFECT_STRIDE_4_ADDRESS ((uintptr_t)0x00406E9Au)
#define EFFECT_RIGHT_1_ADDRESS ((uintptr_t)0x00406E02u)
#define EFFECT_RIGHT_2_ADDRESS ((uintptr_t)0x00406E40u)
#define EFFECT_RIGHT_3_ADDRESS ((uintptr_t)0x00406E68u)
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
#define WIDESCREEN_CLIENT_WIDTH 864
#define WIDESCREEN_CLIENT_HEIGHT 480

typedef BOOL (WINAPI *GetCursorPosFn)(LPPOINT);
typedef BOOL (WINAPI *SetCursorPosFn)(int, int);
typedef int (__cdecl *EncounterInitFn)(int);
typedef void (__fastcall *PresentFn)(void *renderer);

typedef size_t (*BuildGeneratedHookFn)(uint8_t *buffer, size_t capacity);

static bool EmitEncounterByte(uint8_t *buffer, size_t capacity, size_t *offset, uint8_t value);
static bool EmitEncounterDword(uint8_t *buffer, size_t capacity, size_t *offset, uint32_t value);
static bool EmitEncounterPushImm(uint8_t *buffer, size_t capacity, size_t *offset, uintptr_t value);
static size_t BuildDialogTextHook(uint8_t *buffer,
                                  size_t capacity,
                                  uintptr_t return_address);
static size_t BuildDialogTextNameHook(uint8_t *buffer, size_t capacity);
static size_t BuildDialogTextBodyHook(uint8_t *buffer, size_t capacity);
static size_t BuildGlyphBlitGuardHook(uint8_t *buffer, size_t capacity);
static size_t BuildMapUiSpriteHook(uint8_t *buffer, size_t capacity);
static bool InstallGeneratedHook(uintptr_t address,
                                 const uint8_t *expected,
                                 size_t expected_length,
                                 BuildGeneratedHookFn builder);

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
static uint8_t *g_last_hook_code;
static PresentFn g_original_present;
static bool g_dynamic_encounter_rate;
static bool g_windowed;
static bool g_widescreen;
static bool g_encounter_map_enabled;
static LPTOP_LEVEL_EXCEPTION_FILTER g_previous_exception_filter;
static wchar_t g_original_window_title[256];
static WNDPROC g_original_window_proc;
static volatile LONG g_encounter_rate_percent = 100;
static volatile LONG g_title_restore_due;
static volatile LONG g_window_proc_installed;
static volatile LONG g_map_frame;
static volatile LONG g_map_active;
static volatile LONG g_bink_frame;

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

static LONG WINAPI RuntimeUnhandledExceptionFilter(EXCEPTION_POINTERS *exception) {
    wchar_t path[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, path, _countof(path));
    if (length != 0 && length < _countof(path)) {
        wchar_t *slash = wcsrchr(path, L'\\');
        if (slash != NULL) {
            DWORD pid = GetCurrentProcessId();
            _snwprintf_s(slash + 1,
                         (size_t)(path + _countof(path) - slash - 1),
                         _TRUNCATE,
                         L"CastleRuntime-crash-%lu.dmp",
                         (unsigned long)pid);
            HANDLE dump = CreateFileW(path,
                                       GENERIC_WRITE,
                                       0,
                                       NULL,
                                       CREATE_ALWAYS,
                                       FILE_ATTRIBUTE_NORMAL,
                                       NULL);
            if (dump != INVALID_HANDLE_VALUE) {
                MINIDUMP_EXCEPTION_INFORMATION info = {
                    GetCurrentThreadId(),
                    exception,
                    FALSE,
                };
                MiniDumpWriteDump(GetCurrentProcess(),
                                  pid,
                                  dump,
                                   MiniDumpWithFullMemory |
                                       MiniDumpWithHandleData |
                                       MiniDumpWithUnloadedModules,
                                  exception != NULL ? &info : NULL,
                                  NULL,
                                  NULL);
                CloseHandle(dump);
            }
        }
    }
    if (g_previous_exception_filter != NULL) return g_previous_exception_filter(exception);
    return EXCEPTION_CONTINUE_SEARCH;
}

static bool IsMapActive(void) {
    return InterlockedCompareExchange(&g_map_active, 0, 0) != 0;
}

static size_t BuildDialogTextHook(uint8_t *buffer,
                                  size_t capacity,
                                  uintptr_t return_address) {
    size_t offset = 0;
    if (!EmitEncounterByte(buffer, capacity, &offset, 0x81) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x44) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x24) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x0C) ||
        !EmitEncounterDword(buffer, capacity, &offset,
                            /* The original glyph blit overruns at the exact right edge. */
                            (uint32_t)((WIDESCREEN_CLIENT_WIDTH - GAME_CLIENT_WIDTH) / 2)) ||
        !EmitEncounterPushImm(buffer, capacity, &offset, return_address) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0xB8) ||
        !EmitEncounterDword(buffer, capacity, &offset, (uint32_t)0x00402EE0u) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0xFF) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0xE0)) {
        return 0;
    }
    return offset;
}

static size_t BuildDialogTextNameHook(uint8_t *buffer, size_t capacity) {
    return BuildDialogTextHook(buffer, capacity, DIALOG_TEXT_NAME_RETURN_ADDRESS);
}

static size_t BuildDialogTextBodyHook(uint8_t *buffer, size_t capacity) {
    return BuildDialogTextHook(buffer, capacity, DIALOG_TEXT_BODY_RETURN_ADDRESS);
}

static size_t BuildGlyphBlitGuardHook(uint8_t *buffer, size_t capacity) {
    static const uint8_t original[] = {
        0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x53, 0x56,
        0x57, 0x8B, 0x45, 0x14, 0x2B, 0x45, 0x20,
    };
    size_t offset = 0;
    size_t first_branch;
    size_t second_branch;
    size_t invalid;
    if (capacity - offset < sizeof(original)) {
        return 0;
    }
    memcpy(buffer + offset, original, sizeof(original));
    offset += sizeof(original);
    if (
        !EmitEncounterByte(buffer, capacity, &offset, 0x8B) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x45) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x14) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x3B) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x45) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x20) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x74)) {
        return 0;
    }
    first_branch = offset++;
    if (!EmitEncounterByte(buffer, capacity, &offset, 0x8B) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x45) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x18) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x3B) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x45) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x24) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x74)) {
        return 0;
    }
    second_branch = offset++;
    if (!EmitEncounterByte(buffer, capacity, &offset, 0x8B) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x45) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x14) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x2B) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x45) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x20) ||
        !EmitEncounterPushImm(buffer, capacity, &offset, GLYPH_BLIT_CONTINUE_ADDRESS) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0xC3)) {
        return 0;
    }
    invalid = offset;
    if (!EmitEncounterByte(buffer, capacity, &offset, 0x31) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0xC0) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x5F) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x5E) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x5B) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x8B) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0xE5) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x5D) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0xC3) ||
        invalid <= first_branch + 1u ||
        invalid <= second_branch + 1u ||
        invalid - first_branch - 1u > 0x7Fu ||
        invalid - second_branch - 1u > 0x7Fu) {
        return 0;
    }
    buffer[first_branch] = (uint8_t)(invalid - first_branch - 1u);
    buffer[second_branch] = (uint8_t)(invalid - second_branch - 1u);
    return offset;
}

static void __cdecl OffsetMapUiSpriteX(uintptr_t object, uintptr_t caller) {
    if (!g_widescreen || caller < 0x00404800u || caller >= 0x00404A46u) return;
    *(int32_t *)object += (WIDESCREEN_CLIENT_WIDTH - GAME_CLIENT_WIDTH) / 2;
}

static bool GetFixedViewport(HWND window, RECT *viewport) {
    RECT client;
    if (viewport == NULL || !GetClientRect(window, &client)) return false;
    LONG client_width = client.right - client.left;
    LONG client_height = client.bottom - client.top;
    if (client_width <= 0 || client_height <= 0) return false;

    LONG width;
    LONG height;
    if ((int64_t)client_width * 3 >= (int64_t)client_height * 4) {
        height = client_height;
        width = (LONG)(((int64_t)height * 4) / 3);
    } else {
        width = client_width;
        height = (LONG)(((int64_t)width * 3) / 4);
    }
    viewport->left = (client_width - width) / 2;
    viewport->top = (client_height - height) / 2;
    viewport->right = viewport->left + width;
    viewport->bottom = viewport->top + height;
    return true;
}

static BOOL WINAPI RuntimeGetCursorPos(LPPOINT point) {
    if (g_original_get_cursor_pos == NULL || point == NULL || !g_original_get_cursor_pos(point)) return FALSE;
    HWND window = ReadRuntimeWindow();
    if (window == NULL) window = ReadGameWindow();
    if (window == NULL || !IsWindow(window) || !ScreenToClient(window, point)) return FALSE;

    if (g_widescreen && !IsMapActive()) {
        RECT viewport;
        if (!GetFixedViewport(window, &viewport)) return FALSE;
        if (!PtInRect(&viewport, *point)) {
            point->x = -1;
            point->y = -1;
            return TRUE;
        }
        point->x = (LONG)(((int64_t)(point->x - viewport.left) * GAME_CLIENT_WIDTH) /
                          (viewport.right - viewport.left));
        point->y = (LONG)(((int64_t)(point->y - viewport.top) * GAME_CLIENT_HEIGHT) /
                          (viewport.bottom - viewport.top));
        return TRUE;
    }

    point->x = (LONG)(((int64_t)point->x * 2) / (int64_t)g_scale2);
    point->y = (LONG)(((int64_t)point->y * 2) / (int64_t)g_scale2);
    return TRUE;
}

static BOOL WINAPI RuntimeSetCursorPos(int x, int y) {
    if (g_original_set_cursor_pos == NULL) return FALSE;
    HWND window = ReadRuntimeWindow();
    if (window == NULL) window = ReadGameWindow();
    if (window == NULL || !IsWindow(window)) return FALSE;
    POINT point;
    if (g_widescreen && !IsMapActive()) {
        RECT viewport;
        if (!GetFixedViewport(window, &viewport)) return FALSE;
        point.x = viewport.left + (LONG)(((int64_t)x * (viewport.right - viewport.left)) /
                                         GAME_CLIENT_WIDTH);
        point.y = viewport.top + (LONG)(((int64_t)y * (viewport.bottom - viewport.top)) /
                                        GAME_CLIENT_HEIGHT);
    } else {
        point.x = (LONG)(((int64_t)x * g_scale2) / 2);
        point.y = (LONG)(((int64_t)y * g_scale2) / 2);
    }
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

static bool PatchEffectStride(uintptr_t address) {
    static const uint8_t expected[] = {0x68, 0x00, 0x03, 0x00, 0x00};
    static const uint8_t replacement[] = {0x68, 0xE0, 0x03, 0x00, 0x00};
    if (memcmp((const void *)address, replacement, sizeof(replacement)) == 0) return true;
    if (memcmp((const void *)address, expected, sizeof(expected)) != 0) {
        SetLastError(ERROR_REVISION_MISMATCH);
        return false;
    }
    return WriteCode(address, replacement, sizeof(replacement));
}

static bool PatchEffectRight(uintptr_t address) {
    static const uint8_t expected[] = {0x68, 0xC0, 0x02, 0x00, 0x00};
    static const uint8_t replacement[] = {0x68, 0xA0, 0x03, 0x00, 0x00};
    if (memcmp((const void *)address, replacement, sizeof(replacement)) == 0) return true;
    if (memcmp((const void *)address, expected, sizeof(expected)) != 0) {
        SetLastError(ERROR_REVISION_MISMATCH);
        return false;
    }
    return WriteCode(address, replacement, sizeof(replacement));
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
    bool installed = WriteCode(address, jump, expected_length);
    if (installed) g_last_hook_code = remote;
    return installed;
}

static void ApplyWidescreenRendererWidth(void) {
    uintptr_t renderer = *(volatile uintptr_t *)RENDERER_GLOBAL_ADDRESS;
    if (renderer == 0) return;
    uintptr_t descriptor = *(volatile uintptr_t *)(renderer + 0x28u);
    if (descriptor != 0) *(volatile uint32_t *)(descriptor + 0x04u) = WIDESCREEN_CLIENT_WIDTH;
}

static void __cdecl MarkMapFrame(void) {
    InterlockedExchange(&g_map_frame, 1);
}

static void __cdecl MarkBinkFrame(void) {
    InterlockedExchange(&g_bink_frame, 1);
}

static bool ComposeFixedFrame(uintptr_t renderer) {
    LPDIRECTDRAWSURFACE4 back =
        (LPDIRECTDRAWSURFACE4)*(volatile uintptr_t *)(renderer + 0x08u);
    LPDIRECTDRAWSURFACE4 scratch =
        (LPDIRECTDRAWSURFACE4)*(volatile uintptr_t *)(renderer + 0x14u);
    if (back == NULL || scratch == NULL) return false;

    /* Build the centered 4:3 frame off-screen so primary receives one Blt. */
    RECT left_bar = {0, 0, (WIDESCREEN_CLIENT_WIDTH - GAME_CLIENT_WIDTH) / 2, WIDESCREEN_CLIENT_HEIGHT};
    RECT right_bar = {left_bar.right + GAME_CLIENT_WIDTH, 0, WIDESCREEN_CLIENT_WIDTH, WIDESCREEN_CLIENT_HEIGHT};
    RECT source = {0, 0, GAME_CLIENT_WIDTH, GAME_CLIENT_HEIGHT};
    RECT destination = {left_bar.right, 0, left_bar.right + GAME_CLIENT_WIDTH, GAME_CLIENT_HEIGHT};
    DDBLTFX fill = {0};
    fill.dwSize = sizeof(fill);

    if (FAILED(IDirectDrawSurface4_Blt(scratch,
                                       &source,
                                       back,
                                       &source,
                                       DDBLT_WAIT,
                                       NULL))) {
        return false;
    }
    if (FAILED(IDirectDrawSurface4_Blt(back,
                                       &destination,
                                       scratch,
                                       &source,
                                       DDBLT_WAIT,
                                       NULL))) {
        return false;
    }
    return SUCCEEDED(IDirectDrawSurface4_Blt(back,
                                             &left_bar,
                                             NULL,
                                             NULL,
                                             DDBLT_WAIT | DDBLT_COLORFILL,
                                             &fill)) &&
           SUCCEEDED(IDirectDrawSurface4_Blt(back,
                                             &right_bar,
                                             NULL,
                                             NULL,
                                             DDBLT_WAIT | DDBLT_COLORFILL,
                                             &fill));
}

static bool PresentBinkCentered(uintptr_t renderer) {
    LPDIRECTDRAWSURFACE4 primary =
        (LPDIRECTDRAWSURFACE4)*(volatile uintptr_t *)(renderer + 0x04u);
    LPDIRECTDRAWSURFACE4 back =
        (LPDIRECTDRAWSURFACE4)*(volatile uintptr_t *)(renderer + 0x08u);
    HWND window = ReadRuntimeWindow();
    RECT viewport;
    POINT top_left;
    POINT bottom_right;
    RECT source = {0, 0, GAME_CLIENT_WIDTH, GAME_CLIENT_HEIGHT};
    RECT destination;
    if (primary == NULL || back == NULL || window == NULL ||
        !GetFixedViewport(window, &viewport)) return false;

    top_left.x = viewport.left;
    top_left.y = viewport.top;
    bottom_right.x = viewport.right;
    bottom_right.y = viewport.bottom;
    if (!ClientToScreen(window, &top_left) || !ClientToScreen(window, &bottom_right)) return false;
    destination.left = top_left.x;
    destination.top = top_left.y;
    destination.right = bottom_right.x;
    destination.bottom = bottom_right.y;
    return SUCCEEDED(IDirectDrawSurface4_Blt(primary,
                                              &destination,
                                              back,
                                              &source,
                                              DDBLT_WAIT,
                                              NULL));
}

static void __fastcall RuntimePresent(void *renderer, void *unused_edx) {
    (void)unused_edx;
    bool map_frame = InterlockedExchange(&g_map_frame, 0) != 0;
    bool bink_frame = InterlockedExchange(&g_bink_frame, 0) != 0;
    InterlockedExchange(&g_map_active, map_frame ? 1 : 0);
    if (bink_frame) {
        (void)PresentBinkCentered((uintptr_t)renderer);
    } else {
        if (!map_frame) (void)ComposeFixedFrame((uintptr_t)renderer);
        if (g_original_present != NULL) g_original_present(renderer);
    }
}

static bool InstallWidescreenHooks(void) {
    static const uint8_t renderer_width_expected[] = {0xE8, 0xDC, 0x43, 0x00, 0x00};
    static const uint8_t map_render_expected[] = {
        0x56, 0x8B, 0xF1, 0x8A, 0x86, 0x19, 0x02, 0x00, 0x00, 0x84, 0xC0,
    };
    static const uint8_t present_expected[] = {
        0x83, 0xEC, 0x14, 0x56, 0x8B, 0xF1, 0x8A, 0x46, 0x30,
    };
    static const uint8_t bink_frame_expected[] = {
        0x56, 0x8B, 0xF1, 0x8A, 0x46, 0x08, 0x84, 0xC0,
    };
    static const uint8_t map_ui_sprite_expected[] = {
        0x51, 0x56, 0x8B, 0xF1, 0xC7, 0x44, 0x24, 0x04, 0x00, 0x00, 0x00, 0x00,
    };
    static const uint8_t dialog_text_name_call_expected[] = {0xE8, 0xF5, 0xE5, 0xFF, 0xFF};
    static const uint8_t dialog_text_body_call_expected[] = {0xE8, 0xDC, 0xE4, 0xFF, 0xFF};
    static const uint8_t glyph_blit_expected[] = {
        0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x53, 0x56,
        0x57, 0x8B, 0x45, 0x14, 0x2B, 0x45, 0x20,
    };
    static const uint8_t camera_left_expected[] = {0x00, 0x01, 0x00, 0x00};
    static const uint8_t camera_right_expected[] = {0x80, 0x01, 0x00, 0x00};
    static const uint8_t camera_left_reposition_expected[] = {0x00, 0xFF, 0xFF, 0xFF};
    static const uint8_t camera_right_reposition_expected[] = {0x80, 0xFE, 0xFF, 0xFF};
    static const uint8_t camera_left_replacement[] = {0x00, 0x01, 0x00, 0x00};
    static const uint8_t camera_right_replacement[] = {0x60, 0x02, 0x00, 0x00};
    static const uint8_t camera_left_reposition_replacement[] = {0x00, 0xFF, 0xFF, 0xFF};
    static const uint8_t camera_right_reposition_replacement[] = {0xA0, 0xFD, 0xFF, 0xFF};

    if (memcmp((const void *)RENDERER_WIDTH_HOOK_ADDRESS,
               renderer_width_expected,
               sizeof(renderer_width_expected)) != 0 ||
        memcmp((const void *)MAP_RENDER_HOOK_ADDRESS,
               map_render_expected,
               sizeof(map_render_expected)) != 0 ||
        memcmp((const void *)PRESENT_FUNCTION_ADDRESS,
               present_expected,
               sizeof(present_expected)) != 0 ||
        memcmp((const void *)MAP_UI_SPRITE_HOOK_ADDRESS,
               map_ui_sprite_expected,
               sizeof(map_ui_sprite_expected)) != 0 ||
        memcmp((const void *)DIALOG_TEXT_NAME_CALL_ADDRESS,
               dialog_text_name_call_expected,
               sizeof(dialog_text_name_call_expected)) != 0 ||
        memcmp((const void *)DIALOG_TEXT_BODY_CALL_ADDRESS,
               dialog_text_body_call_expected,
               sizeof(dialog_text_body_call_expected)) != 0 ||
        memcmp((const void *)GLYPH_BLIT_HOOK_ADDRESS,
               glyph_blit_expected,
               sizeof(glyph_blit_expected)) != 0 ||
        memcmp((const void *)MAP_CAMERA_LEFT_ADDRESS,
               camera_left_expected,
               sizeof(camera_left_expected)) != 0 ||
         memcmp((const void *)MAP_CAMERA_RIGHT_ADDRESS,
                 camera_right_expected,
                 sizeof(camera_right_expected)) != 0 ||
         memcmp((const void *)MAP_CAMERA_LEFT_REPOSITION_ADDRESS,
                camera_left_reposition_expected,
                sizeof(camera_left_reposition_expected)) != 0 ||
         memcmp((const void *)MAP_CAMERA_RIGHT_REPOSITION_ADDRESS,
                camera_right_reposition_expected,
                sizeof(camera_right_reposition_expected)) != 0) {
        SetLastError(ERROR_REVISION_MISMATCH);
        return false;
    }

    uint8_t renderer_width_code[] = {
        0x60,
        0xB8, 0, 0, 0, 0, 0xFF, 0xD0,
        0x61,
        0xB8, 0, 0, 0, 0, 0xFF, 0xD0,
        0xE9, 0, 0, 0, 0,
    };
    memcpy(renderer_width_code + 2,
           &(uint32_t){(uint32_t)(uintptr_t)&ApplyWidescreenRendererWidth},
           sizeof(uint32_t));
    memcpy(renderer_width_code + 10,
           &(uint32_t){(uint32_t)0x00405960u},
           sizeof(uint32_t));

    uint8_t map_render_code[] = {
        0x60,                         /* mark the whole map/fade frame */
        0xB8, 0, 0, 0, 0, 0xFF, 0xD0,
        0x61,
        0x56, 0x8B, 0xF1, 0x8A, 0x86, 0x19, 0x02, 0x00, 0x00, 0x84, 0xC0,
        0xE9, 0, 0, 0, 0,
    };
    memcpy(map_render_code + 2,
           &(uint32_t){(uint32_t)(uintptr_t)&MarkMapFrame},
           sizeof(uint32_t));

    uint8_t present_code[] = {
        0x83, 0xEC, 0x14, 0x56, 0x8B, 0xF1, 0x8A, 0x46, 0x30,
        0xE9, 0, 0, 0, 0,
    };

    uint8_t bink_frame_code[] = {
        0x51,
        0xB8, 0, 0, 0, 0, 0xFF, 0xD0,
        0x59,
        0x56, 0x8B, 0xF1, 0x8A, 0x46, 0x08, 0x84, 0xC0,
        0xE9, 0, 0, 0, 0,
    };
    memcpy(bink_frame_code + 2,
           &(uint32_t){(uint32_t)(uintptr_t)&MarkBinkFrame},
           sizeof(uint32_t));

    if (!WriteCode(MAP_CAMERA_LEFT_ADDRESS,
                   camera_left_replacement,
                   sizeof(camera_left_replacement)) ||
        !WriteCode(MAP_CAMERA_RIGHT_ADDRESS,
                   camera_right_replacement,
                   sizeof(camera_right_replacement)) ||
        !WriteCode(MAP_CAMERA_LEFT_REPOSITION_ADDRESS,
                   camera_left_reposition_replacement,
                   sizeof(camera_left_reposition_replacement)) ||
        !WriteCode(MAP_CAMERA_RIGHT_REPOSITION_ADDRESS,
                   camera_right_reposition_replacement,
                   sizeof(camera_right_reposition_replacement)) ||
        !InstallHook(RENDERER_WIDTH_HOOK_ADDRESS,
                     renderer_width_expected,
                     sizeof(renderer_width_expected),
                     renderer_width_code,
                     sizeof(renderer_width_code),
                     17) ||
        !InstallHook(MAP_RENDER_HOOK_ADDRESS,
                      map_render_expected,
                      sizeof(map_render_expected),
                      map_render_code,
                      sizeof(map_render_code),
                      21) ||
        !InstallHook(PRESENT_FUNCTION_ADDRESS,
                     present_expected,
                     sizeof(present_expected),
                     present_code,
                     sizeof(present_code),
                     10) ||
        !InstallGeneratedHook(MAP_UI_SPRITE_HOOK_ADDRESS,
                               map_ui_sprite_expected,
                               sizeof(map_ui_sprite_expected),
                               BuildMapUiSpriteHook) ||
        !InstallGeneratedHook(DIALOG_TEXT_NAME_CALL_ADDRESS,
                              dialog_text_name_call_expected,
                              sizeof(dialog_text_name_call_expected),
                              BuildDialogTextNameHook) ||
        !InstallGeneratedHook(DIALOG_TEXT_BODY_CALL_ADDRESS,
                              dialog_text_body_call_expected,
                              sizeof(dialog_text_body_call_expected),
                              BuildDialogTextBodyHook) ||
        !InstallGeneratedHook(GLYPH_BLIT_HOOK_ADDRESS,
                              glyph_blit_expected,
                              sizeof(glyph_blit_expected),
                              BuildGlyphBlitGuardHook)) {
        return false;
    }
    if (!PatchEffectStride(EFFECT_STRIDE_1_ADDRESS) ||
        !PatchEffectStride(EFFECT_STRIDE_2_ADDRESS) ||
        !PatchEffectStride(EFFECT_STRIDE_3_ADDRESS) ||
        !PatchEffectStride(EFFECT_STRIDE_4_ADDRESS) ||
        !PatchEffectRight(EFFECT_RIGHT_1_ADDRESS) ||
        !PatchEffectRight(EFFECT_RIGHT_2_ADDRESS) ||
        !PatchEffectRight(EFFECT_RIGHT_3_ADDRESS)) {
        return false;
    }
    g_original_present = (PresentFn)g_last_hook_code;

    if (!InstallHook(BINK_FRAME_FUNCTION_ADDRESS,
                     bink_frame_expected,
                     sizeof(bink_frame_expected),
                     bink_frame_code,
                     sizeof(bink_frame_code),
                     18)) {
        return false;
    }

    uint8_t present_jump[16] = {0xE9};
    int64_t present_displacement = (int64_t)(uintptr_t)&RuntimePresent -
                                   (int64_t)(PRESENT_FUNCTION_ADDRESS + 5u);
    if (present_displacement < INT32_MIN || present_displacement > INT32_MAX) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return false;
    }
    memcpy(present_jump + 1, &(int32_t){(int32_t)present_displacement}, sizeof(int32_t));
    memset(present_jump + 5, 0x90, sizeof(present_expected) - 5u);
    if (!WriteCode(PRESENT_FUNCTION_ADDRESS, present_jump, sizeof(present_expected))) return false;
    return true;
}

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
    if (!g_encounter_map_enabled) {
        *threshold = 0;
        *progress = 0;
        return;
    }
    if (IsEncounterStateInvalid(*threshold, *progress)) {
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

static size_t BuildMapUiSpriteHook(uint8_t *buffer, size_t capacity) {
    static const uint8_t original[] = {
        0x51, 0x56, 0x8B, 0xF1, 0xC7, 0x44, 0x24, 0x04, 0x00, 0x00, 0x00, 0x00,
    };
    size_t offset = 0;
    if (!EmitEncounterByte(buffer, capacity, &offset, 0x60) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x8B) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x44) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x24) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x0C) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0xFF) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x30) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0xFF) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x74) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x24) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x1C) ||
        !EmitEncounterAbsoluteCall(buffer, capacity, &offset, (uintptr_t)&OffsetMapUiSpriteX) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x83) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0xC4) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x08) ||
        !EmitEncounterByte(buffer, capacity, &offset, 0x61)) {
        return 0;
    }
    if (capacity - offset < sizeof(original)) return 0;
    memcpy(buffer + offset, original, sizeof(original));
    offset += sizeof(original);
    if (!EmitEncounterPushImm(buffer, capacity, &offset, MAP_UI_SPRITE_RETURN_ADDRESS) ||
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
    uint32_t logical_width = g_widescreen ? WIDESCREEN_CLIENT_WIDTH : GAME_CLIENT_WIDTH;
    uint32_t logical_height = g_widescreen ? WIDESCREEN_CLIENT_HEIGHT : GAME_CLIENT_HEIGHT;
    LONG client_width = (LONG)(((uint64_t)logical_width * g_scale2 + 1u) / 2u);
    LONG client_height = (LONG)(((uint64_t)logical_height * g_scale2 + 1u) / 2u);
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

static void ShowEncounterRateFeedback(uint32_t rate) {
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
        ShowEncounterRateFeedback(rate);
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

    ShowEncounterRateFeedback(rate);
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
    g_widescreen = LoadBool(L"Widescreen", false);
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
    g_previous_exception_filter = SetUnhandledExceptionFilter(RuntimeUnhandledExceptionFilter);
    g_experience_multiplier = LoadMultiplier(L"ExperienceMultiplier");
    g_money_multiplier = LoadMultiplier(L"MoneyMultiplier");
    g_dynamic_encounter_rate = LoadPatchBool(L"DynamicEncounterRate", false);
    g_windowed = LoadBool(L"Windowed", false);
    g_widescreen = LoadBool(L"Widescreen", false);
    DWORD settlement_result = InstallSettlementHooks();
    if (settlement_result != 1) return settlement_result;
    if (g_dynamic_encounter_rate) {
        DWORD encounter_result = InstallEncounterHooks();
        if (encounter_result != 1) return encounter_result;
    }
    if (g_widescreen) {
        if (!g_windowed || !InstallWidescreenHooks()) return 241;
    }
    if (!g_windowed && !g_dynamic_encounter_rate && !g_widescreen) return 1;
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
