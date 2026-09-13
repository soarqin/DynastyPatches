#include "runtime_internal.h"
#include "display_addresses.h"

#include <intrin.h>
#include <MinHook.h>
#include <string.h>

typedef void *(WINAPI *BinkBufferOpenFn)(HWND, uint32_t, uint32_t, uint32_t);
typedef void (WINAPI *BinkCloseFn)(void *);
typedef void (WINAPI *BinkBufferSetOffsetFn)(void *, uint32_t, uint32_t);
typedef int (WINAPI *BinkBufferSetScaleFn)(void *, uint32_t, uint32_t);
typedef HWND (WINAPI *CreateWindowExAFn)(DWORD, LPCSTR, LPCSTR, DWORD, int, int,
                                         int, int, HWND, HMENU, HINSTANCE, LPVOID);
typedef int (WINAPI *StretchDIBitsFn)(HDC, int, int, int, int, int, int, int, int,
                                      const void *, const BITMAPINFO *, UINT, DWORD);

enum {
    BINK_BUFFER_WINDOW_OFFSET_X = 0x30u,
    BINK_BUFFER_WINDOW_OFFSET_Y = 0x34u,
    BINK_BUFFER_XP_WINDOW_OFFSET = 0x58u,
    BINK_BUFFER_SCALE_CAPABILITIES_OFFSET = 0x38u,
    BINK_BUFFER_SCALE_CAPABILITIES = 0xFF000000u,
};

void SwordmanMovieCleanupHook(void);
void SwordmanBinkBlitRectHook(void);
void SwordmanBinkFrameLayoutHook(void);
void SwordmanMovieBufferLayoutHook(void);
void SwordmanMoviePaintHook(void);

void *g_display_original_cleanup_call;
const uintptr_t SwordmanBinkBlitRectContinueAddress = SWORDMAN_BINK_BLIT_RECT_CONTINUE_ADDRESS;
const uintptr_t SwordmanBinkFrameLayoutContinueAddress = SWORDMAN_BINK_FRAME_LAYOUT_CONTINUE_ADDRESS;
const uintptr_t SwordmanMovieBufferLayoutContinueAddress = SWORDMAN_MOVIE_BUFFER_LAYOUT_CONTINUE_ADDRESS;
const uintptr_t SwordmanMoviePaintContinueAddress = SWORDMAN_MOVIE_PAINT_CONTINUE_ADDRESS;
const uintptr_t SwordmanMoviePaintOriginalCallAddress = SWORDMAN_MOVIE_PAINT_ORIGINAL_CALL_ADDRESS;

static BinkBufferOpenFn g_original_buffer_open;
static BinkCloseFn g_original_buffer_close;
static BinkCloseFn g_bink_close;
static BinkCloseFn g_buffer_close;
static BinkBufferSetOffsetFn g_original_buffer_set_offset;
static BinkBufferSetScaleFn g_buffer_scale;
static WNDPROC g_original_window_proc;
static CreateWindowExAFn g_original_create_window;
static StretchDIBitsFn g_original_stretch_dibits;
static uint32_t g_first_movie_mode;
static bool g_software_movie;
static bool g_extended_bink_layout;
static void *g_movie_buffer;
static HWND g_movie_window;

static const uint8_t kNoCdExpected[] = {
    0x81, 0xEC, 0xE4, 0x00, 0x00, 0x00, 0x53, 0x55, 0x56, 0x57,
    0x8B, 0x3D, 0x34, 0xB0, 0x45, 0x00, 0x8B, 0xD9, 0x33, 0xF6,
};
static const uint8_t kWindowProcExpected[] = {
    0x56, 0x8B, 0x74, 0x24, 0x0C, 0x57, 0x8B, 0x7C, 0x24, 0x18,
};
static const uint8_t kBlitRectExpected[] = {0x83, 0xC2, 0x3C, 0x52, 0x50};
static const uint8_t kFrameLayoutExpected[] = {0x8B, 0x48, 0x14, 0x8B, 0x50, 0x10};
static const uint8_t kMovieBufferLayoutExpected[] = {
    0x8B, 0x56, 0x18, 0x6A, 0x01, 0x89, 0x15, 0x74, 0xF1, 0x58, 0x00,
};
static const uint8_t kCleanupExpected[] = {0xE8, 0x86, 0x9A, 0x04, 0x00};
static const uint8_t kMoviePaintExpected[] = {
    0x8B, 0x0D, 0x6C, 0xF1, 0x58, 0x00, 0xE8, 0xDA, 0x2E, 0xFB, 0xFF,
};

