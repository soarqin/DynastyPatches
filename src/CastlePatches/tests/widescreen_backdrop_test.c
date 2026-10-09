#include "../castle_addresses.h"
#include "../runtime/widescreen_backdrop.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    SIDE_WIDTH = (WIDESCREEN_CLIENT_WIDTH - GAME_CLIENT_WIDTH) / 2,
    MAP_BORDER_X = 64,
    MAP_BORDER_Y = 48,
    MAP_PITCH = WIDESCREEN_CLIENT_WIDTH + 2 * MAP_BORDER_X,
    MAP_HEIGHT = GAME_CLIENT_HEIGHT + 2 * MAP_BORDER_Y,
    SURFACE_PITCH = WIDESCREEN_CLIENT_WIDTH + 8,
    SURFACE_HEIGHT = GAME_CLIENT_HEIGHT + 1,
};

typedef struct {
    uint16_t *top;
    int32_t pitch;
    uint16_t pixels[SURFACE_HEIGHT][SURFACE_PITCH];
} FakeSurface;

static uintptr_t g_renderer[0x74u / sizeof(uintptr_t) + 1];
static uint32_t g_mode[4];
static uint16_t g_map[MAP_HEIGHT][MAP_PITCH];
static uint16_t g_source_before[MAP_HEIGHT][MAP_PITCH];
static FakeSurface g_back;
static const uint16_t SENTINEL = 0xA55Au;
static const uintptr_t MAP_SCENE = 1u;

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "line %d: %s\n", __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static uint16_t *SurfaceRow(int32_t y) {
    return (uint16_t *)((uint8_t *)g_back.top + (ptrdiff_t)y * g_back.pitch);
}

static void SetPointer(size_t offset, const void *value) {
    *(uintptr_t *)((uint8_t *)g_renderer + offset) = (uintptr_t)value;
}

static void SetField(size_t offset, uint32_t value) {
    *(uint32_t *)((uint8_t *)g_renderer + offset) = value;
}

static void SetupSurface(bool negative_pitch) {
    g_back.pitch = (negative_pitch ? -SURFACE_PITCH : SURFACE_PITCH) * 2;
    g_back.top = g_back.pixels[negative_pitch ? GAME_CLIENT_HEIGHT - 1 : 0];
    for (size_t y = 0; y < SURFACE_HEIGHT; ++y) {
        for (size_t x = 0; x < SURFACE_PITCH; ++x) g_back.pixels[y][x] = SENTINEL;
    }
}

static uint16_t UiPixel(int32_t x, int32_t y) {
    return (uint16_t)((uint32_t)x * 37u + (uint32_t)y * 101u + 11u);
}

static void ResetUi(void) {
    for (size_t y = 0; y < SURFACE_HEIGHT; ++y) {
        for (size_t x = 0; x < SURFACE_PITCH; ++x) g_back.pixels[y][x] = SENTINEL;
    }
    for (int32_t y = 0; y < GAME_CLIENT_HEIGHT; ++y) {
        for (int32_t x = 0; x < GAME_CLIENT_WIDTH; ++x) {
            g_map[MAP_BORDER_Y + y][MAP_BORDER_X + x] = UiPixel(x, y);
        }
    }
    memcpy(g_source_before, g_map, sizeof(g_map));
}

static bool Compose(uintptr_t renderer) {
    return ComposeFixedFrame(renderer, g_back.top, g_back.pitch);
}

static void CheckFrame(uint16_t left, uint16_t right) {
    for (int32_t y = 0; y < GAME_CLIENT_HEIGHT; ++y) {
        uint16_t *row = SurfaceRow(y);
        for (int32_t x = 0; x < SIDE_WIDTH; ++x) {
            REQUIRE(row[x] == left);
            REQUIRE(row[SIDE_WIDTH + GAME_CLIENT_WIDTH + x] == right);
        }
        for (int32_t x = 0; x < GAME_CLIENT_WIDTH; ++x) {
            REQUIRE(row[SIDE_WIDTH + x] == UiPixel(x, y));
        }
        for (int32_t x = WIDESCREEN_CLIENT_WIDTH; x < SURFACE_PITCH; ++x) REQUIRE(row[x] == SENTINEL);
    }
    for (size_t x = 0; x < SURFACE_PITCH; ++x) {
        REQUIRE(g_back.pixels[GAME_CLIENT_HEIGHT][x] == SENTINEL);
    }
    REQUIRE(memcmp(g_source_before, g_map, sizeof(g_map)) == 0);
}

