#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define GAME_WINDOW_HANDLE_ADDRESS ((uintptr_t)0x0046F384u)
#define GET_CURSOR_POS_IAT_ADDRESS ((uintptr_t)0x00460204u)
#define GAME_CLIENT_WIDTH 640
#define GAME_CLIENT_HEIGHT 480

typedef BOOL (WINAPI *GetCursorPosFn)(LPPOINT);

static HINSTANCE g_instance;
static GetCursorPosFn g_original_get_cursor_pos;
static volatile LONG g_hook_installed;
static uint32_t g_scale2 = 2;
static bool g_cursor_lock;
static HWND g_window;

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

static bool LoadBool(const wchar_t *key, bool default_value) {
    wchar_t path[MAX_PATH];
    if (!GetConfigPath(path, _countof(path))) return default_value;
    return GetPrivateProfileIntW(L"Display", key, default_value ? 1 : 0, path) != 0;
}

static HWND ReadGameWindow(void) {
    return *(HWND *)(uintptr_t)GAME_WINDOW_HANDLE_ADDRESS;
}

static BOOL WINAPI RuntimeGetCursorPos(LPPOINT point) {
    GetCursorPosFn original = g_original_get_cursor_pos;
    if (original == NULL || point == NULL || !original(point)) return FALSE;

    HWND window = g_window != NULL ? g_window : ReadGameWindow();
    if (window == NULL || !IsWindow(window) || !ScreenToClient(window, point)) {
        point->x = 0;
        point->y = 0;
        return FALSE;
    }

    point->x = (LONG)(((int64_t)point->x * 2) / (int64_t)g_scale2);
    point->y = (LONG)(((int64_t)point->y * 2) / (int64_t)g_scale2);
    return TRUE;
}

static bool InstallCursorHook(void) {
    if (InterlockedCompareExchange(&g_hook_installed, 1, 0) != 0) return true;
    volatile GetCursorPosFn *iat = (volatile GetCursorPosFn *)(uintptr_t)GET_CURSOR_POS_IAT_ADDRESS;
    GetCursorPosFn original = *iat;
    if (original == NULL || original == RuntimeGetCursorPos) {
        InterlockedExchange(&g_hook_installed, 0);
        return false;
    }
    g_original_get_cursor_pos = original;

    DWORD old_protection = 0;
    if (!VirtualProtect((LPVOID)iat, sizeof(*iat), PAGE_READWRITE, &old_protection)) {
        InterlockedExchange(&g_hook_installed, 0);
        return false;
    }
    *iat = RuntimeGetCursorPos;
    DWORD ignored = 0;
    VirtualProtect((LPVOID)iat, sizeof(*iat), old_protection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)RuntimeGetCursorPos, 1);
    return true;
}

static void ResizeGameWindow(HWND window) {
    LONG client_width = (LONG)(((uint64_t)GAME_CLIENT_WIDTH * g_scale2 + 1u) / 2u);
    LONG client_height = (LONG)(((uint64_t)GAME_CLIENT_HEIGHT * g_scale2 + 1u) / 2u);
    RECT rect = {0, 0, client_width, client_height};
    LONG style = GetWindowLongA(window, GWL_STYLE);
    LONG ex_style = GetWindowLongA(window, GWL_EXSTYLE);
    if (!AdjustWindowRectEx(&rect, (DWORD)style, GetMenu(window) != NULL, (DWORD)ex_style)) return;

    RECT work_area = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    LONG width = rect.right - rect.left;
    LONG height = rect.bottom - rect.top;
    LONG x = work_area.left + ((work_area.right - work_area.left) - width) / 2;
    LONG y = work_area.top + ((work_area.bottom - work_area.top) - height) / 2;
    if (x < work_area.left) x = work_area.left;
    if (y < work_area.top) y = work_area.top;
    MoveWindow(window, x, y, width, height, TRUE);
}

static void UpdateCursorClip(HWND window) {
    if (!g_cursor_lock || window == NULL || !IsWindow(window) || GetForegroundWindow() != window) {
        ClipCursor(NULL);
        return;
    }

    RECT client = {0};
    POINT top_left;
    POINT bottom_right;
    if (!GetClientRect(window, &client)) {
        ClipCursor(NULL);
        return;
    }
    top_left.x = client.left;
    top_left.y = client.top;
    bottom_right.x = client.right;
    bottom_right.y = client.bottom;
    if (!ClientToScreen(window, &top_left) || !ClientToScreen(window, &bottom_right)) {
        ClipCursor(NULL);
        return;
    }

    RECT screen_rect = {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
    ClipCursor(&screen_rect);
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
    /* The game's DirectDraw setup performs its own initial MoveWindow call
       after publishing the HWND.  Reapply the same client size during that
       short initialization window, then leave all further work in this
       target-process worker. */
    for (int attempt = 0; attempt < 20 && IsWindow(g_window); ++attempt) {
        ResizeGameWindow(g_window);
        InstallCursorHook();
        UpdateCursorClip(g_window);
        Sleep(100);
    }

    DWORD missing_since = 0;
    for (;;) {
        HWND current = ReadGameWindow();
        if (current != NULL && IsWindow(current)) {
            g_window = current;
            missing_since = 0;
            UpdateCursorClip(g_window);
        } else {
            ClipCursor(NULL);
            if (missing_since == 0) {
                missing_since = GetTickCount();
            } else if (GetTickCount() - missing_since >= 1000u) {
                /* The legacy game can destroy its top-level window before
                   returning from shutdown.  Keep the process from remaining
                   as a headless RPG.exe after that teardown path. */
                ExitProcess(0);
            }
        }
        Sleep(50);
    }
}

__declspec(dllexport) DWORD WINAPI CastleRuntimeStart(void) {
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