static bool CreateApiHook(LPCWSTR module, LPCSTR name, LPVOID detour, LPVOID *original) {
    MH_STATUS status = MH_CreateHookApi(module, name, detour, original);
    if (status == MH_OK) return true;
    SetLastError(status == MH_ERROR_MODULE_NOT_FOUND ? ERROR_MOD_NOT_FOUND :
                 status == MH_ERROR_FUNCTION_NOT_FOUND ? ERROR_PROC_NOT_FOUND :
                 status == MH_ERROR_MEMORY_ALLOC ? ERROR_NOT_ENOUGH_MEMORY : ERROR_FUNCTION_FAILED);
    return false;
}

static HMODULE EnsureBinkModule(void) {
    HMODULE bink = GetModuleHandleW(L"binkw32.dll");
    if (bink != NULL) return bink;

    bink = LoadLibraryW(L"binkw32.dll");
    if (bink == NULL && GetLastError() == ERROR_SUCCESS) {
        SetLastError(ERROR_MOD_NOT_FOUND);
    }
    return bink;
}

static bool ShouldScaleMovie(void) {
    return !g_software_movie || g_extended_bink_layout;
}

static void ScaleBuffer(void *buffer, LONG width, LONG height) {
    if (buffer != NULL && g_buffer_scale != NULL && ShouldScaleMovie() && width > 0 && height > 0) {
        uint32_t *capabilities = NULL;
        uint32_t saved_capabilities = 0;
        if (!g_extended_bink_layout) {
            capabilities = (uint32_t *)((uint8_t *)buffer + BINK_BUFFER_SCALE_CAPABILITIES_OFFSET);
            saved_capabilities = *capabilities;
            *capabilities |= BINK_BUFFER_SCALE_CAPABILITIES;
        }
        (void)g_buffer_scale(buffer, (uint32_t)width, (uint32_t)height);
        if (capabilities != NULL) *capabilities = saved_capabilities;
    }
}

static void ScaleMovieToClient(void) {
    RECT client;
    if (g_movie_window != NULL && GetClientRect(g_movie_window, &client)) {
        ScaleBuffer(g_movie_buffer, client.right - client.left, client.bottom - client.top);
    }
}

static void ScaleMovieToOuter(const RECT *outer) {
    if (g_movie_buffer == NULL || outer == NULL || !ShouldScaleMovie()) return;
    const uint8_t *buffer = (const uint8_t *)g_movie_buffer;
    ScaleBuffer(g_movie_buffer,
                outer->right - outer->left - *(const LONG *)(buffer + BINK_BUFFER_WINDOW_OFFSET_X),
                outer->bottom - outer->top - *(const LONG *)(buffer + BINK_BUFFER_WINDOW_OFFSET_Y));
}

static void RememberMovieBuffer(HWND window, void *buffer) {
    g_movie_buffer = buffer;
    g_movie_window = window;
    if (ShouldScaleMovie()) ScaleMovieToClient();
}

void __cdecl SwordmanScaleMovieBuffer(void *movie) {
    if (movie == NULL || !ShouldScaleMovie()) return;
    void *buffer = *(void **)((uint8_t *)movie + SWORDMAN_MOVIE_OBJECT_BUFFER_OFFSET);
    if (buffer == NULL) return;

    HWND window = *(HWND *)((uint8_t *)buffer + BINK_BUFFER_XP_WINDOW_OFFSET);
    RECT client;
    if (window == NULL || !GetClientRect(window, &client)) return;
    ScaleBuffer(buffer, client.right - client.left, client.bottom - client.top);
    PostMessageA(window, WM_PAINT, 0, 0);
}

static void *WINAPI MovieBufferOpen(HWND window, uint32_t width, uint32_t height, uint32_t flags) {
    if ((uintptr_t)_ReturnAddress() != SWORDMAN_BINK_BUFFER_OPEN_RETURN_ADDRESS) {
        return g_original_buffer_open(window, width, height, flags);
    }
    if (!g_extended_bink_layout) {
        for (unsigned attempt = 0; attempt < (g_software_movie ? 1u : 3u); ++attempt) {
            uint32_t mode = attempt == 0 ? g_first_movie_mode : attempt + 3u;
            void *buffer = g_original_buffer_open(window, width, height,
                                                  0xFF000000u | mode);
            if (buffer != NULL) {
                RememberMovieBuffer(window, buffer);
                return buffer;
            }
        }
        void *buffer = g_original_buffer_open(window, width, height, flags);
        if (buffer != NULL) RememberMovieBuffer(window, buffer);
        return buffer;
    }
    for (unsigned attempt = 0; attempt < (g_software_movie ? 1u : 3u); ++attempt) {
        uint32_t mode = attempt == 0 ? g_first_movie_mode : attempt + 3u;
        void *buffer = g_original_buffer_open(window, width, height, 0xFF000000u | mode);
        if (buffer != NULL) {
            RememberMovieBuffer(window, buffer);
            return buffer;
        }
    }
    return NULL;
}

