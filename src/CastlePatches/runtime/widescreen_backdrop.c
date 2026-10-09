#include "../castle_addresses.h"
#include "widescreen_backdrop.h"
#include "widescreen_transition.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(uintptr_t) == 4, "Castle renderer requires a 32-bit runtime");

enum {
    SIDE_WIDTH = (WIDESCREEN_CLIENT_WIDTH - GAME_CLIENT_WIDTH) / 2,
    SIDE_PIXELS_PER_ROW = SIDE_WIDTH * 2,
};

/* CPU data survives DirectDraw surface restoration without retaining any of
   the game-owned COM objects. Keep the raw strips for reversible tinting. */
static struct {
    uintptr_t renderer;
    uintptr_t source;
    uintptr_t scene;
    bool valid;
    bool rgb565;
    uint16_t pixels[GAME_CLIENT_HEIGHT][SIDE_PIXELS_PER_ROW];
    uint16_t tinted[GAME_CLIENT_HEIGHT][SIDE_PIXELS_PER_ROW];
    int prepared_color;
} g_backdrop;
static int g_backdrop_color;
static struct {
    uintptr_t renderer;
    uintptr_t source;
    uintptr_t scene;
    bool valid;
    bool rgb565;
    uint16_t pixels[GAME_CLIENT_HEIGHT][WIDESCREEN_CLIENT_WIDTH];
} g_transition_backdrop;
static struct {
    bool valid;
    bool rgb565;
    int color;
    uint16_t pixels[65536];
    uint16_t side_pixels[65536];
} g_tint;

typedef struct {
    uintptr_t source;
    int32_t pitch;
    int32_t x;
    int32_t y;
    bool rgb565;
} MapBuffer;

void InvalidateMapBackdrop(uintptr_t scene) {
    if (g_backdrop.scene == scene) g_backdrop.valid = false;
    if (g_transition_backdrop.scene == scene) g_transition_backdrop.valid = false;
}

void ResetMapBackdrop(void) {
    g_backdrop.valid = false;
    g_transition_backdrop.valid = false;
    g_backdrop_color = 0;
}

void SetBackdropTransitionColor(int color) {
    g_backdrop_color = color < 0 ? 0 : color > CASTLE_TRANSITION_COLOR ? CASTLE_TRANSITION_COLOR : color;
}

static bool GetMapBuffer(uintptr_t renderer, MapBuffer *buffer) {
    if (renderer == 0) return false;

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
        return false;
    }
    if ((size_t)pitch > SIZE_MAX / sizeof(uint16_t) / (size_t)height) return false;
    buffer->source = source;
    buffer->pitch = pitch;
    buffer->x = x;
    buffer->y = border_y;
    buffer->rgb565 = format == 16u;
    return true;
}

void CaptureMapBackdrop(uintptr_t renderer, uintptr_t scene) {
    MapBuffer buffer;
    g_backdrop.valid = false;
    if (!GetMapBuffer(renderer, &buffer)) {
        g_transition_backdrop.valid = false;
        return;
    }

    for (size_t line = 0; line < GAME_CLIENT_HEIGHT; ++line) {
        const uint16_t *row = (const uint16_t *)buffer.source +
            ((size_t)buffer.y + line) * (size_t)buffer.pitch + (size_t)buffer.x;
        memcpy(g_backdrop.pixels[line], row, SIDE_WIDTH * sizeof(uint16_t));
        memcpy(g_backdrop.pixels[line] + SIDE_WIDTH,
               row + WIDESCREEN_CLIENT_WIDTH - SIDE_WIDTH,
               SIDE_WIDTH * sizeof(uint16_t));
    }
    g_backdrop.renderer = renderer;
    g_backdrop.source = buffer.source;
    g_backdrop.scene = scene;
    g_backdrop.rgb565 = buffer.rgb565;
    g_backdrop.prepared_color = -1;
    g_backdrop.valid = true;
    /* Keep the menu's held frame before fade, dialog or cursor pixels. */
    (void)CaptureTransitionBackdrop(renderer, scene);
}

static void PrepareTint(bool rgb565, int color) {
    if (g_tint.valid && g_tint.rgb565 == rgb565 && g_tint.color == color) return;
    for (uint32_t pixel = 0; pixel < 65536u; ++pixel) {
        uint32_t red = (pixel >> (rgb565 ? 11u : 10u)) & 31u;
        uint32_t green = (pixel >> 5u) & (rgb565 ? 63u : 31u);
        uint32_t blue = pixel & 31u;
        red = (red << 3u) | (red >> 2u);
        green = rgb565 ? (green << 2u) | (green >> 4u) : (green << 3u) | (green >> 2u);
        blue = (blue << 3u) | (blue >> 2u);
        /* Weighted luminance, followed by a 50% black mask. */
        uint32_t gray = ((77u * red + 150u * green + 29u * blue + 128u) >> 8u) / 2u;
        uint32_t weight = (uint32_t)color;
        uint32_t inverse = CASTLE_TRANSITION_COLOR - weight;
        red = (red * weight + gray * inverse + 7u) / CASTLE_TRANSITION_COLOR;
        green = (green * weight + gray * inverse + 7u) / CASTLE_TRANSITION_COLOR;
        blue = (blue * weight + gray * inverse + 7u) / CASTLE_TRANSITION_COLOR;
        g_tint.side_pixels[pixel] =
            (uint16_t)(((red >> 3u) << 11u) | ((green >> 2u) << 5u) | (blue >> 3u));
        g_tint.pixels[pixel] = rgb565 ? g_tint.side_pixels[pixel] :
            (uint16_t)(((red >> 3u) << 10u) | ((green >> 3u) << 5u) | (blue >> 3u));
    }
    g_tint.rgb565 = rgb565;
    g_tint.color = color;
    g_tint.valid = true;
}

