#include <windows.h>
#include <intrin.h>
#include <ddraw.h>
#include <dbghelp.h>
#include <MinHook.h>

#include "../castle_addresses.h"
#include "widescreen_backdrop.h"
#include "widescreen_transition.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define ENCOUNTER_FEEDBACK_MESSAGE (WM_APP + 0x3A1u)
#define ENCOUNTER_FEEDBACK_TIMER_ID 0x3A1u
#define SHUTDOWN_WATCHDOG_TIMEOUT_MS 10000u
#define HOOK_ADDRESS(function) ((LPVOID)(uintptr_t)(function))

typedef BOOL (WINAPI *GetCursorPosFn)(LPPOINT);
typedef BOOL (WINAPI *SetCursorPosFn)(int, int);
typedef HWND (WINAPI *CreateWindowExAFn)(DWORD, LPCSTR, LPCSTR, DWORD, int, int,
                                         int, int, HWND, HMENU, HINSTANCE, LPVOID);
typedef int (__cdecl *EncounterInitFn)(int);
typedef void (__fastcall *PresentFn)(void *renderer);
typedef void (__fastcall *TimerSetupFn)(void *timer, void *unused_edx);
typedef void (__cdecl *GameLogicTickFn)(void);
typedef void (__fastcall *UiCursorDrawFn)(void *input_manager);
typedef void (__fastcall *VideoCloseFn)(void *video, void *unused_edx);
typedef void (__fastcall *MapSceneReleaseFn)(void *scene);
typedef uint16_t *(__cdecl *FrameCopyFn)(int32_t, int32_t, const uint16_t *,
                                       int32_t, int32_t, int32_t, int32_t, uint16_t *,
                                       int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);

void SurfaceFormatHook(void);
void RendererWidthHook(void);
void MapRenderHook(void);
void MapBackdropCaptureHook(void);
void EncounterBattleCaptureHook(void);
void ScriptBattleCaptureHook(void);
void MapUiSpriteHook(void);
void DialogTextNameHook(void);
void DialogTextBodyHook(void);
void GlyphBlitHook(void);
void BinkFrameHook(void);
void BinkPresentCallHook(void);
void FullscreenVblankHook(void);
void ExperienceGainHook(void);
void ExperienceDisplayHook(void);
void MoneyGainHook(void);
void MoneyDisplayHook(void);
void EncounterInitialHook(void);
void EncounterRegenerationHook(void);
void FramePacingTimerSetupHook(void);
void GameLogicTickHook(void);

static uint32_t GetEncounterRate(void);

static HINSTANCE g_instance;
static GetCursorPosFn g_original_get_cursor_pos;
static SetCursorPosFn g_original_set_cursor_pos;
static CreateWindowExAFn g_original_create_window_ex_a;
static volatile LONG g_cursor_hooks_installed;
static uint32_t g_scale2 = 2;
uint32_t g_experience_multiplier = 1;
uint32_t g_money_multiplier = 1;
static bool g_cursor_lock;
static HWND volatile g_window;
void *g_original_surface_format;
void *g_original_renderer_width;
void *g_original_map_render;
void *g_original_map_backdrop_capture;
void *g_original_encounter_battle_capture;
void *g_original_script_battle_capture;
void *g_original_map_ui_sprite;
void *g_original_dialog_name;
void *g_original_dialog_body;
void *g_original_glyph_blit;
void *g_original_bink_frame;
void *g_original_fullscreen_vblank;
void *g_original_encounter_initial;
PresentFn g_original_present;
TimerSetupFn g_original_timer_setup;
GameLogicTickFn g_original_game_logic_tick;
static UiCursorDrawFn g_original_ui_cursor_draw;
static MapSceneReleaseFn g_original_map_scene_release;
static FrameCopyFn g_original_frame_copy;
typedef int (__cdecl *MapEffectFn)(void);
typedef uint8_t (__cdecl *FadeFn)(int16_t, int16_t);
typedef uintptr_t (__cdecl *CommandFactoryFn)(const void *);
typedef int (__fastcall *MapLoadFn)(void *, void *, const char *);
typedef int (__fastcall *MapResumeFn)(void *);
typedef void (__fastcall *MenuReleaseFn)(void *);
typedef uint8_t (__fastcall *UiObjectDrawFn)(void *);
typedef int (__fastcall *UiSpriteBlitFn)(void *, void *, uintptr_t, int32_t, int32_t,
                                       int32_t, int32_t, int32_t, int32_t);
typedef uint8_t (__fastcall *SaveLoadFn)(void *, void *, const char *);
static MapEffectFn g_original_map_effect;
static FadeFn g_original_fade_out;
static FadeFn g_original_fade_in;
static CommandFactoryFn g_original_command_factory;
static MapLoadFn g_original_map_load;
static MapResumeFn g_original_map_suspend;
static MapResumeFn g_original_map_resume;
static MenuReleaseFn g_original_menu_release;
static MenuReleaseFn g_original_system_menu_release;
static UiObjectDrawFn g_original_ui_object_draw;
static UiSpriteBlitFn g_original_ui_sprite_blit;
static uintptr_t g_suppressed_ui_sprite;
static SaveLoadFn g_original_save_load;
static CastleTransition g_transition;
static uint32_t g_factory_opcode = UINT32_MAX;
static CastleFrameLayout g_frame_layout;
static bool g_dynamic_encounter_rate;
static bool g_windowed;
static bool g_widescreen;
static bool g_encounter_map_enabled;
static bool g_frame_pacing;
static uint32_t g_game_logic_tick;
static int64_t g_native_timer_ticks;
static int64_t g_render_timer_ticks;
static int64_t g_next_logic_tick;
static bool g_logic_updated;
static LPTOP_LEVEL_EXCEPTION_FILTER g_previous_exception_filter;
static wchar_t g_original_window_title[256];
static WNDPROC g_original_window_proc;
static volatile LONG g_encounter_rate_percent = 100;
static volatile LONG g_title_restore_due;
static volatile LONG g_window_proc_installed;
static volatile LONG g_map_frame;
static volatile LONG g_map_frame_suppressed;
static volatile LONG g_frame_upload_prepared;
static volatile LONG g_map_active;
static volatile LONG g_cursor_map_active;
static volatile LONG g_bink_frame;
volatile LONG g_runtime_shutting_down;
static volatile LONG g_shutdown_watchdog_armed;

static void ResizeGameWindow(HWND window);
static void UpdateCursorLock(HWND window, bool active);
static bool InstallCursorHooks(void);
static bool InstallRuntimeWindowProcedure(void);

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

static bool IsRuntimeShuttingDown(void) {
    return InterlockedCompareExchange(&g_runtime_shutting_down, 0, 0) != 0;
}

static void CloseGameVideo(void) {
    void *video = *(void **)(uintptr_t)VIDEO_STATE_ADDRESS;
    if (video != NULL) ((VideoCloseFn)VIDEO_CLOSE_FUNCTION_ADDRESS)(video, NULL);
}

static DWORD WINAPI ShutdownWatchdog(void *parameter) {
    (void)parameter;
    Sleep(SHUTDOWN_WATCHDOG_TIMEOUT_MS);
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}