static void WINAPI MovieBufferClose(void *buffer) {
    if (buffer == g_movie_buffer) {
        g_movie_buffer = NULL;
        g_movie_window = NULL;
    }
    g_original_buffer_close(buffer);
}

static void WINAPI MovieBufferSetOffset(void *buffer, uint32_t x, uint32_t y) {
    g_original_buffer_set_offset(buffer, x, y);
    if (buffer == g_movie_buffer && g_movie_window != NULL && ShouldScaleMovie()) {
        ScaleMovieToClient();
        PostMessageA(g_movie_window, WM_PAINT, 0, 0);
    }
}

static LRESULT CALLBACK MovieWindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    if (window == g_movie_window && g_movie_buffer != NULL && message == WM_SIZING && l_param != 0 &&
        ShouldScaleMovie()) {
        ScaleMovieToOuter((const RECT *)l_param);
    }
    LRESULT result = g_original_window_proc(window, message, w_param, l_param);
    return result;
}

static HWND WINAPI ResizableCreateWindow(DWORD ex_style, LPCSTR class_name, LPCSTR title,
                                         DWORD style, int x, int y, int width, int height,
                                         HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter) {
    if ((uintptr_t)_ReturnAddress() == SWORDMAN_CREATE_WINDOW_RETURN_ADDRESS && style == 0x10C00000u) {
        style = 0x10CF0000u;
    }
    return g_original_create_window(ex_style, class_name, title, style, x, y, width, height,
                                     parent, menu, instance, parameter);
}

static int WINAPI SmoothStretchDIBits(HDC hdc, int x_dest, int y_dest, int dest_width, int dest_height,
                                      int x_src, int y_src, int src_width, int src_height,
                                      const void *bits, const BITMAPINFO *info, UINT usage, DWORD rop) {
    if ((uintptr_t)_ReturnAddress() == SWORDMAN_STRETCH_DIBITS_RETURN_ADDRESS) {
        SetStretchBltMode(hdc, HALFTONE);
        SetBrushOrgEx(hdc, 0, 0, NULL);
    }
    return g_original_stretch_dibits(hdc, x_dest, y_dest, dest_width, dest_height,
                                     x_src, y_src, src_width, src_height, bits, info, usage, rop);
}

static bool __fastcall NoCdCheck(void *archive, void *unused_edx) {
    (void)unused_edx;
    char directory[SWORDMAN_ARCHIVE_PATH_CAPACITY];
    DWORD length = GetCurrentDirectoryA((DWORD)sizeof(directory), directory);
    if (length == 0 || length >= sizeof(directory)) {
        SetLastError(ERROR_INVALID_DATA);
        return false;
    }
    if (directory[length - 1] != '\\') {
        if (length + 1 >= sizeof(directory)) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return false;
        }
        directory[length++] = '\\';
        directory[length] = '\0';
    }
    memcpy((uint8_t *)archive + SWORDMAN_ARCHIVE_CD_DIRECTORY_OFFSET, directory, length + 1u);
    return true;
}

void __cdecl SwordmanCleanupVideo(void *movie) {
    void **bink = (void **)((uint8_t *)movie + SWORDMAN_MOVIE_OBJECT_BINK_OFFSET);
    void **buffer = (void **)((uint8_t *)movie + SWORDMAN_MOVIE_OBJECT_BUFFER_OFFSET);
    if (*buffer == g_movie_buffer) {
        g_movie_buffer = NULL;
        g_movie_window = NULL;
    }
    if (*bink != NULL) {
        g_bink_close(*bink);
        *bink = NULL;
        *(void **)SWORDMAN_CURRENT_BINK_ADDRESS = NULL;
    }
    if (*buffer != NULL) {
        g_buffer_close(*buffer);
        *buffer = NULL;
        *(void **)SWORDMAN_CURRENT_BUFFER_ADDRESS = NULL;
    }
}