static void PrepareTintedBackdrop(void) {
    if (g_backdrop.prepared_color == g_backdrop_color) return;
    PrepareTint(g_backdrop.rgb565, g_backdrop_color);
    for (size_t y = 0; y < GAME_CLIENT_HEIGHT; ++y) {
        for (size_t x = 0; x < SIDE_PIXELS_PER_ROW; ++x) {
            g_backdrop.tinted[y][x] = g_tint.side_pixels[g_backdrop.pixels[y][x]];
        }
    }
    g_backdrop.prepared_color = g_backdrop_color;
}

bool CaptureTransitionBackdrop(uintptr_t renderer, uintptr_t scene) {
    MapBuffer buffer;
    g_transition_backdrop.valid = false;
    if (!GetMapBuffer(renderer, &buffer)) return false;
    for (size_t y = 0; y < GAME_CLIENT_HEIGHT; ++y) {
        const uint16_t *row = (const uint16_t *)buffer.source +
            ((size_t)buffer.y + y) * (size_t)buffer.pitch + (size_t)buffer.x;
        memcpy(g_transition_backdrop.pixels[y], row, WIDESCREEN_CLIENT_WIDTH * sizeof(uint16_t));
    }
    g_transition_backdrop.renderer = renderer;
    g_transition_backdrop.source = buffer.source;
    g_transition_backdrop.scene = scene;
    g_transition_backdrop.rgb565 = buffer.rgb565;
    g_transition_backdrop.valid = true;
    return true;
}

bool HasTransitionBackdrop(uintptr_t renderer, uintptr_t scene) {
    MapBuffer buffer;
    return g_transition_backdrop.valid && g_transition_backdrop.renderer == renderer &&
        g_transition_backdrop.scene == scene && GetMapBuffer(renderer, &buffer) &&
        g_transition_backdrop.source == buffer.source && g_transition_backdrop.rgb565 == buffer.rgb565;
}

bool ApplyMapTransition(uintptr_t renderer, int color) {
    MapBuffer buffer;
    if (color < 0 || color > CASTLE_TRANSITION_COLOR || !GetMapBuffer(renderer, &buffer)) return false;
    SetBackdropTransitionColor(color);
    if (color == CASTLE_TRANSITION_COLOR) return true;
    PrepareTint(buffer.rgb565, color);
    for (size_t y = 0; y < GAME_CLIENT_HEIGHT; ++y) {
        uint16_t *row = (uint16_t *)buffer.source +
            ((size_t)buffer.y + y) * (size_t)buffer.pitch + (size_t)buffer.x;
        for (size_t x = 0; x < WIDESCREEN_CLIENT_WIDTH; ++x) row[x] = g_tint.pixels[row[x]];
    }
    return true;
}

bool DrawTransitionBackdrop(uintptr_t renderer, int color) {
    MapBuffer buffer;
    if (color < 0 || color > CASTLE_TRANSITION_COLOR ||
        !HasTransitionBackdrop(renderer, g_transition_backdrop.scene) || !GetMapBuffer(renderer, &buffer)) {
        return false;
    }
    SetBackdropTransitionColor(color);
    PrepareTint(buffer.rgb565, color);
    for (size_t y = 0; y < GAME_CLIENT_HEIGHT; ++y) {
        uint16_t *row = (uint16_t *)buffer.source +
            ((size_t)buffer.y + y) * (size_t)buffer.pitch + (size_t)buffer.x;
        for (size_t x = 0; x < GAME_CLIENT_WIDTH; ++x) {
            row[x] = g_tint.pixels[g_transition_backdrop.pixels[y][SIDE_WIDTH + x]];
        }
    }
    return true;
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
    if (use_map) PrepareTintedBackdrop();

    /* Upload directly from the normal CPU buffer. Never read pixels back from
       the DirectDraw destination or lock it a second time after game upload. */
    for (size_t y = 0; y < GAME_CLIENT_HEIGHT; ++y) {
        const uint16_t *source_row = (const uint16_t *)source +
            ((size_t)border_y + y) * (size_t)width + (size_t)x;
        uint16_t *pixels = (uint16_t *)((uint8_t *)destination + (ptrdiff_t)y * destination_pitch);
        memcpy(pixels + SIDE_WIDTH, source_row, GAME_CLIENT_WIDTH * sizeof(uint16_t));
        if (use_map) {
            memcpy(pixels, g_backdrop.tinted[y], SIDE_WIDTH * sizeof(uint16_t));
            memcpy(pixels + SIDE_WIDTH + GAME_CLIENT_WIDTH, g_backdrop.tinted[y] + SIDE_WIDTH,
                   SIDE_WIDTH * sizeof(uint16_t));
        } else {
            memset(pixels, 0, SIDE_WIDTH * sizeof(uint16_t));
            memset(pixels + SIDE_WIDTH + GAME_CLIENT_WIDTH, 0, SIDE_WIDTH * sizeof(uint16_t));
        }
    }
    return true;
}
