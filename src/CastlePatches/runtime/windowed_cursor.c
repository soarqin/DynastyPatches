#include "windowed_cursor.h"
#include "../castle_addresses.h"

#include <string.h>

enum {
    CURSOR_RADIUS = 48,
    CURSOR_DIAMETER = 2 * CURSOR_RADIUS,
    RENDERER_MAX_PIXELS = (WIDESCREEN_CLIENT_WIDTH + 128) * (GAME_CLIENT_HEIGHT + 96),
    CURSOR_MAX_SCALE = 3,
    CURSOR_MAX_WIDTH = CURSOR_DIAMETER * CURSOR_MAX_SCALE,
};

static uint16_t g_black_buffer[RENDERER_MAX_PIXELS];
static uint16_t g_white_buffer[RENDERER_MAX_PIXELS];
static struct {
    int32_t stride;
    size_t offset;
    uint16_t white;
    bool rgb565;
} g_capture;
static uint32_t g_candidate[CURSOR_DIAMETER][CURSOR_DIAMETER];
static uint32_t g_pending[CURSOR_DIAMETER][CURSOR_DIAMETER];
static uint32_t g_displayed[CURSOR_DIAMETER][CURSOR_DIAMETER];
static RECT g_pending_bounds;
static bool g_has_pending;
static HCURSOR g_cursor;

bool BeginWindowedCursorFrame(uintptr_t renderer, LONG x, LONG y) {
    if (renderer == 0) return false;
    int32_t stride = *(const int32_t *)(renderer + RENDERER_BUFFER_WIDTH_OFFSET);
    int32_t height = *(const int32_t *)(renderer + RENDERER_BUFFER_HEIGHT_OFFSET);
    LONG left = x + *(const LONG *)(renderer + RENDERER_BORDER_X_OFFSET) - CURSOR_RADIUS;
    LONG top = y + *(const LONG *)(renderer + RENDERER_BORDER_Y_OFFSET) - CURSOR_RADIUS;
    uint32_t format = *(const uint32_t *)(renderer + RENDERER_PIXEL_FORMAT_OFFSET);
    if (stride < CURSOR_DIAMETER || height < CURSOR_DIAMETER ||
        (size_t)stride > RENDERER_MAX_PIXELS / (size_t)height ||
        left < 0 || left > stride - CURSOR_DIAMETER || top < 0 || top > height - CURSOR_DIAMETER ||
        *(const uint32_t *)(renderer + RENDERER_PIXEL_BYTES_OFFSET) != sizeof(uint16_t) ||
        (format != 15u && format != 16u)) {
        return false;
    }

    g_capture.stride = stride;
    g_capture.offset = (size_t)top * (size_t)stride + (size_t)left;
    g_capture.rgb565 = format == 16u;
    g_capture.white = g_capture.rgb565 ? 0xFFFFu : 0x7FFFu;
    for (size_t row_index = 0; row_index < CURSOR_DIAMETER; ++row_index) {
        size_t row = g_capture.offset + row_index * (size_t)stride;
        memset(&g_black_buffer[row], 0, CURSOR_DIAMETER * sizeof(uint16_t));
        for (size_t column = 0; column < CURSOR_DIAMETER; ++column) {
            g_white_buffer[row + column] = g_capture.white;
        }
    }
    return true;
}

uint16_t *WindowedCursorPixels(bool white) {
    return white ? g_white_buffer : g_black_buffer;
}

static uint32_t SpriteColor(uint16_t pixel) {
    uint32_t red = (pixel >> (g_capture.rgb565 ? 11u : 10u)) & 31u;
    uint32_t green = (pixel >> 5u) & (g_capture.rgb565 ? 63u : 31u);
    uint32_t blue = pixel & 31u;
    red = (red << 3u) | (red >> 2u);
    green = g_capture.rgb565 ? (green << 2u) | (green >> 4u) : (green << 3u) | (green >> 2u);
    blue = (blue << 3u) | (blue >> 2u);
    return (red << 16u) | (green << 8u) | blue;
}

static uint32_t CursorPixel(uint16_t on_black, uint16_t on_white) {
    if (on_black == 0 && on_white == g_capture.white) return 0;
    uint32_t color = SpriteColor(on_black);
    if (on_black == on_white) return 0xFF000000u | color;

    /* The black pass gives premultiplied RGB. The white pass reveals opacity. */
    uint32_t white = SpriteColor(on_white);
    int red = (int)((color >> 16u) & 0xFFu);
    int green = (int)((color >> 8u) & 0xFFu);
    int blue = (int)(color & 0xFFu);
    int delta = (int)((white >> 16u) & 0xFFu) + (int)((white >> 8u) & 0xFFu) + (int)(white & 0xFFu);
    int alpha = 255 - (delta - red - green - blue) / 3;
    if (alpha > 255) alpha = 255;
    if (alpha < red) alpha = red;
    if (alpha < green) alpha = green;
    if (alpha < blue) alpha = blue;
    return ((uint32_t)alpha << 24u) | color;
}