static bool ValidateDisplaySites(uint32_t flags, bool extended_bink_layout) {
    static const uint8_t window_style[] = {0x68, 0x00, 0x00, 0xC0, 0x10};
    static const uint8_t window_call[] = {0xFF, 0x15, 0x5C, 0xB1, 0x45, 0x00};
    static const uint8_t stretch_call[] = {0xFF, 0x15, 0x10, 0xB0, 0x45, 0x00};
    static const uint8_t buffer_open_call[] = {
        0x6A, 0x00, 0x52, 0x50, 0x51, 0xFF, 0x15, 0xD8, 0xB1, 0x45, 0x00,
    };
    static const uint8_t buffer_set_offset_call[] = {
        0x6A, 0x00, 0x6A, 0x00, 0x50, 0xFF, 0x15, 0xC4, 0xB1, 0x45, 0x00,
    };
    if ((flags & SWORDMAN_NO_CD) != 0 &&
        !ValidateGameBytes(SWORDMAN_NO_CD_HOOK_ADDRESS, kNoCdExpected, sizeof(kNoCdExpected))) return false;
    if ((flags & SWORDMAN_RESIZABLE) != 0 &&
        (!ValidateGameBytes(SWORDMAN_CREATE_WINDOW_STYLE_ADDRESS, window_style, sizeof(window_style)) ||
         !ValidateGameBytes(SWORDMAN_CREATE_WINDOW_CALL_ADDRESS, window_call, sizeof(window_call)))) return false;
    if ((flags & SWORDMAN_SMOOTH) != 0 &&
        !ValidateGameBytes(SWORDMAN_STRETCH_DIBITS_CALL_ADDRESS, stretch_call, sizeof(stretch_call))) return false;
    if ((flags & SWORDMAN_MOVIE) != 0 &&
        (!ValidateGameBytes(SWORDMAN_WINDOW_PROC_HOOK_ADDRESS, kWindowProcExpected, sizeof(kWindowProcExpected)) ||
         !ValidateGameBytes(SWORDMAN_BINK_BUFFER_OPEN_CALL_ADDRESS, buffer_open_call, sizeof(buffer_open_call)) ||
         !ValidateGameBytes(SWORDMAN_BINK_BUFFER_SET_OFFSET_CALL_ADDRESS, buffer_set_offset_call,
                            sizeof(buffer_set_offset_call)) ||
         !ValidateGameBytes(SWORDMAN_MOVIE_PAINT_HOOK_ADDRESS, kMoviePaintExpected,
                            sizeof(kMoviePaintExpected)))) return false;
    if ((flags & SWORDMAN_MOVIE) != 0 && extended_bink_layout &&
        (!ValidateGameBytes(SWORDMAN_BINK_BLIT_RECT_HOOK_ADDRESS, kBlitRectExpected, sizeof(kBlitRectExpected)) ||
         !ValidateGameBytes(SWORDMAN_BINK_FRAME_LAYOUT_HOOK_ADDRESS, kFrameLayoutExpected, sizeof(kFrameLayoutExpected)))) return false;
    if ((flags & SWORDMAN_MOVIE) != 0 && !extended_bink_layout &&
        !ValidateGameBytes(SWORDMAN_MOVIE_BUFFER_LAYOUT_HOOK_ADDRESS, kMovieBufferLayoutExpected,
                           sizeof(kMovieBufferLayoutExpected))) return false;
    if ((flags & SWORDMAN_MOVIE_CLEANUP) != 0 &&
        !ValidateGameBytes(SWORDMAN_MOVIE_CLEANUP_HOOK_ADDRESS, kCleanupExpected, sizeof(kCleanupExpected))) return false;
    return true;
}