static void CheckUnwritten(void) {
    for (size_t y = 0; y < SURFACE_HEIGHT; ++y) {
        for (size_t x = 0; x < SURFACE_PITCH; ++x) REQUIRE(g_back.pixels[y][x] == SENTINEL);
    }
}

static void SetMap(uint16_t left, uint16_t right) {
    for (size_t y = 0; y < MAP_HEIGHT; ++y) {
        for (size_t x = 0; x < MAP_PITCH; ++x) g_map[y][x] = 0xFFFF;
    }
    for (size_t y = MAP_BORDER_Y; y < MAP_BORDER_Y + GAME_CLIENT_HEIGHT; ++y) {
        for (size_t x = 0; x < SIDE_WIDTH; ++x) {
            g_map[y][MAP_BORDER_X + x] = left;
            g_map[y][MAP_BORDER_X + WIDESCREEN_CLIENT_WIDTH - SIDE_WIDTH + x] = right;
        }
    }
}

static void SetupRenderer(void) {
    memset(g_renderer, 0, sizeof(g_renderer));
    g_mode[1] = WIDESCREEN_CLIENT_WIDTH;
    g_mode[2] = GAME_CLIENT_HEIGHT;
    SetPointer(RENDERER_MODE_OFFSET, g_mode);
    SetPointer(RENDERER_PIXELS_OFFSET, g_map);
    SetField(RENDERER_BUFFER_WIDTH_OFFSET, MAP_PITCH);
    SetField(RENDERER_BUFFER_HEIGHT_OFFSET, MAP_HEIGHT);
    SetField(RENDERER_BORDER_X_OFFSET, MAP_BORDER_X);
    SetField(RENDERER_BORDER_Y_OFFSET, MAP_BORDER_Y);
    SetField(RENDERER_PIXEL_BYTES_OFFSET, sizeof(uint16_t));
    SetField(RENDERER_PIXEL_FORMAT_OFFSET, 16);
}

static uint16_t ReferenceTint(uint16_t pixel, bool rgb565, int color, bool output565) {
    uint32_t r = (pixel >> (rgb565 ? 11 : 10)) & 31u;
    uint32_t g = (pixel >> 5) & (rgb565 ? 63u : 31u);
    uint32_t b = pixel & 31u;
    r = (r << 3) | (r >> 2);
    g = rgb565 ? (g << 2) | (g >> 4) : (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    uint32_t gray = ((77u * r + 150u * g + 29u * b + 128u) >> 8) / 2;
    r = (r * (uint32_t)color + gray * (uint32_t)(15 - color) + 7) / 15;
    g = (g * (uint32_t)color + gray * (uint32_t)(15 - color) + 7) / 15;
    b = (b * (uint32_t)color + gray * (uint32_t)(15 - color) + 7) / 15;
    return output565 ? (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)) :
        (uint16_t)((r >> 3) << 10 | (g >> 3) << 5 | (b >> 3));
}