void EndWindowedCursorFrame(void) {
    RECT bounds = {CURSOR_RADIUS, CURSOR_RADIUS, CURSOR_RADIUS + 1, CURSOR_RADIUS + 1};
    bool visible = false;
    for (LONG y = 0; y < CURSOR_DIAMETER; ++y) {
        size_t row = g_capture.offset + (size_t)y * (size_t)g_capture.stride;
        for (LONG x = 0; x < CURSOR_DIAMETER; ++x) {
            uint32_t pixel = CursorPixel(g_black_buffer[row + x], g_white_buffer[row + x]);
            g_candidate[y][x] = pixel;
            if (pixel == 0) continue;
            visible = true;
            if (bounds.left > x) bounds.left = x;
            if (bounds.top > y) bounds.top = y;
            if (bounds.right <= x) bounds.right = x + 1;
            if (bounds.bottom <= y) bounds.bottom = y + 1;
        }
    }
    if (visible) {
        memcpy(g_pending, g_candidate, sizeof(g_pending));
        g_pending_bounds = bounds;
        g_has_pending = true;
    }
}

static HCURSOR CreateSpriteCursor(int scale4) {
    const RECT *bounds = &g_pending_bounds;
    int width = ((bounds->right - bounds->left) * scale4 + 2) / 4;
    int height = ((bounds->bottom - bounds->top) * scale4 + 2) / 4;
    BITMAPINFO bitmap = {
        .bmiHeader = {
            .biSize = sizeof(BITMAPINFOHEADER),
            .biWidth = width,
            .biHeight = -height,
            .biPlanes = 1,
            .biBitCount = 32,
            .biCompression = BI_RGB,
        },
    };
    void *pixels = NULL;
    HBITMAP color = CreateDIBSection(NULL, &bitmap, DIB_RGB_COLORS, &pixels, NULL, 0);
    if (color == NULL) return NULL;

    uint8_t mask_bits[((CURSOR_MAX_WIDTH + 15) / 16) * 2 * CURSOR_MAX_WIDTH];
    int mask_stride = ((width + 15) / 16) * 2;
    memset(mask_bits, 0, (size_t)mask_stride * (size_t)height);
    for (int y = 0; y < height; ++y) {
        int source_y = bounds->top + y * 4 / scale4;
        for (int x = 0; x < width; ++x) {
            int source_x = bounds->left + x * 4 / scale4;
            uint32_t pixel = g_pending[source_y][source_x];
            ((uint32_t *)pixels)[(size_t)y * (size_t)width + (size_t)x] = pixel;
            if (pixel == 0) mask_bits[y * mask_stride + x / 8] |= (uint8_t)(0x80u >> (x % 8));
        }
    }
    HBITMAP mask = CreateBitmap(width, height, 1, 1, mask_bits);
    HCURSOR cursor = NULL;
    if (mask != NULL) {
        ICONINFO icon = {
            .xHotspot = (DWORD)((CURSOR_RADIUS - bounds->left) * scale4 / 4),
            .yHotspot = (DWORD)((CURSOR_RADIUS - bounds->top) * scale4 / 4),
            .hbmMask = mask,
            .hbmColor = color,
        };
        cursor = (HCURSOR)CreateIconIndirect(&icon);
        DeleteObject(mask);
    }
    DeleteObject(color);
    return cursor;
}

static bool IsOverGameClient(HWND window) {
    if (window == NULL || GetForegroundWindow() != window) return false;
    CURSORINFO cursor = {sizeof(cursor)};
    RECT client;
    return GetCursorInfo(&cursor) && WindowFromPoint(cursor.ptScreenPos) == window &&
           ScreenToClient(window, &cursor.ptScreenPos) && GetClientRect(window, &client) &&
           PtInRect(&client, cursor.ptScreenPos);
}

void PresentWindowedCursor(HWND window, uint32_t scale4) {
    if (!g_has_pending) return;
    g_has_pending = false;
    if (scale4 < 4u || scale4 > 4u * CURSOR_MAX_SCALE ||
        (g_cursor != NULL && memcmp(g_displayed, g_pending, sizeof(g_displayed)) == 0)) {
        return;
    }

    HCURSOR cursor = CreateSpriteCursor((int)scale4);
    if (cursor == NULL) return;
    HCURSOR previous = g_cursor;
    g_cursor = cursor;
    memcpy(g_displayed, g_pending, sizeof(g_displayed));
    if (IsOverGameClient(window)) {
        SetCursor(cursor);
    } else if (previous != NULL && GetCursor() == previous) {
        SetCursor(LoadCursorW(NULL, IDC_ARROW));
    }
    if (previous != NULL) DestroyCursor(previous);
}

HCURSOR GetWindowedCursor(void) {
    return g_cursor;
}

void ReleaseWindowedCursor(void) {
    if (g_cursor != NULL) {
        if (GetCursor() == g_cursor) SetCursor(LoadCursorW(NULL, IDC_ARROW));
        DestroyCursor(g_cursor);
        g_cursor = NULL;
    }
    g_has_pending = false;
}
