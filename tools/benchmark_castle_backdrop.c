/* DirectDraw upload benchmark. --window measures the primary Blt in a test window.
   Neither mode runs RPG.exe. Link with widescreen_backdrop.c, ddraw and dxguid. */
#include <windows.h>
#include <ddraw.h>

#include "../src/CastlePatches/castle_addresses.h"
#include "../src/CastlePatches/runtime/widescreen_backdrop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SIDE = 112, SOURCE_WIDTH = 992, SOURCE_HEIGHT = 576, ITERATIONS = 24 };
enum { UI_BYTES_PER_FRAME = 640 * 480 * (int)sizeof(uint16_t) };
static uintptr_t g_renderer[32];
static uint32_t g_mode[4] = {0, 864, 480, 16};
static uint16_t g_source[SOURCE_HEIGHT][SOURCE_WIDTH];
static uint16_t g_map_source[SOURCE_HEIGHT][SOURCE_WIDTH];
static uint16_t g_gray[480][224];
static LPDIRECTDRAWSURFACE4 g_back, g_scratch, g_sink;
static int g_transition_frame;
static int64_t g_old_lock_ticks, g_old_copy_ticks, g_old_unlock_ticks;

static void Check(HRESULT result, const char *action) {
    if (FAILED(result)) {
        fprintf(stderr, "%s: HRESULT=%08lX\n", action, (unsigned long)result);
        exit(1);
    }
}

static void OldCompose(void) {
    DDSURFACEDESC2 descriptor = {0};
    descriptor.dwSize = sizeof(descriptor);
    LARGE_INTEGER begin, locked, copied, end;
    QueryPerformanceCounter(&begin);
    Check(IDirectDrawSurface4_Lock(g_back, NULL, &descriptor, DDLOCK_WAIT, NULL), "previous Lock");
    QueryPerformanceCounter(&locked);
    for (size_t y = 0; y < 480; ++y) {
        uint16_t *row = (uint16_t *)((uint8_t *)descriptor.lpSurface + (ptrdiff_t)y * descriptor.lPitch);
        memmove(row + SIDE, row, 640 * sizeof(uint16_t));
        memcpy(row, g_gray[y], SIDE * sizeof(uint16_t));
        memcpy(row + 752, g_gray[y] + SIDE, SIDE * sizeof(uint16_t));
    }
    QueryPerformanceCounter(&copied);
    Check(IDirectDrawSurface4_Unlock(g_back, NULL), "previous Unlock");
    QueryPerformanceCounter(&end);
    g_old_lock_ticks += locked.QuadPart - begin.QuadPart;
    g_old_copy_ticks += copied.QuadPart - locked.QuadPart;
    g_old_unlock_ticks += end.QuadPart - copied.QuadPart;
}

static void VerifyUploadedFrame(int mode) {
    DDSURFACEDESC2 descriptor = {0};
    descriptor.dwSize = sizeof(descriptor);
    Check(IDirectDrawSurface4_Lock(g_back, NULL, &descriptor, DDLOCK_WAIT, NULL), "verify Lock");
    for (size_t y = 0; y < 480; ++y) {
        const uint16_t *row = (const uint16_t *)((uint8_t *)descriptor.lpSurface +
                                               (ptrdiff_t)y * descriptor.lPitch);
        if (memcmp(row + SIDE, &g_source[y + 48][64], 640 * sizeof(uint16_t)) != 0 ||
            memcmp(row, g_gray[y], SIDE * sizeof(uint16_t)) != 0 ||
            memcmp(row + 752, g_gray[y] + SIDE, SIDE * sizeof(uint16_t)) != 0) {
            fprintf(stderr, "mode %d produced different pixels\n", mode);
            exit(3);
        }
    }
    Check(IDirectDrawSurface4_Unlock(g_back, NULL), "verify Unlock");
}