static void TestMapTransition(uintptr_t renderer) {
    SetupRenderer();
    for (size_t y = 0; y < MAP_HEIGHT; ++y) {
        for (size_t x = 0; x < MAP_PITCH; ++x) {
            g_source_before[y][x] = (uint16_t)(x * 137u + y * 173u);
        }
    }
    for (uint32_t format = 15; format <= 16; ++format) {
        SetField(RENDERER_PIXEL_FORMAT_OFFSET, format);
        for (int color = 0; color <= 15; ++color) {
            memcpy(g_map, g_source_before, sizeof(g_map));
            REQUIRE(ApplyMapTransition(renderer, color));
            for (size_t y = 0; y < MAP_HEIGHT; ++y) {
                for (size_t x = 0; x < MAP_PITCH; ++x) {
                    bool inside = y >= MAP_BORDER_Y && y < MAP_BORDER_Y + GAME_CLIENT_HEIGHT &&
                        x >= MAP_BORDER_X && x < MAP_BORDER_X + WIDESCREEN_CLIENT_WIDTH;
                    uint16_t original = g_source_before[y][x];
                    uint16_t expected = inside && color != 15 ?
                        ReferenceTint(original, format == 16, color, format == 16) : original;
                    REQUIRE(g_map[y][x] == expected);
                }
            }
        }
    }
    REQUIRE(!ApplyMapTransition(renderer, -1));
    REQUIRE(!ApplyMapTransition(renderer, 16));
    SetBackdropTransitionColor(0);
}

static void TestFrozenTransition(uintptr_t renderer) {
    SetupRenderer();
    for (size_t y = 0; y < MAP_HEIGHT; ++y) {
        for (size_t x = 0; x < MAP_PITCH; ++x) {
            g_map[y][x] = (uint16_t)(x * 23u + y * 47u);
        }
    }
    memcpy(g_source_before, g_map, sizeof(g_map));
    for (uint32_t format = 15; format <= 16; ++format) {
        SetField(RENDERER_PIXEL_FORMAT_OFFSET, format);
        memcpy(g_map, g_source_before, sizeof(g_map));
        CaptureMapBackdrop(renderer, MAP_SCENE);
        REQUIRE(HasTransitionBackdrop(renderer, MAP_SCENE));
        for (int step = 0; step <= 30; ++step) {
            int color = step <= 15 ? 15 - step : step - 15;
            SetupSurface((color & 1) != 0);
            for (size_t y = 0; y < MAP_HEIGHT; ++y) {
                for (size_t x = 0; x < MAP_PITCH; ++x) g_map[y][x] = SENTINEL;
            }
            REQUIRE(DrawTransitionBackdrop(renderer, color));
            REQUIRE(Compose(renderer));
            for (int y = 0; y < GAME_CLIENT_HEIGHT; ++y) {
                const uint16_t *row = SurfaceRow(y);
                for (int x = 0; x < WIDESCREEN_CLIENT_WIDTH; ++x) {
                    bool output565 = format == 16 || x < SIDE_WIDTH || x >= SIDE_WIDTH + GAME_CLIENT_WIDTH;
                    uint16_t original = g_source_before[MAP_BORDER_Y + y][MAP_BORDER_X + x];
                    REQUIRE(row[x] == ReferenceTint(original, format == 16, color, output565));
                }
                for (int x = WIDESCREEN_CLIENT_WIDTH; x < SURFACE_PITCH; ++x) REQUIRE(row[x] == SENTINEL);
            }
            for (size_t x = 0; x < SURFACE_PITCH; ++x) {
                REQUIRE(g_back.pixels[GAME_CLIENT_HEIGHT][x] == SENTINEL);
            }
        }
    }
    /* A renderer/source change, map disposal or explicit load invalidates the snapshot. */
    InvalidateMapBackdrop(MAP_SCENE + 1);
    REQUIRE(HasTransitionBackdrop(renderer, MAP_SCENE));
    SetPointer(RENDERER_PIXELS_OFFSET, NULL);
    REQUIRE(!HasTransitionBackdrop(renderer, MAP_SCENE));
    SetPointer(RENDERER_PIXELS_OFFSET, g_map);
    InvalidateMapBackdrop(MAP_SCENE);
    REQUIRE(!DrawTransitionBackdrop(renderer, 0));
    REQUIRE(CaptureTransitionBackdrop(renderer, MAP_SCENE));
    ResetMapBackdrop();
    REQUIRE(!HasTransitionBackdrop(renderer, MAP_SCENE));
    REQUIRE(!DrawTransitionBackdrop(renderer, 0));
    SetBackdropTransitionColor(0);
}

