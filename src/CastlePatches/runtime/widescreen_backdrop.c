#include "../castle_addresses.h"
#include "widescreen_backdrop.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(uintptr_t) == 4, "Castle renderer requires a 32-bit runtime");

enum {
    SIDE_WIDTH = (WIDESCREEN_CLIENT_WIDTH - GAME_CLIENT_WIDTH) / 2,
    SIDE_PIXELS_PER_ROW = SIDE_WIDTH * 2,
};

/* CPU data survives DirectDraw surface restoration without retaining any of
   the game-owned COM objects. Only the two visible strips are cached. */
static struct {
    uintptr_t renderer;
    uintptr_t source;
    uintptr_t scene;
    bool valid;
    bool grayscale;
    bool rgb565;
    uint16_t pixels[GAME_CLIENT_HEIGHT][SIDE_PIXELS_PER_ROW];
} g_backdrop;

void InvalidateMapBackdrop(uintptr_t scene) {
    if (g_backdrop.scene == scene) g_backdrop.valid = false;
}

void CaptureMapBackdrop(uintptr_t renderer, uintptr_t scene) {
    g_backdrop.valid = false;
    if (renderer == 0) return;

    uintptr_t mode = *(const uintptr_t *)(renderer + RENDERER_MODE_OFFSET);
    uintptr_t source = *(const uintptr_t *)(renderer + RENDERER_PIXELS_OFFSET);
    int32_t pitch = *(const int32_t *)(renderer + RENDERER_BUFFER_WIDTH_OFFSET);
    int32_t height = *(const int32_t *)(renderer + RENDERER_BUFFER_HEIGHT_OFFSET);
    int32_t x = *(const int32_t *)(renderer + RENDERER_BORDER_X_OFFSET);
    int32_t border_y = *(const int32_t *)(renderer + RENDERER_BORDER_Y_OFFSET);
    uint32_t format = *(const uint32_t *)(renderer + RENDERER_PIXEL_FORMAT_OFFSET);
    if (mode == 0 || source == 0 || x < 0 || border_y < 0 ||
        pitch < WIDESCREEN_CLIENT_WIDTH || height < GAME_CLIENT_HEIGHT ||
        x > pitch - WIDESCREEN_CLIENT_WIDTH || border_y > height - GAME_CLIENT_HEIGHT ||
        *(const uint32_t *)(renderer + RENDERER_PIXEL_BYTES_OFFSET) != sizeof(uint16_t) ||
        *(const uint32_t *)(mode + 0x04u) != WIDESCREEN_CLIENT_WIDTH ||
        *(const uint32_t *)(mode + 0x08u) != GAME_CLIENT_HEIGHT ||
        (format != 15u && format != 16u)) {
        return;
    }
    if ((size_t)pitch > SIZE_MAX / sizeof(uint16_t) / (size_t)height) return;

    for (size_t line = 0; line < GAME_CLIENT_HEIGHT; ++line) {
        const uint16_t *row = (const uint16_t *)source +
            ((size_t)border_y + line) * (size_t)pitch + (size_t)x;
        memcpy(g_backdrop.pixels[line], row, SIDE_WIDTH * sizeof(uint16_t));
        memcpy(g_backdrop.pixels[line] + SIDE_WIDTH,
               row + WIDESCREEN_CLIENT_WIDTH - SIDE_WIDTH,
               SIDE_WIDTH * sizeof(uint16_t));
    }
    g_backdrop.renderer = renderer;
    g_backdrop.source = source;
    g_backdrop.scene = scene;
    g_backdrop.rgb565 = format == 16u;
    g_backdrop.grayscale = false;
    g_backdrop.valid = true;
}