static void Frame(int mode) {
    /* Match sub_406330: scratch Blt, lock, upload, unlock, then present Blt. */
    Check(IDirectDrawSurface4_Blt(g_back, NULL, g_scratch, NULL, DDBLT_WAIT, NULL), "clear Blt");
    DDSURFACEDESC2 descriptor = {0};
    descriptor.dwSize = sizeof(descriptor);
    Check(IDirectDrawSurface4_Lock(g_back, NULL, &descriptor, DDLOCK_WAIT, NULL), "upload Lock");
    if (mode >= 3) {
        /* Match a fresh map draw, followed by the CPU transition and normal upload. */
        memcpy(g_source, g_map_source, sizeof(g_source));
        CaptureMapBackdrop((uintptr_t)g_renderer, 1);
        int color = g_transition_frame++ % 16;
        if (mode == 4) {
            if (!DrawTransitionBackdrop((uintptr_t)g_renderer, color) ||
                !ComposeFixedFrame((uintptr_t)g_renderer, descriptor.lpSurface, descriptor.lPitch)) exit(2);
        } else {
            if (!ApplyMapTransition((uintptr_t)g_renderer, color)) exit(2);
            for (size_t y = 0; y < 480; ++y) {
                uint8_t *row = (uint8_t *)descriptor.lpSurface + (ptrdiff_t)y * descriptor.lPitch;
                memcpy(row, &g_source[y + 48][64], 864 * sizeof(uint16_t));
            }
        }
    } else if (mode == 2) {
        SetBackdropTransitionColor(0);
        if (!ComposeFixedFrame((uintptr_t)g_renderer, descriptor.lpSurface, descriptor.lPitch)) exit(2);
    } else {
        for (size_t y = 0; y < 480; ++y) {
            uint8_t *row = (uint8_t *)descriptor.lpSurface + (ptrdiff_t)y * descriptor.lPitch;
            memcpy(row, &g_source[y + 48][64], 864 * sizeof(uint16_t));
        }
    }
    Check(IDirectDrawSurface4_Unlock(g_back, NULL), "upload Unlock");
    if (mode == 1) OldCompose();
    Check(IDirectDrawSurface4_Blt(g_sink, NULL, g_back, NULL, DDBLT_WAIT, NULL), "sink Blt");
}

static bool CreateSurface(LPDIRECTDRAW4 draw, DWORD width, DWORD memory,
                          LPDIRECTDRAWSURFACE4 *surface) {
    DDSURFACEDESC2 descriptor = {0};
    descriptor.dwSize = sizeof(descriptor);
    descriptor.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS | DDSD_PIXELFORMAT;
    descriptor.dwWidth = width;
    descriptor.dwHeight = 480;
    descriptor.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | memory;
    descriptor.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    descriptor.ddpfPixelFormat.dwFlags = DDPF_RGB;
    descriptor.ddpfPixelFormat.dwRGBBitCount = 16;
    descriptor.ddpfPixelFormat.dwRBitMask = 0xF800;
    descriptor.ddpfPixelFormat.dwGBitMask = 0x07E0;
    descriptor.ddpfPixelFormat.dwBBitMask = 0x001F;
    return SUCCEEDED(IDirectDraw4_CreateSurface(draw, &descriptor, surface, NULL));
}