int main(void) {
    SetupSurface(false);
    SetupRenderer();
    uintptr_t renderer = (uintptr_t)g_renderer;

    /* The destination starts empty: all center pixels must come from CPU RAM. */
    ResetUi();
    REQUIRE(Compose(renderer));
    CheckFrame(0, 0);

    SetMap(0xF800, 0x001F);
    CaptureMapBackdrop(renderer, MAP_SCENE);
    SetMap(0x07E0, 0xFFFF);
    ResetUi();
    REQUIRE(Compose(renderer));
    CheckFrame(0x2124, 0x0861);
    ResetUi();
    REQUIRE(Compose(renderer));
    CheckFrame(0x2124, 0x0861);
    REQUIRE(Compose(renderer));
    CheckFrame(0x2124, 0x0861); /* Re-uploading cannot re-center/darken pixels. */

    SetMap(0x07E0, 0xFFFF);
    CaptureMapBackdrop(renderer, MAP_SCENE);
    SetupSurface(false);
    ResetUi();
    REQUIRE(Compose(renderer));
    CheckFrame(0x4A49, 0x7BEF);

    /* CPU RGB555 maps still produce RGB565 gray without converting the UI. */
    SetField(RENDERER_PIXEL_FORMAT_OFFSET, 15);
    SetMap(0x7C00, 0x03E0);
    CaptureMapBackdrop(renderer, MAP_SCENE);
    SetupSurface(true);
    ResetUi();
    REQUIRE(Compose(renderer));
    CheckFrame(0x2124, 0x4A49);

    /* Unsupported map format invalidates the cache; valid UI still uploads. */
    SetField(RENDERER_PIXEL_FORMAT_OFFSET, 32);
    CaptureMapBackdrop(renderer, MAP_SCENE);
    ResetUi();
    REQUIRE(Compose(renderer));
    CheckFrame(0, 0);
    SetField(RENDERER_PIXEL_FORMAT_OFFSET, 15);

    SetField(RENDERER_BUFFER_WIDTH_OFFSET, WIDESCREEN_CLIENT_WIDTH - 1);
    CaptureMapBackdrop(renderer, MAP_SCENE);
    SetField(RENDERER_BUFFER_WIDTH_OFFSET, MAP_PITCH);
    ResetUi();
    REQUIRE(Compose(renderer));
    CheckFrame(0, 0);

    SetMap(0x7C00, 0x03E0);
    CaptureMapBackdrop(renderer, MAP_SCENE);
    InvalidateMapBackdrop(MAP_SCENE + 1u);
    ResetUi();
    REQUIRE(Compose(renderer));
    CheckFrame(0x2124, 0x4A49);
    InvalidateMapBackdrop(MAP_SCENE);
    ResetUi();
    REQUIRE(Compose(renderer));
    CheckFrame(0, 0);
    SetMap(0x7C00, 0x03E0);
    CaptureMapBackdrop(renderer, MAP_SCENE + 1u);
    InvalidateMapBackdrop(MAP_SCENE);
    ResetUi();
    REQUIRE(Compose(renderer));
    CheckFrame(0x2124, 0x4A49);

    /* Invalid destination/source bounds return before writing any pixels. */
    SetupSurface(false);
    REQUIRE(!ComposeFixedFrame(renderer, g_back.top, WIDESCREEN_CLIENT_WIDTH * 2 - 1));
    CheckUnwritten();
    REQUIRE(!ComposeFixedFrame(renderer, g_back.top, INT32_MIN));
    CheckUnwritten();
    REQUIRE(!ComposeFixedFrame(renderer, NULL, g_back.pitch));
    SetField(RENDERER_BORDER_Y_OFFSET, MAP_HEIGHT);
    CaptureMapBackdrop(renderer, MAP_SCENE);
    REQUIRE(!Compose(renderer));
    CheckUnwritten();
    SetField(RENDERER_BORDER_Y_OFFSET, MAP_BORDER_Y);
    SetPointer(RENDERER_PIXELS_OFFSET, NULL);
    REQUIRE(!Compose(renderer));
    CheckUnwritten();
    REQUIRE(!Compose(0));

    TestMapTransition(renderer);
    TestFrozenTransition(renderer);
    puts("Castle widescreen backdrop tests passed");
    return 0;
}