bool CreateDisplayHooks(const SwordmanOptions *options) {
    if (options == NULL || (options->flags & ~(uint32_t)SWORDMAN_ALL_FLAGS) != 0 ||
        ((options->flags & SWORDMAN_SOFTWARE_MOVIE) != 0 && (options->flags & SWORDMAN_MOVIE) == 0)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    uint32_t flags = options->flags;
    g_extended_bink_layout = false;

    if ((flags & (SWORDMAN_MOVIE | SWORDMAN_MOVIE_CLEANUP)) != 0) {
        HMODULE bink = EnsureBinkModule();
        if (bink == NULL) {
            return false;
        }
        if ((flags & SWORDMAN_MOVIE) != 0) {
            g_extended_bink_layout = GetProcAddress(bink, "_BinkBufferGetDescription@4") != NULL;
            g_buffer_scale = (BinkBufferSetScaleFn)GetProcAddress(bink, "_BinkBufferSetScale@12");
            if (g_buffer_scale == NULL) {
                SetLastError(ERROR_PROC_NOT_FOUND);
                return false;
            }
        }
        if ((flags & SWORDMAN_MOVIE_CLEANUP) != 0) {
            g_bink_close = (BinkCloseFn)GetProcAddress(bink, "_BinkClose@4");
            g_buffer_close = (BinkCloseFn)GetProcAddress(bink, "_BinkBufferClose@4");
            if (g_bink_close == NULL || g_buffer_close == NULL) {
                SetLastError(ERROR_PROC_NOT_FOUND);
                return false;
            }
        }
    }
    if (!ValidateDisplaySites(flags, g_extended_bink_layout)) return false;

    if ((flags & SWORDMAN_MOVIE) != 0) {
        g_software_movie = (flags & SWORDMAN_SOFTWARE_MOVIE) != 0;
        g_first_movie_mode = g_software_movie ? 2u : 3u;
        if (!CreateApiHook(L"binkw32.dll", "_BinkBufferOpen@16", HOOK_ADDRESS(MovieBufferOpen),
                           (LPVOID *)&g_original_buffer_open) ||
            !CreateApiHook(L"binkw32.dll", "_BinkBufferClose@4", HOOK_ADDRESS(MovieBufferClose),
                           (LPVOID *)&g_original_buffer_close) ||
            !CreateApiHook(L"binkw32.dll", "_BinkBufferSetOffset@12", HOOK_ADDRESS(MovieBufferSetOffset),
                           (LPVOID *)&g_original_buffer_set_offset) ||
            !CreateGameHook(SWORDMAN_WINDOW_PROC_HOOK_ADDRESS, kWindowProcExpected, sizeof(kWindowProcExpected),
                            HOOK_ADDRESS(MovieWindowProc), (LPVOID *)&g_original_window_proc) ||
            !CreateGameHook(SWORDMAN_MOVIE_PAINT_HOOK_ADDRESS, kMoviePaintExpected,
                            sizeof(kMoviePaintExpected),
                            HOOK_ADDRESS(SwordmanMoviePaintHook), NULL)) return false;
        if (g_extended_bink_layout &&
            (!CreateGameHook(SWORDMAN_BINK_BLIT_RECT_HOOK_ADDRESS, kBlitRectExpected, sizeof(kBlitRectExpected),
                             HOOK_ADDRESS(SwordmanBinkBlitRectHook), NULL) ||
               !CreateGameHook(SWORDMAN_BINK_FRAME_LAYOUT_HOOK_ADDRESS, kFrameLayoutExpected, sizeof(kFrameLayoutExpected),
                               HOOK_ADDRESS(SwordmanBinkFrameLayoutHook), NULL))) return false;
        if (!g_extended_bink_layout &&
            !CreateGameHook(SWORDMAN_MOVIE_BUFFER_LAYOUT_HOOK_ADDRESS, kMovieBufferLayoutExpected,
                            sizeof(kMovieBufferLayoutExpected), HOOK_ADDRESS(SwordmanMovieBufferLayoutHook), NULL)) return false;
    }
    if ((flags & SWORDMAN_MOVIE_CLEANUP) != 0 &&
        !CreateGameHook(SWORDMAN_MOVIE_CLEANUP_HOOK_ADDRESS, kCleanupExpected, sizeof(kCleanupExpected),
                        HOOK_ADDRESS(SwordmanMovieCleanupHook), &g_display_original_cleanup_call)) return false;
    if ((flags & SWORDMAN_NO_CD) != 0 &&
        !CreateGameHook(SWORDMAN_NO_CD_HOOK_ADDRESS, kNoCdExpected, sizeof(kNoCdExpected),
                        HOOK_ADDRESS(NoCdCheck), NULL)) return false;
    if ((flags & SWORDMAN_RESIZABLE) != 0 &&
        !CreateApiHook(L"user32.dll", "CreateWindowExA", HOOK_ADDRESS(ResizableCreateWindow),
                       (LPVOID *)&g_original_create_window)) return false;
    if ((flags & SWORDMAN_SMOOTH) != 0 &&
        !CreateApiHook(L"gdi32.dll", "StretchDIBits", HOOK_ADDRESS(SmoothStretchDIBits),
                       (LPVOID *)&g_original_stretch_dibits)) return false;
    return true;
}