static int64_t Counter(void) {
    LARGE_INTEGER value;
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

static void MeasureWindowUpload(HWND window, LPDIRECTDRAWSURFACE4 primary, DWORD width,
                                int scale2, const char *memory, bool clear, double ticks_per_us) {
    enum { SAMPLES = 60, STAGES = 5 };
    int64_t totals[STAGES] = {0}, maximum = 0;
    int lock_retries = 0;
    for (int frame = -4; frame < SAMPLES; ++frame) {
        MSG message;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
        int64_t times[STAGES + 1];
        times[0] = Counter();
        if (clear) Check(IDirectDrawSurface4_Blt(g_back, NULL, g_scratch, NULL, DDBLT_WAIT, NULL), "scratch Blt");
        times[1] = Counter();
        DDSURFACEDESC2 desc = {0};
        desc.dwSize = sizeof(desc);
        HRESULT result;
        do {
            result = IDirectDrawSurface4_Lock(g_back, NULL, &desc, 0, NULL);
            if (frame >= 0 && result == DDERR_WASSTILLDRAWING) ++lock_retries;
        } while (result == DDERR_WASSTILLDRAWING);
        Check(result, "upload Lock");
        times[2] = Counter();
        for (size_t y = 0; y < GAME_CLIENT_HEIGHT; ++y) {
            memcpy((uint8_t *)desc.lpSurface + (ptrdiff_t)y * desc.lPitch,
                   &g_source[y + 48][64], width * sizeof(uint16_t));
        }
        times[3] = Counter();
        Check(IDirectDrawSurface4_Unlock(g_back, NULL), "upload Unlock");
        times[4] = Counter();
        RECT destination;
        GetClientRect(window, &destination);
        ClientToScreen(window, (POINT *)&destination);
        ClientToScreen(window, (POINT *)&destination.right);
        Check(IDirectDrawSurface4_Blt(primary, &destination, g_back, NULL, DDBLT_WAIT, NULL), "primary Blt");
        times[5] = Counter();
        if (frame < 0) continue;
        for (size_t i = 0; i < STAGES; ++i) totals[i] += times[i + 1] - times[i];
        if (times[5] - times[0] > maximum) maximum = times[5] - times[0];
    }
    double average = 1.0 / (ticks_per_us * SAMPLES);
    printf("%lu x%.1f %s clear=%d scratch_us=%.1f lock_us=%.1f copy_us=%.1f unlock_us=%.1f "
           "present_us=%.1f max_total_us=%.1f lock_retries=%d\n",
           (unsigned long)width, scale2 / 2.0, memory, (int)clear, totals[0] * average,
           totals[1] * average, totals[2] * average, totals[3] * average, totals[4] * average,
           maximum / ticks_per_us, lock_retries);
}

static int BenchmarkWindow(LPDIRECTDRAW4 draw, LARGE_INTEGER frequency) {
    SetProcessDPIAware();
    HWND window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"Castle benchmark",
                                  WS_POPUP, 0, 0, GAME_CLIENT_WIDTH, GAME_CLIENT_HEIGHT,
                                  NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (window == NULL) return 2;
    ShowWindow(window, SW_SHOWNOACTIVATE);
    Check(IDirectDraw4_SetCooperativeLevel(draw, window, DDSCL_NORMAL), "SetCooperativeLevel");
    LPDIRECTDRAWCLIPPER clipper = NULL;
    Check(IDirectDraw4_CreateClipper(draw, 0, &clipper, NULL), "CreateClipper");
    Check(IDirectDrawClipper_SetHWnd(clipper, 0, window), "SetHWnd");
    DDSURFACEDESC2 desc = {0};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    LPDIRECTDRAWSURFACE4 primary = NULL;
    Check(IDirectDraw4_CreateSurface(draw, &desc, &primary, NULL), "create primary");
    Check(IDirectDrawSurface4_SetClipper(primary, clipper), "SetClipper");
    IDirectDrawClipper_Release(clipper);

    int screen_width = GetSystemMetrics(SM_CXSCREEN), screen_height = GetSystemMetrics(SM_CYSCREEN);
    printf("desktop %dx%d\n", screen_width, screen_height);
    const DWORD widths[] = {GAME_CLIENT_WIDTH, WIDESCREEN_CLIENT_WIDTH};
    for (size_t i = 0; i < _countof(widths); ++i) {
        for (int system = 0; system <= 1; ++system) {
            DWORD memory = system ? DDSCAPS_SYSTEMMEMORY : 0;
            DWORD width = widths[i];
            if (!CreateSurface(draw, width, memory, &g_back) ||
                !CreateSurface(draw, width, memory, &g_scratch)) return 2;
            for (int scale2 = 2; scale2 <= 6; ++scale2) {
                int w = (int)width * scale2 / 2, h = GAME_CLIENT_HEIGHT * scale2 / 2;
                MoveWindow(window, (screen_width - w) / 2, (screen_height - h) / 2, w, h, TRUE);
                for (int clear = 1; clear >= 0; --clear) {
                    MeasureWindowUpload(window, primary, width, scale2, system ? "system" : "default",
                                        clear != 0, frequency.QuadPart / 1000000.0);
                }
            }
            IDirectDrawSurface4_Release(g_scratch);
            IDirectDrawSurface4_Release(g_back);
        }
    }
    IDirectDrawSurface4_Release(primary);
    DestroyWindow(window);
    return 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    *(uintptr_t *)((uint8_t *)g_renderer + RENDERER_MODE_OFFSET) = (uintptr_t)g_mode;
    *(uintptr_t *)((uint8_t *)g_renderer + RENDERER_PIXELS_OFFSET) = (uintptr_t)g_source;
    *(uint32_t *)((uint8_t *)g_renderer + RENDERER_BUFFER_WIDTH_OFFSET) = SOURCE_WIDTH;
    *(uint32_t *)((uint8_t *)g_renderer + RENDERER_BUFFER_HEIGHT_OFFSET) = SOURCE_HEIGHT;
    *(uint32_t *)((uint8_t *)g_renderer + RENDERER_BORDER_X_OFFSET) = 64;
    *(uint32_t *)((uint8_t *)g_renderer + RENDERER_BORDER_Y_OFFSET) = 48;
    *(uint32_t *)((uint8_t *)g_renderer + RENDERER_PIXEL_BYTES_OFFSET) = 2;
    *(uint32_t *)((uint8_t *)g_renderer + RENDERER_PIXEL_FORMAT_OFFSET) = 16;
    for (size_t y = 0; y < SOURCE_HEIGHT; ++y) {
        for (size_t x = 0; x < SOURCE_WIDTH; ++x) g_source[y][x] = (uint16_t)(x * 37u + y * 101u);
    }
    memcpy(g_map_source, g_source, sizeof(g_source));
    CaptureMapBackdrop((uintptr_t)g_renderer, 1);
    uint16_t *reference = malloc(864 * 480 * sizeof(uint16_t));
    if (reference == NULL || !ComposeFixedFrame((uintptr_t)g_renderer, reference, 864 * 2)) return 2;
    for (size_t y = 0; y < 480; ++y) {
        memcpy(g_gray[y], reference + y * 864, SIDE * sizeof(uint16_t));
        memcpy(g_gray[y] + SIDE, reference + y * 864 + 752, SIDE * sizeof(uint16_t));
    }
    free(reference);

    LPDIRECTDRAW original = NULL;
    LPDIRECTDRAW4 draw = NULL;
    Check(DirectDrawCreate(NULL, &original, NULL), "DirectDrawCreate");
    Check(IDirectDraw_QueryInterface(original, &IID_IDirectDraw4, (void **)&draw), "QueryInterface");
    IDirectDraw_Release(original);
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    if (argc == 2 && strcmp(argv[1], "--window") == 0) {
        int result = BenchmarkWindow(draw, frequency);
        IDirectDraw4_Release(draw);
        return result;
    }

    Check(IDirectDraw4_SetCooperativeLevel(draw, GetDesktopWindow(), DDSCL_NORMAL), "SetCooperativeLevel");
    DWORD requests[] = {0, DDSCAPS_SYSTEMMEMORY};
    const char *names[] = {"default", "system"};
    for (size_t memory = 0; memory < sizeof(requests) / sizeof(requests[0]); ++memory) {
        if (!CreateSurface(draw, WIDESCREEN_CLIENT_WIDTH, requests[memory], &g_back)) {
            printf("%s surface unavailable\n", names[memory]);
            continue;
        }
        if (!CreateSurface(draw, WIDESCREEN_CLIENT_WIDTH, requests[memory], &g_scratch) ||
            !CreateSurface(draw, WIDESCREEN_CLIENT_WIDTH, requests[memory], &g_sink)) return 2;
        DDBLTFX fill = {0};
        fill.dwSize = sizeof(fill);
        Check(IDirectDrawSurface4_Blt(g_scratch, NULL, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &fill), "scratch fill");
        for (int sample = 0; sample < 3; ++sample) {
            for (int mode = 0; mode < 5; ++mode) {
                static const char *modes[] = {"upload_only", "previous", "merged", "map_transition", "menu_transition"};
                memcpy(g_source, g_map_source, sizeof(g_source));
                for (int iteration = 0; iteration < 2; ++iteration) Frame(mode);
                LARGE_INTEGER start, end;
                g_old_lock_ticks = g_old_copy_ticks = g_old_unlock_ticks = 0;
                QueryPerformanceCounter(&start);
                for (int iteration = 0; iteration < ITERATIONS; ++iteration) Frame(mode);
                QueryPerformanceCounter(&end);
                double microseconds = (double)(end.QuadPart - start.QuadPart) * 1000000.0 /
                    (double)frequency.QuadPart / ITERATIONS;
                printf("%s sample=%d mode=%s frame_us=%.2f\n", names[memory], sample + 1,
                       modes[mode], microseconds);
                if (mode == 1) {
                    double tick_us = 1000000.0 / (double)frequency.QuadPart / ITERATIONS;
                    printf("  extra_lock_us=%.2f copy_us=%.2f extra_unlock_us=%.2f\n",
                           g_old_lock_ticks * tick_us, g_old_copy_ticks * tick_us,
                           g_old_unlock_ticks * tick_us);
                }
            }
        }
        memcpy(g_source, g_map_source, sizeof(g_source));
        Frame(1);
        VerifyUploadedFrame(1);
        Frame(2);
        VerifyUploadedFrame(2);
        printf("%s previous/merged pixels verified (center=%d bytes)\n", names[memory], UI_BYTES_PER_FRAME);
        IDirectDrawSurface4_Release(g_sink);
        IDirectDrawSurface4_Release(g_scratch);
        IDirectDrawSurface4_Release(g_back);
    }
    IDirectDraw4_Release(draw);
    return 0;
}