static void PrepareGrayscaleBackdrop(void) {
    if (g_backdrop.grayscale) return;
    for (size_t y = 0; y < GAME_CLIENT_HEIGHT; ++y) {
        for (size_t x = 0; x < SIDE_PIXELS_PER_ROW; ++x) {
            uint16_t pixel = g_backdrop.pixels[y][x];
            uint32_t red = (pixel >> (g_backdrop.rgb565 ? 11u : 10u)) & 31u;
            uint32_t green = (pixel >> 5u) & (g_backdrop.rgb565 ? 63u : 31u);
            uint32_t blue = pixel & 31u;
            red = (red << 3u) | (red >> 2u);
            green = g_backdrop.rgb565 ? (green << 2u) | (green >> 4u)
                                      : (green << 3u) | (green >> 2u);
            blue = (blue << 3u) | (blue >> 2u);
            /* Weighted luminance, followed by a 50% black mask. */
            uint32_t gray = ((77u * red + 150u * green + 29u * blue + 128u) >> 8u) / 2u;
            g_backdrop.pixels[y][x] =
                (uint16_t)(((gray >> 3u) << 11u) | ((gray >> 2u) << 5u) | (gray >> 3u));
        }
    }
    g_backdrop.grayscale = true;
}

bool ComposeFixedFrame(uintptr_t renderer, void *destination, int32_t destination_pitch) {
    if (renderer == 0 || destination == NULL) return false;
    int64_t pitch_bytes = destination_pitch;
    if (pitch_bytes < 0) pitch_bytes = -pitch_bytes;
    if (pitch_bytes < WIDESCREEN_CLIENT_WIDTH * (int64_t)sizeof(uint16_t) ||
        pitch_bytes > PTRDIFF_MAX / (GAME_CLIENT_HEIGHT - 1)) return false;

    uintptr_t mode = *(const uintptr_t *)(renderer + RENDERER_MODE_OFFSET);
    uintptr_t source = *(const uintptr_t *)(renderer + RENDERER_PIXELS_OFFSET);
    int32_t width = *(const int32_t *)(renderer + RENDERER_BUFFER_WIDTH_OFFSET);
    int32_t height = *(const int32_t *)(renderer + RENDERER_BUFFER_HEIGHT_OFFSET);
    int32_t x = *(const int32_t *)(renderer + RENDERER_BORDER_X_OFFSET);
    int32_t border_y = *(const int32_t *)(renderer + RENDERER_BORDER_Y_OFFSET);
    if (mode == 0 || source == 0 || x < 0 || border_y < 0 ||
        width < GAME_CLIENT_WIDTH || height < GAME_CLIENT_HEIGHT ||
        x > width - GAME_CLIENT_WIDTH || border_y > height - GAME_CLIENT_HEIGHT ||
        *(const uint32_t *)(renderer + RENDERER_PIXEL_BYTES_OFFSET) != sizeof(uint16_t) ||
        *(const uint32_t *)(mode + 0x04u) != WIDESCREEN_CLIENT_WIDTH ||
        *(const uint32_t *)(mode + 0x08u) != GAME_CLIENT_HEIGHT ||
        (size_t)width > SIZE_MAX / sizeof(uint16_t) / (size_t)height) {
        return false;
    }

    bool use_map = g_backdrop.valid && g_backdrop.renderer == renderer &&
        g_backdrop.source == source;
    if (use_map) PrepareGrayscaleBackdrop();

    /* Upload directly from the normal CPU buffer. Never read pixels back from
       the DirectDraw destination or lock it a second time after game upload. */
    for (size_t y = 0; y < GAME_CLIENT_HEIGHT; ++y) {
        const uint16_t *source_row = (const uint16_t *)source +
            ((size_t)border_y + y) * (size_t)width + (size_t)x;
        uint16_t *pixels = (uint16_t *)((uint8_t *)destination + (ptrdiff_t)y * destination_pitch);
        memcpy(pixels + SIDE_WIDTH, source_row, GAME_CLIENT_WIDTH * sizeof(uint16_t));
        if (use_map) {
            memcpy(pixels, g_backdrop.pixels[y], SIDE_WIDTH * sizeof(uint16_t));
            memcpy(pixels + SIDE_WIDTH + GAME_CLIENT_WIDTH, g_backdrop.pixels[y] + SIDE_WIDTH,
                   SIDE_WIDTH * sizeof(uint16_t));
        } else {
            memset(pixels, 0, SIDE_WIDTH * sizeof(uint16_t));
            memset(pixels + SIDE_WIDTH + GAME_CLIENT_WIDTH, 0, SIDE_WIDTH * sizeof(uint16_t));
        }
    }
    return true;
}