static void ArmShutdownWatchdog(void) {
    if (InterlockedCompareExchange(&g_shutdown_watchdog_armed, 1, 0) != 0) return;
    HANDLE thread = CreateThread(NULL, 0, ShutdownWatchdog, NULL, 0, NULL);
    if (thread == NULL) {
        InterlockedExchange(&g_shutdown_watchdog_armed, 0);
        return;
    }
    CloseHandle(thread);
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

static bool IsCursorMapActive(void) {
    return InterlockedCompareExchange(&g_cursor_map_active, 0, 0) != 0;
}

void __cdecl ApplySurfaceFormat(void *descriptor) {
    if (descriptor == NULL || (*(const uint32_t *)((const uint8_t *)descriptor + 0x68u) & 0x40u) == 0) {
        return;
    }
    uint8_t *surface = (uint8_t *)descriptor;
    *(uint32_t *)(surface + 0x04u) |= 0x1000u;
    *(uint32_t *)(surface + 0x48u) = 0x20u;
    *(uint32_t *)(surface + 0x4Cu) = 0x40u;
    *(uint32_t *)(surface + 0x54u) = 16u;
    *(uint32_t *)(surface + 0x58u) = 0xF800u;
    *(uint32_t *)(surface + 0x5Cu) = 0x07E0u;
    *(uint32_t *)(surface + 0x60u) = 0x001Fu;
}

void __cdecl OffsetMapUiSpriteX(uintptr_t object, uintptr_t caller) {
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
    if (IsRuntimeShuttingDown()) return TRUE;
    HWND window = ReadRuntimeWindow();
    if (window == NULL) window = ReadGameWindow();
    if (window == NULL || !IsWindow(window) || !ScreenToClient(window, point)) return FALSE;

    bool game_cursor = (uintptr_t)_ReturnAddress() == CURSOR_UPDATE_GET_CURSOR_RETURN_ADDRESS;
    if (g_widescreen && !game_cursor && !IsCursorMapActive()) {
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
    if (IsRuntimeShuttingDown()) return g_original_set_cursor_pos(x, y);
    HWND window = ReadRuntimeWindow();
    if (window == NULL) window = ReadGameWindow();
    if (window == NULL || !IsWindow(window)) return FALSE;
    POINT point;
    if (g_widescreen) {
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
    return MH_CreateHookApi(L"user32.dll", "SetCursorPos", HOOK_ADDRESS(RuntimeSetCursorPos),
                            (LPVOID *)&g_original_set_cursor_pos) == MH_OK &&
           MH_CreateHookApi(L"user32.dll", "GetCursorPos", HOOK_ADDRESS(RuntimeGetCursorPos),
                            (LPVOID *)&g_original_get_cursor_pos) == MH_OK &&
           MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
}

static HWND WINAPI RuntimeCreateWindowExA(DWORD ex_style, LPCSTR class_name, LPCSTR title,
                                          DWORD style, int x, int y, int width, int height,
                                          HWND parent, HMENU menu, HINSTANCE instance,
                                          LPVOID parameter) {
    HWND window = g_original_create_window_ex_a(
        ex_style, class_name, title, style, x, y, width, height,
        parent, menu, instance, parameter);
        if (window != NULL && class_name != NULL && strcmp(class_name, "MainWnd") == 0) {
        InterlockedExchange(&g_runtime_shutting_down, 0);
        WriteRuntimeWindow(window);
        InstallRuntimeWindowProcedure();
        if (g_windowed) ResizeGameWindow(window);
            UpdateCursorLock(window, true);
            if (g_windowed) InstallCursorHooks();
    }
    return window;
}

static bool InstallCreateWindowHook(void) {
    static const uint8_t video_close_expected[] = {0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x04, 0x85, 0xC0};
    if (memcmp((const void *)VIDEO_CLOSE_FUNCTION_ADDRESS,
               video_close_expected,
               sizeof(video_close_expected)) != 0) {
        SetLastError(ERROR_REVISION_MISMATCH);
        return false;
    }
    return MH_CreateHookApi(L"user32.dll", "CreateWindowExA", HOOK_ADDRESS(RuntimeCreateWindowExA),
                            (LPVOID *)&g_original_create_window_ex_a) == MH_OK &&
           MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
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

static bool ApplyCodePatch(uintptr_t address,
                           const uint8_t *expected,
                           const uint8_t *replacement,
                           size_t length) {
    if (memcmp((const void *)address, expected, length) != 0) {
        SetLastError(ERROR_REVISION_MISMATCH);
        return false;
    }
    return WriteCode(address, replacement, length);
}

static bool InstallGameHook(uintptr_t address,
                            const uint8_t *expected,
                            size_t expected_length,
                            LPVOID detour,
                            LPVOID *original) {
    if (memcmp((const void *)address, expected, expected_length) != 0) {
        SetLastError(ERROR_REVISION_MISMATCH);
        return false;
    }
    if (MH_CreateHook((LPVOID)address, detour, original) != MH_OK) {
        SetLastError(ERROR_FUNCTION_FAILED);
        return false;
    }
    return MH_EnableHook((LPVOID)address) == MH_OK;
}

static bool InstallFramePacingHooks(void) {
    static const uint8_t timer_expected[] = {
        0x83, 0xEC, 0x08, 0x53, 0x56, 0x8B, 0xF1, 0x33,
    };
    static const uint8_t logic_expected[] = {
        0x8B, 0x15, 0xD4, 0x40, 0x8C, 0x00, 0x56, 0x33,
    };
    if (memcmp((const void *)TIMER_SETUP_HOOK_ADDRESS, timer_expected, sizeof(timer_expected)) != 0 ||
        memcmp((const void *)LOGIC_OBJECT_RESET_HOOK_ADDRESS, logic_expected, sizeof(logic_expected)) != 0) {
        SetLastError(ERROR_REVISION_MISMATCH);
        return false;
    }
    if (MH_CreateHook((LPVOID)TIMER_SETUP_HOOK_ADDRESS, HOOK_ADDRESS(FramePacingTimerSetupHook),
                      (LPVOID *)&g_original_timer_setup) != MH_OK) {
        SetLastError(ERROR_FUNCTION_FAILED);
        return false;
    }
    if (MH_EnableHook((LPVOID)TIMER_SETUP_HOOK_ADDRESS) != MH_OK) {
        SetLastError(ERROR_FUNCTION_FAILED);
        return false;
    }
    if (MH_CreateHook((LPVOID)LOGIC_OBJECT_RESET_HOOK_ADDRESS, HOOK_ADDRESS(GameLogicTickHook),
                      (LPVOID *)&g_original_game_logic_tick) != MH_OK ||
        MH_EnableHook((LPVOID)LOGIC_OBJECT_RESET_HOOK_ADDRESS) != MH_OK) {
        SetLastError(ERROR_FUNCTION_FAILED);
        return false;
    }
    return true;
}

void PrepareFrameTimer(const uint32_t *timer) {
    if (*timer != 0) return;

    uint32_t *interval = (uint32_t *)TIMER_INTERVAL_ADDRESS;
    uint32_t native_ms = *interval;
    uint32_t render_ms = native_ms > 3u ? (native_ms + 2u) / 3u : 1u;
    *interval = render_ms;

    LARGE_INTEGER frequency, now;
    g_game_logic_tick = 0;
    g_logic_updated = false;
    g_native_timer_ticks = 0;
    if (QueryPerformanceFrequency(&frequency) && QueryPerformanceCounter(&now)) {
        /* The game posts one frame per divisor multimedia timer callbacks. */
        g_native_timer_ticks = frequency.QuadPart * native_ms / 1000;
        g_render_timer_ticks = frequency.QuadPart * render_ms / 1000;
        g_next_logic_tick = now.QuadPart +
            g_native_timer_ticks * *(const uint32_t *)TIMER_PRESENT_DIVISOR_ADDRESS;
    }
}

int ShouldRunGameLogic(void) {
    LARGE_INTEGER now;
    uint32_t divisor = *(const uint32_t *)TIMER_PRESENT_DIVISOR_ADDRESS;
    int64_t period = g_native_timer_ticks * divisor;
    if (period > 0 && QueryPerformanceCounter(&now)) {
        /* Allow a little timer jitter without waiting another whole render frame. */
        int64_t slack = g_render_timer_ticks * divisor / 4;
        g_logic_updated = now.QuadPart + slack >= g_next_logic_tick;
        if (g_logic_updated) {
            g_next_logic_tick += period;
            /* A stall must not enqueue a burst of object updates on resume. */
            if (g_next_logic_tick <= now.QuadPart) g_next_logic_tick = now.QuadPart + period;
        }
    } else {
        g_game_logic_tick = (g_game_logic_tick + 1u) % 3u;
        g_logic_updated = g_game_logic_tick == 0u;
    }
    return g_logic_updated;
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

void __cdecl ApplyWidescreenRendererWidth(void) {
    uintptr_t renderer = *(volatile uintptr_t *)RENDERER_GLOBAL_ADDRESS;
    if (renderer == 0) return;
    uintptr_t descriptor = *(volatile uintptr_t *)(renderer + 0x28u);
    if (descriptor != 0) *(volatile uint32_t *)(descriptor + 0x04u) = WIDESCREEN_CLIENT_WIDTH;
}

void __cdecl MarkMapFrame(uintptr_t scene) {
    if (*(const uint8_t *)(scene + MAP_SCENE_RENDER_SUPPRESSED_OFFSET) == 0) {
        InterlockedExchange(&g_map_frame, 1);
    } else {
        InterlockedExchange(&g_map_frame_suppressed, 1);
    }
}

void __cdecl CaptureCurrentMapBackdrop(uintptr_t scene) {
    uintptr_t renderer = *(const uintptr_t *)RENDERER_GLOBAL_ADDRESS;
    CaptureMapBackdrop(renderer, scene);
}

void __cdecl AdjustBattleCapture(int32_t *source_stride, int32_t *source_x) {
    uintptr_t renderer = *(const uintptr_t *)RENDERER_GLOBAL_ADDRESS;
    if (renderer == 0) return;

    int32_t width = *(const int32_t *)(renderer + RENDERER_BUFFER_WIDTH_OFFSET);
    int32_t x = *source_x;
    /* The map is full-width. Fixed scenes already draw their center at source x. */
    if (IsMapActive()) x += (WIDESCREEN_CLIENT_WIDTH - GAME_CLIENT_WIDTH) / 2;
    if (x < 0 || width < GAME_CLIENT_WIDTH || x > width - GAME_CLIENT_WIDTH) return;
    *source_stride = width;
    *source_x = x;
}

static void __fastcall RuntimeMapSceneRelease(void *scene, void *unused_edx) {
    (void)unused_edx;
    InvalidateMapBackdrop((uintptr_t)scene);
    if (g_transition.scene == (uintptr_t)scene) {
        CancelFixedTransition(&g_transition);
        SetBackdropTransitionColor(0);
    }
    if (g_original_map_scene_release != NULL) g_original_map_scene_release(scene);
}

static void CancelMapTransition(void) {
    CancelFixedTransition(&g_transition);
    ResetMapBackdrop();
}

static uint32_t CurrentScriptOpcode(void) {
    if (g_factory_opcode != UINT32_MAX) return g_factory_opcode;
    uintptr_t command = *(const uintptr_t *)CURRENT_SCRIPT_COMMAND_ADDRESS;
    if (command == 0) return UINT32_MAX;
    uintptr_t record = *(const uintptr_t *)(command + SCRIPT_COMMAND_RECORD_OFFSET);
    return record != 0 ? *(const uint32_t *)record : UINT32_MAX;
}

static uint32_t FixedUiAfterCurrentFade(void) {
    uintptr_t script = *(const uintptr_t *)SCRIPT_BUFFER_ADDRESS;
    MEMORY_BASIC_INFORMATION region;
    if (script == 0 || VirtualQuery((const void *)script, &region, sizeof(region)) == 0 ||
        region.State != MEM_COMMIT || (region.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return 0;
    uintptr_t region_start = (uintptr_t)region.BaseAddress;
    if (region.RegionSize > UINTPTR_MAX - region_start || script < region_start) return 0;
    size_t size = region.RegionSize - (size_t)(script - region_start);
    return FindFixedUiAfterFade((const uint8_t *)script, size,
                               *(const uint32_t *)SCRIPT_COMMAND_TABLE_ADDRESS,
                               *(const uint32_t *)SCRIPT_COMMAND_COUNT_ADDRESS,
                               *(const uint32_t *)SCRIPT_COMMAND_INDEX_ADDRESS);
}

static uintptr_t __cdecl RuntimeCommandFactory(const void *record) {
    uint32_t opcode = *(const uint32_t *)record;
    uintptr_t scene = *(const uintptr_t *)CURRENT_MAP_SCENE_ADDRESS;
    if (IsFixedUiCommand(opcode)) {
        BeginFixedTransition(&g_transition, scene, opcode);
        SetBackdropTransitionColor(0);
    }
    if (opcode == 0x37u) {
        CancelFixedTransition(&g_transition);
        SetBackdropTransitionColor(0);
    }
    /* A map load/new game or video is a different destination even if an earlier
       UI return is still fading. Cancel before its first draw or script wait. */
    if (opcode == 0x3Au || opcode == 0x3Bu || opcode == 0x3Cu ||
        opcode == 0x8Du || opcode == 0x98u) {
        CancelMapTransition();
    }
    uint32_t previous = g_factory_opcode;
    g_factory_opcode = opcode;
    uintptr_t command = g_original_command_factory(record);
    g_factory_opcode = previous;
    return command;
}

static uint8_t __cdecl RuntimeFadeOut(int16_t step, int16_t delay) {
    uint32_t opcode = CurrentScriptOpcode();
    uintptr_t scene = *(const uintptr_t *)CURRENT_MAP_SCENE_ADDRESS;
    uint32_t target = IsFixedUiCommand(opcode) ? opcode :
        (opcode == 0x53u || opcode == 0x5Au) ? FixedUiAfterCurrentFade() : 0;
    if (target != 0) {
        BeginFixedTransition(&g_transition, scene, target);
    } else {
        CancelFixedTransition(&g_transition);
        SetBackdropTransitionColor(0);
    }
    return g_original_fade_out(step, delay);
}

static uint8_t __cdecl RuntimeFadeIn(int16_t step, int16_t delay) {
    uintptr_t scene = *(const uintptr_t *)CURRENT_MAP_SCENE_ADDRESS;
    if (scene != 0 && g_transition.scene == scene &&
        HasTransitionBackdrop(*(const uintptr_t *)RENDERER_GLOBAL_ADDRESS, scene) &&
        *(const uint8_t *)(scene + MAP_SCENE_RENDER_SUPPRESSED_OFFSET) == 0) {
        ReturnFromFixedTransition(&g_transition, scene);
    }
    return g_original_fade_in(step, delay);
}

static int __cdecl RuntimeMapEffect(void) {
    uintptr_t scene = *(const uintptr_t *)CURRENT_MAP_SCENE_ADDRESS;
    if (g_transition.phase == CASTLE_TRANSITION_NONE || g_transition.scene != scene) {
        return g_original_map_effect();
    }
    g_transition.awaiting_map_frame = false;
    int state = *(const int32_t *)MAP_EFFECT_STATE_ADDRESS;
    if (state != 1 && (*(const uint16_t *)MAP_EFFECT_COLOR_ADDRESS != 1u ||
        *(const int16_t *)MAP_EFFECT_FADE_OUT_TARGET_ADDRESS != 0 ||
        *(const int16_t *)MAP_EFFECT_FADE_IN_TARGET_ADDRESS != CASTLE_TRANSITION_COLOR)) {
        return g_original_map_effect();
    }
    int color = MapTransitionColor(&g_transition, scene, state,
                                   *(const int16_t *)MAP_EFFECT_BRIGHTNESS_ADDRESS, GetTickCount());
    uintptr_t renderer = *(const uintptr_t *)RENDERER_GLOBAL_ADDRESS;
    if (color >= 0) {
        /* This menu leaves the map in the draw queue. Its transparent center
           must use the same crop that meets the two unshifted side strips. */
        if (*(const uintptr_t *)ORDINARY_MENU_OBJECT_ADDRESS != 0 &&
            DrawTransitionBackdrop(renderer, color)) return 0;
        if (ApplyMapTransition(renderer, color)) return 0;
    }
    return g_original_map_effect();
}

static int __fastcall RuntimeMapLoad(void *scene, void *unused_edx, const char *path) {
    (void)unused_edx;
    CancelMapTransition();
    return g_original_map_load(scene, NULL, path);
}

static int __fastcall RuntimeMapResume(void *scene, void *unused_edx) {
    (void)unused_edx;
    int result = g_original_map_resume(scene);
    ReturnFromFixedTransition(&g_transition, (uintptr_t)scene);
    return result;
}

static int __fastcall RuntimeMapSuspend(void *scene, void *unused_edx) {
    (void)unused_edx;
    int result = g_original_map_suspend(scene);
    /* Native menu/shop fades finish on a wide map. Crop the held gray frame
       before the fixed UI first draws, so its center meets the cached sides. */
    if (!IsFixedUiCommand(CurrentScriptOpcode())) {
        /* Random encounters suspend the map without creating a script command. */
        CancelFixedTransition(&g_transition);
        SetBackdropTransitionColor(0);
    } else if (g_transition.scene == (uintptr_t)scene && g_transition.phase == CASTLE_TRANSITION_FIXED) {
        (void)DrawTransitionBackdrop(*(const uintptr_t *)RENDERER_GLOBAL_ADDRESS, 0);
    } else {
        if (g_transition.phase == CASTLE_TRANSITION_RETURNING) {
            /* Random encounters can suspend a map midway through a menu return. */
            CancelFixedTransition(&g_transition);
        }
        SetBackdropTransitionColor(0);
    }
    return result;
}

static void __fastcall RuntimeMenuRelease(void *menu, void *unused_edx) {
    (void)unused_edx;
    g_original_menu_release(menu);
    if (g_transition.target == 0x36u) {
        ReturnFromFixedTransition(&g_transition, *(const uintptr_t *)CURRENT_MAP_SCENE_ADDRESS);
        if (g_transition.awaiting_map_frame) {
            (void)DrawTransitionBackdrop(*(const uintptr_t *)RENDERER_GLOBAL_ADDRESS, g_transition.color);
        }
    }
}

static bool IsGraySystemMenuReturn(uintptr_t menu, uintptr_t renderer, uintptr_t scene) {
    if (menu == 0 || scene == 0 || g_transition.scene != scene || g_transition.target != 0x35u ||
        g_transition.phase == CASTLE_TRANSITION_NONE || !HasTransitionBackdrop(renderer, scene)) return false;
    int32_t phase = *(const int32_t *)(menu + SYSTEM_MENU_EXIT_PHASE_OFFSET);
    uintptr_t state = *(const uintptr_t *)GAME_STATE_OBJECT_ADDRESS;
    return phase >= 1 && phase <= 3 && state != 0 &&
        *(const int32_t *)(state + GAME_STATE_MENU_EVENT_OFFSET) == 0 &&
        *(const int32_t *)(state + GAME_STATE_MENU_LOAD_OFFSET) == 0 &&
        *(const int32_t *)(state + GAME_STATE_MENU_TITLE_OFFSET) == 0;
}

static void __fastcall RuntimeSystemMenuRelease(void *menu, void *unused_edx) {
    (void)unused_edx;
    uintptr_t scene = *(const uintptr_t *)CURRENT_MAP_SCENE_ADDRESS;
    uintptr_t renderer = *(const uintptr_t *)RENDERER_GLOBAL_ADDRESS;
    bool gray_return = IsGraySystemMenuReturn((uintptr_t)menu, renderer, scene);
    g_original_system_menu_release(menu);
    if (gray_return && g_transition.scene == scene && g_transition.phase != CASTLE_TRANSITION_NONE) {
        /* Queue resume runs on the next tick. Keep the final gray background
           while the native map is still suspended, then honor its fade-in. */
        g_transition.awaiting_map_frame = true;
        (void)DrawTransitionBackdrop(renderer, g_transition.color);
    }
}

static uint8_t __fastcall RuntimeUiObjectDraw(void *object, void *unused_edx) {
    (void)unused_edx;
    uintptr_t ui = (uintptr_t)object;
    uintptr_t menu = *(const uintptr_t *)SYSTEM_MENU_OBJECT_ADDRESS;
    uintptr_t renderer = *(const uintptr_t *)RENDERER_GLOBAL_ADDRESS;
    uintptr_t scene = *(const uintptr_t *)CURRENT_MAP_SCENE_ADDRESS;
    uintptr_t previous = g_suppressed_ui_sprite;
    g_suppressed_ui_sprite = 0;
    if (menu != 0 && ui == *(const uintptr_t *)(menu + SYSTEM_MENU_RETURN_MASK_OFFSET) &&
        IsGraySystemMenuReturn(menu, renderer, scene) &&
        *(const uint8_t *)(ui + UI_OBJECT_ENABLED_OFFSET) != 0 &&
        *(const uint8_t *)(ui + UI_OBJECT_VISIBLE_OFFSET) != 0) {
        /* Keep the menu background while its elements slide outward. Suppress
           only FANIN pixels; switch to the gray map after menu destruction. */
        g_suppressed_ui_sprite = *(const uintptr_t *)(ui + UI_OBJECT_SPRITE_OFFSET);
    }
    uint8_t result = g_original_ui_object_draw(object);
    g_suppressed_ui_sprite = previous;
    return result;
}

static int __fastcall RuntimeUiSpriteBlit(void *resource, void *unused_edx, uintptr_t destination,
                                         int32_t x, int32_t y, int32_t mode,
                                         int32_t a6, int32_t a7, int32_t a8) {
    (void)unused_edx;
    if (g_suppressed_ui_sprite != 0) return 0;
    return g_original_ui_sprite_blit(resource, NULL, destination, x, y, mode, a6, a7, a8);
}

static uint8_t __fastcall RuntimeSaveLoad(void *state, void *unused_edx, const char *path) {
    (void)unused_edx;
    uint8_t result = g_original_save_load(state, NULL, path);
    if (result != 0) CancelMapTransition();
    return result;
}

void __cdecl MarkBinkFrame(void) {
    InterlockedExchange(&g_bink_frame, 1);
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

static void PrepareWidescreenFrame(void) {
    bool map_frame = InterlockedExchange(&g_map_frame, 0) != 0;
    bool map_suppressed = InterlockedExchange(&g_map_frame_suppressed, 0) != 0;
    uintptr_t scene = *(const uintptr_t *)CURRENT_MAP_SCENE_ADDRESS;
    /* A suspended map can leave the draw queue entirely, so there may be no
       MapRenderHook call to mark the first battle/menu frame as suppressed. */
    if (!map_frame && !map_suppressed && scene != 0 &&
        HasTransitionBackdrop(*(const uintptr_t *)RENDERER_GLOBAL_ADDRESS, scene) &&
        *(const uint8_t *)(scene + MAP_SCENE_RENDER_SUPPRESSED_OFFSET) != 0) {
        map_suppressed = true;
    }
    bool ordinary_menu = *(const uintptr_t *)ORDINARY_MENU_OBJECT_ADDRESS != 0;
    if (!map_frame && g_transition.awaiting_map_frame && g_transition.scene == scene &&
        DrawTransitionBackdrop(*(const uintptr_t *)RENDERER_GLOBAL_ADDRESS, g_transition.color)) {
        /* A menu can die after drawing has been scheduled. Hold its gray map
           through that upload; reverse the fade when a fresh map actually draws. */
        g_frame_layout.ordinary_menu_frame = true;
    }
    UpdateCastleFrameLayout(&g_frame_layout, map_frame, map_suppressed, ordinary_menu,
                            !g_frame_pacing || g_logic_updated);
    g_logic_updated = false;
    if (!g_frame_layout.map_active && g_transition.phase == CASTLE_TRANSITION_NONE) {
        SetBackdropTransitionColor(0);
    }
    InterlockedExchange(&g_map_active, g_frame_layout.map_active ? 1 : 0);
    InterlockedExchange(&g_cursor_map_active, g_frame_layout.cursor_map_active ? 1 : 0);
}

static uint16_t *__cdecl RuntimeFrameCopy(int32_t source_width, int32_t source_height,
                                        const uint16_t *source, int32_t source_x, int32_t source_y,
                                        int32_t destination_width, int32_t destination_height,
                                        uint16_t *destination, int32_t destination_x, int32_t destination_y,
                                        int32_t right, int32_t bottom, int32_t pitch, int32_t conversion) {
    /* sub_406330 calls this once with its back surface already locked. Prepare
       the scene state here; Present must not consume the same frame twice. */
    if (InterlockedCompareExchange(&g_frame_upload_prepared, 1, 0) == 0) {
        PrepareWidescreenFrame();
    }
    uintptr_t renderer = *(const uintptr_t *)RENDERER_GLOBAL_ADDRESS;
    if (!IsMapActive() && InterlockedCompareExchange(&g_bink_frame, 0, 0) == 0 &&
        renderer != 0 && conversion == 15 && destination_x == 0 && destination_y == 0 &&
        right == WIDESCREEN_CLIENT_WIDTH - 1 && bottom == GAME_CLIENT_HEIGHT - 1 &&
        (uintptr_t)source == *(const uintptr_t *)(renderer + RENDERER_PIXELS_OFFSET) &&
        source_width == *(const int32_t *)(renderer + RENDERER_BUFFER_WIDTH_OFFSET) &&
        source_height == *(const int32_t *)(renderer + RENDERER_BUFFER_HEIGHT_OFFSET) &&
        source_x == *(const int32_t *)(renderer + RENDERER_BORDER_X_OFFSET) &&
        source_y == *(const int32_t *)(renderer + RENDERER_BORDER_Y_OFFSET) &&
        ComposeFixedFrame(renderer, destination, pitch)) {
        return destination;
    }
    return g_original_frame_copy(source_width, source_height, source, source_x, source_y,
                                 destination_width, destination_height, destination,
                                 destination_x, destination_y, right, bottom, pitch, conversion);
}

static void __fastcall RuntimePresent(void *renderer, void *unused_edx) {
    (void)unused_edx;
    if (IsRuntimeShuttingDown()) return;
    /* Bink can present without sub_406330; retain state updates for that path. */
    if (InterlockedExchange(&g_frame_upload_prepared, 0) == 0) PrepareWidescreenFrame();
    bool bink_frame = InterlockedExchange(&g_bink_frame, 0) != 0;
    if (bink_frame) {
        (void)PresentBinkCentered((uintptr_t)renderer);
    } else {
        if (g_original_present != NULL) g_original_present(renderer);
    }
}

/* When the map scene draws in a frame, that frame is presented without the
   centered 4:3 compose.  If the fixed-UI cursor was already sampled in centered
   640-space (IsCursorMapActive() == false), shift it back by the centering
   offset so it does not appear one frame to the left of the map cursor while
   the switch to a fullscreen UI is in progress. */
static void __fastcall RuntimeUiCursorDraw(void *input_manager, void *unused_edx) {
    (void)unused_edx;
    POINT *cursor = (POINT *)((uint8_t *)input_manager + UI_CURSOR_POSITION_OFFSET);
    LONG x = cursor->x;
    bool map_coordinates = g_widescreen && IsCursorMapActive();
    LONG width = map_coordinates ? WIDESCREEN_CLIENT_WIDTH : GAME_CLIENT_WIDTH;
    /* The fixed viewport's (-1, -1) sentinel can send the RLE blitter above its buffer. */
    if (x < 0 || x >= width || cursor->y < 0 || cursor->y >= GAME_CLIENT_HEIGHT) return;

    bool shift = g_widescreen && !map_coordinates &&
        InterlockedCompareExchange(&g_map_frame, 0, 0) != 0 &&
        *(const uintptr_t *)ORDINARY_MENU_OBJECT_ADDRESS == 0;
    if (shift) cursor->x = x + (WIDESCREEN_CLIENT_WIDTH - GAME_CLIENT_WIDTH) / 2;
    if (g_original_ui_cursor_draw != NULL) g_original_ui_cursor_draw(input_manager);
    if (shift) cursor->x = x;
}

static bool InstallTransitionHooks(void) {
    static const uint8_t effect_expected[] = {0xA1, 0xE8, 0x8A, 0x46, 0x00, 0x56};
    static const uint8_t fade_out_expected[] = {0xA1, 0xE8, 0x8A, 0x46, 0x00, 0x48, 0x74, 0x08};
    static const uint8_t fade_in_expected[] = {0xA1, 0xE8, 0x8A, 0x46, 0x00, 0x48, 0x74, 0x32};
    static const uint8_t factory_expected[] = {0x56, 0x8B, 0x74, 0x24, 0x08, 0x8B, 0x06};
    static const uint8_t load_expected[] = {0x6A, 0xFF, 0x68, 0x2E, 0xD1, 0x45, 0x00};
    static const uint8_t resume_expected[] = {0x83, 0xEC, 0x10, 0x56, 0x8B, 0xF1};
    static const uint8_t suspend_expected[] = {0x51, 0xC6, 0x81, 0x19, 0x02, 0x00, 0x00, 0x01};
    static const uint8_t menu_release_expected[] = {0x6A, 0xFF, 0x68, 0xC8, 0xD7, 0x45, 0x00};
    static const uint8_t system_menu_release_expected[] = {0x6A, 0xFF, 0x68, 0xA6, 0xEF, 0x45, 0x00};
    static const uint8_t ui_draw_expected[] = {0x56, 0x8B, 0xF1, 0x8A, 0x86, 0x78, 0x05, 0x00, 0x00};
    static const uint8_t ui_blit_expected[] = {0x8B, 0x44, 0x24, 0x10, 0x83, 0xF8, 0x1F, 0x75, 0x28};
    static const uint8_t save_load_expected[] = {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A, 0xFF};
    struct {
        uintptr_t address;
        const uint8_t *expected;
        size_t size;
        LPVOID hook;
        LPVOID *original;
    } hooks[] = {
        {MAP_EFFECT_FUNCTION_ADDRESS, effect_expected, sizeof(effect_expected),
         HOOK_ADDRESS(RuntimeMapEffect), (LPVOID *)&g_original_map_effect},
        {FADE_OUT_FUNCTION_ADDRESS, fade_out_expected, sizeof(fade_out_expected),
         HOOK_ADDRESS(RuntimeFadeOut), (LPVOID *)&g_original_fade_out},
        {FADE_IN_FUNCTION_ADDRESS, fade_in_expected, sizeof(fade_in_expected),
         HOOK_ADDRESS(RuntimeFadeIn), (LPVOID *)&g_original_fade_in},
        {SCRIPT_COMMAND_FACTORY_ADDRESS, factory_expected, sizeof(factory_expected),
         HOOK_ADDRESS(RuntimeCommandFactory), (LPVOID *)&g_original_command_factory},
        {MAP_SCENE_LOAD_FUNCTION_ADDRESS, load_expected, sizeof(load_expected),
         HOOK_ADDRESS(RuntimeMapLoad), (LPVOID *)&g_original_map_load},
        {MAP_SCENE_RESUME_FUNCTION_ADDRESS, resume_expected, sizeof(resume_expected),
         HOOK_ADDRESS(RuntimeMapResume), (LPVOID *)&g_original_map_resume},
        {MAP_SCENE_SUSPEND_FUNCTION_ADDRESS, suspend_expected, sizeof(suspend_expected),
         HOOK_ADDRESS(RuntimeMapSuspend), (LPVOID *)&g_original_map_suspend},
        {ORDINARY_MENU_RELEASE_FUNCTION_ADDRESS, menu_release_expected, sizeof(menu_release_expected),
         HOOK_ADDRESS(RuntimeMenuRelease), (LPVOID *)&g_original_menu_release},
        {SYSTEM_MENU_RELEASE_FUNCTION_ADDRESS, system_menu_release_expected, sizeof(system_menu_release_expected),
         HOOK_ADDRESS(RuntimeSystemMenuRelease), (LPVOID *)&g_original_system_menu_release},
        {UI_OBJECT_DRAW_FUNCTION_ADDRESS, ui_draw_expected, sizeof(ui_draw_expected),
         HOOK_ADDRESS(RuntimeUiObjectDraw), (LPVOID *)&g_original_ui_object_draw},
        {UI_SPRITE_BLIT_FUNCTION_ADDRESS, ui_blit_expected, sizeof(ui_blit_expected),
         HOOK_ADDRESS(RuntimeUiSpriteBlit), (LPVOID *)&g_original_ui_sprite_blit},
        {SAVE_LOAD_FUNCTION_ADDRESS, save_load_expected, sizeof(save_load_expected),
         HOOK_ADDRESS(RuntimeSaveLoad), (LPVOID *)&g_original_save_load},
    };
    /* Validate the entire set before any of these hooks is installed. */
    for (size_t i = 0; i < _countof(hooks); ++i) {
        if (memcmp((const void *)hooks[i].address, hooks[i].expected, hooks[i].size) != 0) {
            SetLastError(ERROR_REVISION_MISMATCH);
            return false;
        }
    }
    for (size_t i = 0; i < _countof(hooks); ++i) {
        if (!InstallGameHook(hooks[i].address, hooks[i].expected, hooks[i].size,
                             hooks[i].hook, hooks[i].original)) return false;
    }
    return true;
}

static bool InstallWidescreenHooks(void) {
    static const uint8_t renderer_width_expected[] = {0xE8, 0xDC, 0x43, 0x00, 0x00};
    static const uint8_t map_render_expected[] = {
        0x56, 0x8B, 0xF1, 0x8A, 0x86, 0x19, 0x02, 0x00, 0x00, 0x84, 0xC0,
    };
    static const uint8_t map_backdrop_capture_expected[] = {0xE8, 0x1E, 0xBD, 0xFF, 0xFF};
    static const uint8_t encounter_battle_capture_expected[] = {0xE8, 0x2A, 0xAE, 0x04, 0x00};
    static const uint8_t script_battle_capture_expected[] = {0xE8, 0xD2, 0x11, 0x04, 0x00};
    static const uint8_t map_scene_release_expected[] = {
        0x56, 0x57, 0x8B, 0xF9, 0x8B, 0xB7, 0x80, 0x02, 0x00, 0x00,
    };
    static const uint8_t present_expected[] = {
        0x83, 0xEC, 0x14, 0x56, 0x8B, 0xF1, 0x8A, 0x46, 0x30,
    };
    static const uint8_t frame_copy_expected[] = {
        0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x53, 0x56, 0x57,
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
        memcmp((const void *)MAP_BACKDROP_CAPTURE_HOOK_ADDRESS,
               map_backdrop_capture_expected,
               sizeof(map_backdrop_capture_expected)) != 0 ||
        memcmp((const void *)MAP_SCENE_RELEASE_FUNCTION_ADDRESS,
               map_scene_release_expected,
               sizeof(map_scene_release_expected)) != 0 ||
        memcmp((const void *)PRESENT_FUNCTION_ADDRESS,
               present_expected,
               sizeof(present_expected)) != 0 ||
        memcmp((const void *)FRAME_COPY_FUNCTION_ADDRESS,
               frame_copy_expected,
               sizeof(frame_copy_expected)) != 0 ||
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
         !InstallGameHook(RENDERER_WIDTH_HOOK_ADDRESS,
                          renderer_width_expected,
                          sizeof(renderer_width_expected),
                           HOOK_ADDRESS(RendererWidthHook),
                          &g_original_renderer_width) ||
         !InstallGameHook(MAP_RENDER_HOOK_ADDRESS,
                          map_render_expected,
                          sizeof(map_render_expected),
                          HOOK_ADDRESS(MapRenderHook),
                          &g_original_map_render) ||
         !InstallGameHook(MAP_BACKDROP_CAPTURE_HOOK_ADDRESS,
                          map_backdrop_capture_expected,
                          sizeof(map_backdrop_capture_expected),
                          HOOK_ADDRESS(MapBackdropCaptureHook),
                          &g_original_map_backdrop_capture) ||
         !InstallGameHook(ENCOUNTER_BATTLE_CAPTURE_CALL_ADDRESS, encounter_battle_capture_expected,
                          sizeof(encounter_battle_capture_expected), HOOK_ADDRESS(EncounterBattleCaptureHook),
                          &g_original_encounter_battle_capture) ||
         !InstallGameHook(SCRIPT_BATTLE_CAPTURE_CALL_ADDRESS, script_battle_capture_expected,
                          sizeof(script_battle_capture_expected), HOOK_ADDRESS(ScriptBattleCaptureHook),
                          &g_original_script_battle_capture) ||
         !InstallGameHook(MAP_SCENE_RELEASE_FUNCTION_ADDRESS,
                          map_scene_release_expected,
                          sizeof(map_scene_release_expected),
                          HOOK_ADDRESS(RuntimeMapSceneRelease),
                          (LPVOID *)&g_original_map_scene_release) ||
         !InstallGameHook(PRESENT_FUNCTION_ADDRESS,
                          present_expected,
                          sizeof(present_expected),
                          HOOK_ADDRESS(RuntimePresent),
                          (LPVOID *)&g_original_present) ||
         !InstallGameHook(FRAME_COPY_FUNCTION_ADDRESS,
                          frame_copy_expected,
                          sizeof(frame_copy_expected),
                          HOOK_ADDRESS(RuntimeFrameCopy),
                          (LPVOID *)&g_original_frame_copy) ||
         !InstallGameHook(MAP_UI_SPRITE_HOOK_ADDRESS,
                         map_ui_sprite_expected,
                         sizeof(map_ui_sprite_expected),
                         HOOK_ADDRESS(MapUiSpriteHook),
                         &g_original_map_ui_sprite) ||
        !InstallGameHook(DIALOG_TEXT_NAME_CALL_ADDRESS,
                         dialog_text_name_call_expected,
                         sizeof(dialog_text_name_call_expected),
                         HOOK_ADDRESS(DialogTextNameHook),
                         &g_original_dialog_name) ||
        !InstallGameHook(DIALOG_TEXT_BODY_CALL_ADDRESS,
                         dialog_text_body_call_expected,
                         sizeof(dialog_text_body_call_expected),
                         HOOK_ADDRESS(DialogTextBodyHook),
                         &g_original_dialog_body) ||
        !InstallGameHook(GLYPH_BLIT_HOOK_ADDRESS,
                         glyph_blit_expected,
                         sizeof(glyph_blit_expected),
                         HOOK_ADDRESS(GlyphBlitHook),
                         &g_original_glyph_blit) ||
         !InstallGameHook(BINK_FRAME_FUNCTION_ADDRESS,
                          bink_frame_expected,
                          sizeof(bink_frame_expected),
                          HOOK_ADDRESS(BinkFrameHook),
                          &g_original_bink_frame)) {
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
    return InstallTransitionHooks();
}

static bool InstallWindowedHooks(void) {
    static const uint8_t windowed_style_expected[] = {0x68, 0x00, 0x00, 0x00, 0x80};
    static const uint8_t windowed_style_replacement[] = {0x68, 0x00, 0x00, 0xCA, 0x00};
    static const uint8_t windowed_renderer_expected[] = {0x8A, 0x46, 0x30, 0x84, 0xC0};
    static const uint8_t windowed_renderer_replacement[] = {0x30, 0xC0, 0x88, 0x46, 0x30};
    static const uint8_t surface_format_expected[] = {0x8B, 0x10, 0x51, 0x50, 0xFF, 0x52, 0x18};
    static const uint8_t upload_clear_expected[] = {
        0x8B, 0x46, 0x08, 0x8B, 0x56, 0x14, 0x6A, 0x00, 0x68, 0x00, 0x00, 0x00,
        0x01, 0x8B, 0x08, 0x6A, 0x00, 0x52, 0x6A, 0x00, 0x50, 0xFF, 0x51, 0x14,
        0x8B, 0x46, 0x08, 0x8B, 0xCE,
    };
    static const uint8_t bink_lock_expected[] = {0x8B, 0x69, 0x04};
    static const uint8_t bink_lock_replacement[] = {0x8B, 0x69, 0x08};
    static const uint8_t bink_unlock_expected[] = {0x8B, 0x41, 0x04};
    static const uint8_t bink_unlock_replacement[] = {0x8B, 0x41, 0x08};
    static const uint8_t bink_format_expected[] = {0xBF, 0x09, 0x00, 0x00, 0x00};
    static const uint8_t bink_format_replacement[] = {0xBF, 0x0A, 0x00, 0x00, 0x00};
    static const uint8_t bink_pitch_expected[] = {0x8B, 0x49, 0x40};
    static const uint8_t bink_pitch_replacement[] = {0x8B, 0x49, 0x20};
    static const uint8_t bink_call_expected[] = {0xE8, 0x9C, 0x46, 0x00, 0x00};
    static const uint8_t ui_cursor_draw_expected[] = {
        0x8A, 0x81, 0x48, 0x02, 0x00, 0x00, 0x84, 0xC0, 0x74, 0x30,
    };

    if (!ApplyCodePatch(WINDOWED_STYLE_ADDRESS, windowed_style_expected,
                        windowed_style_replacement, sizeof(windowed_style_expected)) ||
        !ApplyCodePatch(WINDOWED_RENDERER_MODE_ADDRESS, windowed_renderer_expected,
                        windowed_renderer_replacement, sizeof(windowed_renderer_expected)) ||
        !ApplyCodePatch(BINK_LOCK_DEFAULT_SURFACE_ADDRESS, bink_lock_expected,
                        bink_lock_replacement, sizeof(bink_lock_expected)) ||
        !ApplyCodePatch(BINK_UNLOCK_DEFAULT_SURFACE_ADDRESS, bink_unlock_expected,
                        bink_unlock_replacement, sizeof(bink_unlock_expected)) ||
        !ApplyCodePatch(BINK_SURFACE_FORMAT_ADDRESS, bink_format_expected,
                        bink_format_replacement, sizeof(bink_format_expected)) ||
        !ApplyCodePatch(BINK_PITCH_ADDRESS, bink_pitch_expected,
                        bink_pitch_replacement, sizeof(bink_pitch_expected)) ||
        !InstallGameHook(SURFACE_FORMAT_HOOK_ADDRESS, surface_format_expected,
                         sizeof(surface_format_expected), HOOK_ADDRESS(SurfaceFormatHook),
                         &g_original_surface_format) ||
        /* The following upload overwrites every pixel of this windowed back surface. */
        !InstallGameHook(WINDOWED_UPLOAD_CLEAR_ADDRESS, upload_clear_expected,
                         sizeof(upload_clear_expected), (LPVOID)WINDOWED_UPLOAD_LOCK_ADDRESS, NULL) ||
        !InstallGameHook(BINK_UNLOCK_CALL_ADDRESS, bink_call_expected,
                         sizeof(bink_call_expected), HOOK_ADDRESS(BinkPresentCallHook), NULL) ||
        !InstallGameHook(UI_CURSOR_DRAW_HOOK_ADDRESS, ui_cursor_draw_expected,
                         sizeof(ui_cursor_draw_expected), HOOK_ADDRESS(RuntimeUiCursorDraw),
                         (LPVOID *)&g_original_ui_cursor_draw)) {
        return false;
    }
    return true;
}

static bool InstallFullscreenVblankHook(void) {
    static const uint8_t expected[] = {0x8B, 0x76, 0x04, 0x6A, 0x01};
    return InstallGameHook(FULLSCREEN_VBLANK_HOOK_ADDRESS, expected, sizeof(expected),
                           HOOK_ADDRESS(FullscreenVblankHook), &g_original_fullscreen_vblank);
}

static bool IsEncounterStateInvalid(uint32_t threshold, uint32_t progress);

static uint32_t ScaleEncounterThreshold(uint32_t threshold, uint32_t rate) {
    if (rate == 0) return 0;
    uint64_t scaled = ((uint64_t)threshold * 100u + rate - 1u) / rate;
    if (scaled == 0) scaled = 1;
    return scaled > UINT32_MAX ? UINT32_MAX : (uint32_t)scaled;
}

void ApplyEncounterThresholdRate(void) {
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

void ApplyEncounterInitialRate(uint32_t map_record) {
    g_encounter_map_enabled = *(const uint32_t *)(uintptr_t)(map_record + 0x378u) != 0;
    ApplyEncounterThresholdRate();
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

    if (!InstallGameHook(ENCOUNTER_INITIAL_HOOK_ADDRESS,
                         initial_expected,
                         sizeof(initial_expected),
                         HOOK_ADDRESS(EncounterInitialHook),
                         &g_original_encounter_initial)) return 232;
    if (!InstallGameHook(ENCOUNTER_REGENERATION_HOOK_ADDRESS,
                         regeneration_expected,
                         sizeof(regeneration_expected),
                         HOOK_ADDRESS(EncounterRegenerationHook),
                         NULL)) return 233;
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

    if (g_experience_multiplier != 1 &&
        !InstallGameHook(EXPERIENCE_GAIN_HOOK_ADDRESS, experience_gain_expected,
                         sizeof(experience_gain_expected), HOOK_ADDRESS(ExperienceGainHook), NULL)) return 212;
    if (g_experience_multiplier != 1 &&
        !InstallGameHook(EXPERIENCE_DISPLAY_HOOK_ADDRESS, experience_display_expected,
                         sizeof(experience_display_expected), HOOK_ADDRESS(ExperienceDisplayHook), NULL)) return 213;
    if (g_money_multiplier != 1 &&
        !InstallGameHook(MONEY_GAIN_HOOK_ADDRESS, money_gain_expected,
                         sizeof(money_gain_expected), HOOK_ADDRESS(MoneyGainHook), NULL)) return 222;
    if (g_money_multiplier != 1 &&
        !InstallGameHook(MONEY_DISPLAY_HOOK_ADDRESS, money_display_expected,
                         sizeof(money_display_expected), HOOK_ADDRESS(MoneyDisplayHook), NULL)) return 223;
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

static void UpdateCursorLock(HWND window, bool active) {
    RECT client;
    POINT origin = {0, 0};
    if (!g_cursor_lock || !active || window == NULL || !IsWindow(window)) {
        ClipCursor(NULL);
        return;
    }
    if (!GetClientRect(window, &client) || !ClientToScreen(window, &origin)) return;
    OffsetRect(&client, origin.x, origin.y);
    RECT current;
    if (!GetClipCursor(&current) || !EqualRect(&current, &client)) ClipCursor(&client);
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
        if (window != NULL) {
            SetWindowTextW(window, title);
            SetTimer(window, ENCOUNTER_FEEDBACK_TIMER_ID, 25, NULL);
        }
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
        KillTimer(window, ENCOUNTER_FEEDBACK_TIMER_ID);
        if (InterlockedCompareExchange(&g_window_proc_installed, 0, 0) == WINDOW_PROC_INSTALLED &&
            InterlockedCompareExchange(&g_title_restore_due, 0, 0) == 0) {
            SetWindowTextW(window, g_original_window_title);
        }
        return 0;
    }
    if (message == WM_TIMER && w_param == ENCOUNTER_FEEDBACK_TIMER_ID) {
        LONG due = InterlockedCompareExchange(&g_title_restore_due, 0, 0);
        if (due != 0 && (LONG)(GetTickCount() - (DWORD)due) >= 0) {
            KillTimer(window, ENCOUNTER_FEEDBACK_TIMER_ID);
            SetWindowTextW(window, g_original_window_title);
            InterlockedCompareExchange(&g_title_restore_due, 0, due);
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

    bool active_message = message == WM_ACTIVATEAPP || message == WM_ACTIVATE ||
                          message == WM_SETFOCUS || message == WM_KILLFOCUS ||
                          message == WM_DISPLAYCHANGE || message == WM_SIZE ||
                          message == WM_WINDOWPOSCHANGED || message == WM_SHOWWINDOW;
    bool active = message == WM_ACTIVATEAPP ? w_param != FALSE :
                  message == WM_ACTIVATE ? LOWORD(w_param) != WA_INACTIVE :
                  message == WM_KILLFOCUS ? false : true;
    if (message == WM_CLOSE || message == WM_DESTROY) {
        CloseGameVideo();
        ArmShutdownWatchdog();
    }
    if (message == WM_CLOSE || message == WM_DESTROY || message == WM_NCDESTROY) {
        InterlockedExchange(&g_runtime_shutting_down, 1);
    }
    if (message == WM_NCDESTROY) {
        ClipCursor(NULL);
        KillTimer(window, ENCOUNTER_FEEDBACK_TIMER_ID);
        InterlockedExchange(&g_window_proc_installed, WINDOW_PROC_DESTROYED);
        WriteRuntimeWindow(NULL);
    }
    if (active_message && !active) UpdateCursorLock(window, false);
    if (g_original_window_proc == NULL) return DefWindowProcW(window, message, w_param, l_param);
    LRESULT result = CallWindowProcW(g_original_window_proc, window, message, w_param, l_param);
    if (active_message && active) UpdateCursorLock(window, true);
    return result;
}

static bool InstallRuntimeWindowProcedure(void) {
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

__declspec(dllexport) DWORD WINAPI CastleRuntimeStart(void) {
    g_scale2 = LoadScale();
    g_windowed = LoadBool(L"Windowed", false);
    g_widescreen = LoadBool(L"Widescreen", false);
    g_cursor_lock = LoadBool(L"CursorLock", false);
    g_previous_exception_filter = SetUnhandledExceptionFilter(RuntimeUnhandledExceptionFilter);
    g_experience_multiplier = LoadMultiplier(L"ExperienceMultiplier");
    g_money_multiplier = LoadMultiplier(L"MoneyMultiplier");
    g_dynamic_encounter_rate = LoadPatchBool(L"DynamicEncounterRate", false);
    g_frame_pacing = LoadPatchBool(L"FramePacing60", true);
    if (MH_Initialize() != MH_OK) return 243;
    if (g_frame_pacing && !InstallFramePacingHooks()) return 246;
    DWORD settlement_result = InstallSettlementHooks();
    if (settlement_result != 1) return settlement_result;
    if (g_dynamic_encounter_rate) {
        DWORD encounter_result = InstallEncounterHooks();
        if (encounter_result != 1) return encounter_result;
    }
    if (g_widescreen) {
        if (!g_windowed || !InstallWidescreenHooks()) return 241;
    }
    if (g_windowed && !InstallWindowedHooks()) return 244;
    if (!g_windowed && LoadBool(L"FullscreenVSync", false) && !InstallFullscreenVblankHook()) return 245;
    if (g_windowed || g_dynamic_encounter_rate || g_frame_pacing) {
        if (!InstallCreateWindowHook()) return 242;
    }
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
