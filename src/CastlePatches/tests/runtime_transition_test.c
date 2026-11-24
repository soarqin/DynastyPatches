/* Exercise the actual runtime hooks against synthetic game objects and pixels.
   No original executable, entry point or game function is loaded or called. */
#include <windows.h>
#include "../castle_addresses.h"

static struct {
    uintptr_t renderer, scene, state, system_menu, ordinary_menu, command;
    int32_t effect_state;
    int16_t brightness, fade_in, fade_out;
    uint16_t color;
} g_test_globals;

/* Only rebind globals; the runtime hook bodies and x86 object fields are real. */
#undef RENDERER_GLOBAL_ADDRESS
#undef CURRENT_MAP_SCENE_ADDRESS
#undef GAME_STATE_OBJECT_ADDRESS
#undef SYSTEM_MENU_OBJECT_ADDRESS
#undef ORDINARY_MENU_OBJECT_ADDRESS
#undef CURRENT_SCRIPT_COMMAND_ADDRESS
#undef MAP_EFFECT_STATE_ADDRESS
#undef MAP_EFFECT_BRIGHTNESS_ADDRESS
#undef MAP_EFFECT_FADE_IN_TARGET_ADDRESS
#undef MAP_EFFECT_FADE_OUT_TARGET_ADDRESS
#undef MAP_EFFECT_COLOR_ADDRESS
#define RENDERER_GLOBAL_ADDRESS ((uintptr_t)&g_test_globals.renderer)
#define CURRENT_MAP_SCENE_ADDRESS ((uintptr_t)&g_test_globals.scene)
#define GAME_STATE_OBJECT_ADDRESS ((uintptr_t)&g_test_globals.state)
#define SYSTEM_MENU_OBJECT_ADDRESS ((uintptr_t)&g_test_globals.system_menu)
#define ORDINARY_MENU_OBJECT_ADDRESS ((uintptr_t)&g_test_globals.ordinary_menu)
#define CURRENT_SCRIPT_COMMAND_ADDRESS ((uintptr_t)&g_test_globals.command)
#define MAP_EFFECT_STATE_ADDRESS ((uintptr_t)&g_test_globals.effect_state)
#define MAP_EFFECT_BRIGHTNESS_ADDRESS ((uintptr_t)&g_test_globals.brightness)
#define MAP_EFFECT_FADE_IN_TARGET_ADDRESS ((uintptr_t)&g_test_globals.fade_in)
#define MAP_EFFECT_FADE_OUT_TARGET_ADDRESS ((uintptr_t)&g_test_globals.fade_out)
#define MAP_EFFECT_COLOR_ADDRESS ((uintptr_t)&g_test_globals.color)

static DWORD g_test_tick;
static DWORD WINAPI TestTickCount(void) { return g_test_tick; }
#define GetTickCount TestTickCount
#define DllMain CastleTestDllMain
#include "../runtime/runtime_dll.c"
#undef DllMain
#undef GetTickCount

#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "line %d: %s\n", __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

enum { BORDER_X = 64, BORDER_Y = 48, PITCH = 992, HEIGHT = 576, SIDE = 112 };
static uint8_t g_test_renderer[0x78];
static uint32_t g_test_mode[4];
static uint8_t g_test_scene[0x284];
static uint8_t g_test_state[0x180];
static uint8_t g_test_menu[0x660];
static uint8_t g_test_mask[0x588];
static uint8_t g_test_child[0x580];
static uint8_t g_test_sprites[3][0x364];
static uint16_t g_test_map[HEIGHT][PITCH];
static uint16_t g_test_original[HEIGHT][PITCH];
static uint16_t g_test_output[480][864];
static int g_test_draws[3], g_test_pixels[3], g_test_frames[3];
static int g_test_effect_calls, g_test_resumes, g_test_releases, g_test_loads;

static void SetTestPointer(void *base, size_t offset, const void *value) {
    *(uintptr_t *)((uint8_t *)base + offset) = (uintptr_t)value;
}

static uint16_t GrayPixel(uint16_t pixel, bool rgb565) {
    uint32_t r = (pixel >> (rgb565 ? 11 : 10)) & 31u;
    uint32_t g = (pixel >> 5) & (rgb565 ? 63u : 31u);
    uint32_t b = pixel & 31u;
    r = (r << 3) | (r >> 2);
    g = rgb565 ? (g << 2) | (g >> 4) : (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    uint32_t gray = ((77u * r + 150u * g + 29u * b + 128u) >> 8) / 2;
    return (uint16_t)(((gray >> 3) << 11) | ((gray >> 2) << 5) | (gray >> 3));
}

static void ClearVisibleMap(uint16_t pixel) {
    for (size_t y = 0; y < GAME_CLIENT_HEIGHT; ++y) {
        for (size_t x = 0; x < WIDESCREEN_CLIENT_WIDTH; ++x) {
            g_test_map[BORDER_Y + y][BORDER_X + x] = pixel;
        }
    }
}

static int __cdecl TestOriginalEffect(void) {
    ++g_test_effect_calls;
    int state = *(const int32_t *)MAP_EFFECT_STATE_ADDRESS;
    if (state == 7) ClearVisibleMap(0);
    return 17;
}

static uint8_t __cdecl TestFadeIn(int16_t step, int16_t delay) {
    (void)step; (void)delay;
    *(int32_t *)MAP_EFFECT_STATE_ADDRESS = 2;
    return 0;
}

static int __fastcall TestResume(void *scene) {
    ++g_test_resumes;
    ((uint8_t *)scene)[MAP_SCENE_RENDER_SUPPRESSED_OFFSET] = 0;
    return 3;
}

static int __fastcall TestSuspend(void *scene) {
    ((uint8_t *)scene)[MAP_SCENE_RENDER_SUPPRESSED_OFFSET] = 1;
    return 4;
}

static void __fastcall TestRelease(void *menu) {
    (void)menu;
    ++g_test_releases;
    *(uintptr_t *)ORDINARY_MENU_OBJECT_ADDRESS = 0;
    /* Reproduce the empty draw between UI destruction and the next map tick. */
    ClearVisibleMap(0);
}

static void __fastcall TestSystemRelease(void *menu) {
    (void)menu;
    ++g_test_releases;
    *(uintptr_t *)SYSTEM_MENU_OBJECT_ADDRESS = 0;
    ClearVisibleMap(0);
}

static int __fastcall TestLoad(void *scene, void *unused_edx, const char *path) {
    (void)scene; (void)unused_edx; (void)path;
    ++g_test_loads;
    return 5;
}

static int TestSpriteIndex(void *sprite) {
    for (int i = 0; i < 3; ++i) if (sprite == g_test_sprites[i]) return i;
    REQUIRE(false);
    return 0;
}

static uint16_t MenuBackgroundPixel(size_t x, size_t y) {
    return (uint16_t)((0x0315u + x * 7u + y * 11u) & 0x7FFFu);
}

static int __fastcall TestSpriteBlit(void *sprite, void *unused_edx, uintptr_t destination,
                                     int32_t x, int32_t y, int32_t mode,
                                     int32_t a6, int32_t a7, int32_t a8) {
    (void)unused_edx; (void)destination; (void)x; (void)y; (void)mode;
    (void)a6; (void)a7; (void)a8;
    int index = TestSpriteIndex(sprite);
    ++g_test_pixels[index];
    if (index == 0) {
        for (size_t row = 0; row < GAME_CLIENT_HEIGHT; ++row) {
            for (size_t column = 0; column < GAME_CLIENT_WIDTH; ++column) {
                g_test_map[BORDER_Y + row][BORDER_X + column] = MenuBackgroundPixel(column, row);
            }
        }
    } else if (index == 1) {
        for (size_t row = 0; row < GAME_CLIENT_HEIGHT; ++row) {
            memset(&g_test_map[BORDER_Y + row][BORDER_X], 0, GAME_CLIENT_WIDTH * sizeof(uint16_t));
        }
    } else {
        g_test_map[BORDER_Y + 24][BORDER_X + 24] = 0xFFFF;
    }
    return 1;
}

static uint8_t __fastcall TestUiDraw(void *object) {
    uintptr_t sprite = *(const uintptr_t *)((uint8_t *)object + UI_OBJECT_SPRITE_OFFSET);
    int index = TestSpriteIndex((void *)sprite);
    ++g_test_draws[index];
    /* Model the native call order: prepare frame, draw pixels, then advance. */
    (void)RuntimeUiSpriteBlit((void *)sprite, NULL, (uintptr_t)g_test_renderer, 0, 0, 1, 0, 0, 0);
    ++g_test_frames[index];
    return (uint8_t)(index + 1);
}

static void Setup(bool rgb565) {
    CancelFixedTransition(&g_transition);
    ResetMapBackdrop();
    memset(&g_frame_layout, 0, sizeof(g_frame_layout));
    memset(g_test_renderer, 0, sizeof(g_test_renderer));
    memset(g_test_scene, 0, sizeof(g_test_scene));
    memset(g_test_state, 0, sizeof(g_test_state));
    memset(g_test_menu, 0, sizeof(g_test_menu));
    memset(g_test_mask, 0, sizeof(g_test_mask));
    memset(g_test_child, 0, sizeof(g_test_child));
    memset(g_test_draws, 0, sizeof(g_test_draws));
    memset(g_test_pixels, 0, sizeof(g_test_pixels));
    memset(g_test_frames, 0, sizeof(g_test_frames));
    g_test_effect_calls = g_test_resumes = g_test_releases = g_test_loads = 0;
    g_test_tick = 1000;
    g_map_frame = g_map_frame_suppressed = g_frame_upload_prepared = 0;
    g_map_active = g_cursor_map_active = g_bink_frame = 0;
    g_suppressed_ui_sprite = 0;
    g_factory_opcode = UINT32_MAX;
    g_test_mode[1] = WIDESCREEN_CLIENT_WIDTH;
    g_test_mode[2] = GAME_CLIENT_HEIGHT;
    SetTestPointer(g_test_renderer, RENDERER_MODE_OFFSET, g_test_mode);
    SetTestPointer(g_test_renderer, RENDERER_PIXELS_OFFSET, g_test_map);
    *(int32_t *)(g_test_renderer + RENDERER_BUFFER_WIDTH_OFFSET) = PITCH;
    *(int32_t *)(g_test_renderer + RENDERER_BUFFER_HEIGHT_OFFSET) = HEIGHT;
    *(int32_t *)(g_test_renderer + RENDERER_BORDER_X_OFFSET) = BORDER_X;
    *(int32_t *)(g_test_renderer + RENDERER_BORDER_Y_OFFSET) = BORDER_Y;
    *(uint32_t *)(g_test_renderer + RENDERER_PIXEL_BYTES_OFFSET) = 2;
    *(uint32_t *)(g_test_renderer + RENDERER_PIXEL_FORMAT_OFFSET) = rgb565 ? 16 : 15;
    *(uintptr_t *)RENDERER_GLOBAL_ADDRESS = (uintptr_t)g_test_renderer;
    *(uintptr_t *)CURRENT_MAP_SCENE_ADDRESS = (uintptr_t)g_test_scene;
    *(uintptr_t *)GAME_STATE_OBJECT_ADDRESS = (uintptr_t)g_test_state;
    *(uintptr_t *)SYSTEM_MENU_OBJECT_ADDRESS = 0;
    *(uintptr_t *)ORDINARY_MENU_OBJECT_ADDRESS = 0;
    *(uintptr_t *)CURRENT_SCRIPT_COMMAND_ADDRESS = 0;
    *(int32_t *)MAP_EFFECT_STATE_ADDRESS = 1;
    *(int16_t *)MAP_EFFECT_BRIGHTNESS_ADDRESS = 15;
    *(int16_t *)MAP_EFFECT_FADE_IN_TARGET_ADDRESS = 15;
    *(int16_t *)MAP_EFFECT_FADE_OUT_TARGET_ADDRESS = 0;
    *(uint16_t *)MAP_EFFECT_COLOR_ADDRESS = 1;
    uint8_t *objects[] = {g_test_menu, g_test_mask, g_test_child};
    for (size_t i = 0; i < _countof(objects); ++i) {
        SetTestPointer(objects[i], UI_OBJECT_SPRITE_OFFSET, g_test_sprites[i]);
        objects[i][UI_OBJECT_ENABLED_OFFSET] = objects[i][UI_OBJECT_VISIBLE_OFFSET] = 1;
    }
    SetTestPointer(g_test_menu, SYSTEM_MENU_RETURN_MASK_OFFSET, g_test_mask);
    for (size_t y = 0; y < HEIGHT; ++y) {
        for (size_t x = 0; x < PITCH; ++x) {
            g_test_map[y][x] = (uint16_t)(0x2108u | ((x * 37u + y * 101u) & (rgb565 ? 0xFFFFu : 0x7FFFu)));
        }
    }
    memcpy(g_test_original, g_test_map, sizeof(g_test_map));
    CaptureMapBackdrop((uintptr_t)g_test_renderer, (uintptr_t)g_test_scene);
    g_original_map_effect = TestOriginalEffect;
    g_original_fade_in = TestFadeIn;
    g_original_map_resume = TestResume;
    g_original_map_suspend = TestSuspend;
    g_original_menu_release = TestRelease;
    g_original_system_menu_release = TestSystemRelease;
    g_original_map_load = TestLoad;
    g_original_ui_object_draw = TestUiDraw;
    g_original_ui_sprite_blit = TestSpriteBlit;
}

static void BeginGrayMenu(uint32_t target) {
    BeginFixedTransition(&g_transition, (uintptr_t)g_test_scene, target);
    REQUIRE(MapTransitionColor(&g_transition, (uintptr_t)g_test_scene, 3, 0, g_test_tick) == 0);
    REQUIRE(g_transition.phase == CASTLE_TRANSITION_FIXED);
    *(int32_t *)MAP_EFFECT_STATE_ADDRESS = target == 0x36u ? 1 : 7;
    *(int16_t *)MAP_EFFECT_BRIGHTNESS_ADDRESS = target == 0x36u ? 15 : 0;
}

static void CheckGrayFrame(bool rgb565, bool child_pixel) {
    REQUIRE(ComposeFixedFrame((uintptr_t)g_test_renderer, g_test_output, sizeof(g_test_output[0])));
    for (size_t y = 0; y < GAME_CLIENT_HEIGHT; ++y) {
        for (size_t x = 0; x < WIDESCREEN_CLIENT_WIDTH; ++x) {
            uint16_t expected = GrayPixel(g_test_original[BORDER_Y + y][BORDER_X + x], rgb565);
            if (child_pixel && y == 24 && x == SIDE + 24) expected = 0xFFFF;
            uint16_t actual = g_test_output[y][x];
            if (!rgb565 && x >= SIDE && x < SIDE + GAME_CLIENT_WIDTH &&
                !(child_pixel && y == 24 && x == SIDE + 24)) {
                actual = (uint16_t)(((actual & 0x7FE0u) << 1) | (actual & 31u));
                expected = (uint16_t)((expected & 0xFFC0u) | (expected & 31u));
            }
            REQUIRE(actual == expected);
        }
    }
}

static void CheckMenuFrame(bool rgb565) {
    REQUIRE(ComposeFixedFrame((uintptr_t)g_test_renderer, g_test_output, sizeof(g_test_output[0])));
    for (size_t y = 0; y < GAME_CLIENT_HEIGHT; ++y) {
        for (size_t x = 0; x < WIDESCREEN_CLIENT_WIDTH; ++x) {
            uint16_t expected;
            if (x >= SIDE && x < SIDE + GAME_CLIENT_WIDTH) {
                expected = y == 24 && x == SIDE + 24 ? 0xFFFF : MenuBackgroundPixel(x - SIDE, y);
            } else {
                expected = GrayPixel(g_test_original[BORDER_Y + y][BORDER_X + x], rgb565);
            }
            REQUIRE(g_test_output[y][x] == expected);
        }
    }
}

static void TestSystemMenuReturn(bool rgb565, bool paced) {
    Setup(rgb565);
    BeginGrayMenu(0x35);
    g_frame_pacing = paced;
    g_test_scene[MAP_SCENE_RENDER_SUPPRESSED_OFFSET] = 1;
    *(uintptr_t *)SYSTEM_MENU_OBJECT_ADDRESS = (uintptr_t)g_test_menu;
    /* Stage 0 still renders the original menu background. */
    REQUIRE(RuntimeUiObjectDraw(g_test_menu, NULL) == 1);
    REQUIRE(g_test_pixels[0] == 1);
    for (int tick = 0; tick < 10; ++tick) {
        *(int32_t *)(g_test_menu + SYSTEM_MENU_EXIT_PHASE_OFFSET) = tick == 0 ? 1 : 2;
        ClearVisibleMap(0);
        REQUIRE(RuntimeUiObjectDraw(g_test_menu, NULL) == 1);
        REQUIRE(RuntimeUiObjectDraw(g_test_child, NULL) == 3);
        REQUIRE(RuntimeUiObjectDraw(g_test_mask, NULL) == 2);
        REQUIRE(g_test_pixels[0] == tick + 2 && g_test_pixels[1] == 0);
        REQUIRE(g_test_pixels[2] == tick + 1);
        REQUIRE(g_test_frames[0] == tick + 2 && g_test_frames[1] == tick + 1);
        PrepareWidescreenFrame();
        REQUIRE(!IsMapActive() && !g_transition.awaiting_map_frame && !g_transition.timed_fade);
        CheckMenuFrame(rgb565);
    }
    /* Menu destruction precedes queue resume and the first visible map frame. */
    *(int32_t *)(g_test_menu + SYSTEM_MENU_EXIT_PHASE_OFFSET) = 3;
    RuntimeSystemMenuRelease(g_test_menu, NULL);
    REQUIRE(g_test_releases == 1 && g_transition.awaiting_map_frame);
    REQUIRE(g_transition.phase == CASTLE_TRANSITION_FIXED);
    PrepareWidescreenFrame();
    REQUIRE(!IsMapActive());
    CheckGrayFrame(rgb565, false);
    REQUIRE(RuntimeMapResume(g_test_scene, NULL) == 3);
    REQUIRE(g_transition.phase == CASTLE_TRANSITION_RETURNING && g_transition.awaiting_map_frame);
    ClearVisibleMap(0);
    PrepareWidescreenFrame();
    REQUIRE(!IsMapActive());
    REQUIRE(!g_transition.timed_fade);
    CheckGrayFrame(rgb565, false);
    REQUIRE(RuntimeFadeIn(4, 0) == 0);
    memcpy(g_test_map, g_test_original, sizeof(g_test_map));
    MarkMapFrame((uintptr_t)g_test_scene);
    REQUIRE(RuntimeMapEffect() == 0);
    REQUIRE(!g_transition.awaiting_map_frame && g_test_effect_calls == 0);
    PrepareWidescreenFrame();
    REQUIRE(IsMapActive());
    *(int16_t *)MAP_EFFECT_BRIGHTNESS_ADDRESS = 15;
    memcpy(g_test_map, g_test_original, sizeof(g_test_map));
    REQUIRE(RuntimeMapEffect() == 0);
    REQUIRE(g_transition.phase == CASTLE_TRANSITION_NONE);
    REQUIRE(memcmp(g_test_map, g_test_original, sizeof(g_test_map)) == 0);
}

static void TestOrdinaryMenuReturn(bool paced) {
    Setup(true);
    BeginGrayMenu(0x36);
    *(uintptr_t *)ORDINARY_MENU_OBJECT_ADDRESS = (uintptr_t)g_test_menu;
    g_frame_pacing = paced;
    g_map_frame = 1;
    PrepareWidescreenFrame();
    REQUIRE(!IsMapActive());
    RuntimeMenuRelease(g_test_menu, NULL);
    REQUIRE(g_test_releases == 1 && g_transition.awaiting_map_frame);
    for (int skipped = 0; skipped < 3; ++skipped) {
        ClearVisibleMap(0);
        PrepareWidescreenFrame();
        REQUIRE(!IsMapActive() && !g_transition.timed_fade);
        CheckGrayFrame(true, false);
    }
    for (int tick = 0; tick <= 4; ++tick) {
        g_test_tick = 2000 + (DWORD)tick * 50;
        memcpy(g_test_map, g_test_original, sizeof(g_test_map));
        MarkMapFrame((uintptr_t)g_test_scene);
        REQUIRE(RuntimeMapEffect() == 0);
        PrepareWidescreenFrame();
        REQUIRE(IsMapActive());
        for (size_t y = 0; y < GAME_CLIENT_HEIGHT; ++y) {
            for (size_t x = 0; x < WIDESCREEN_CLIENT_WIDTH; ++x) {
                REQUIRE(g_test_map[BORDER_Y + y][BORDER_X + x] != 0);
            }
        }
    }
    REQUIRE(g_test_effect_calls == 0 && g_transition.phase == CASTLE_TRANSITION_NONE);
    REQUIRE(memcmp(g_test_map, g_test_original, sizeof(g_test_map)) == 0);
}

static void TestOriginalDestinations(void) {
    const size_t flags[] = {GAME_STATE_MENU_EVENT_OFFSET, GAME_STATE_MENU_LOAD_OFFSET, GAME_STATE_MENU_TITLE_OFFSET};
    for (size_t i = 0; i < _countof(flags); ++i) {
        Setup(true);
        BeginGrayMenu(0x35);
        *(uintptr_t *)SYSTEM_MENU_OBJECT_ADDRESS = (uintptr_t)g_test_menu;
        *(int32_t *)(g_test_menu + SYSTEM_MENU_EXIT_PHASE_OFFSET) = 2;
        *(int32_t *)(g_test_state + flags[i]) = 1;
        REQUIRE(RuntimeUiObjectDraw(g_test_mask, NULL) == 2);
        REQUIRE(g_test_pixels[1] == 1);
    }
    Setup(true);
    BeginGrayMenu(0x35);
    REQUIRE(RuntimeMapLoad(g_test_scene, NULL, "another map") == 5);
    REQUIRE(g_test_loads == 1 && g_transition.phase == CASTLE_TRANSITION_NONE);
    *(int32_t *)MAP_EFFECT_STATE_ADDRESS = 7;
    REQUIRE(RuntimeMapEffect() == 17 && g_test_effect_calls == 1);
    for (size_t y = 0; y < GAME_CLIENT_HEIGHT; ++y) {
        for (size_t x = 0; x < WIDESCREEN_CLIENT_WIDTH; ++x) REQUIRE(g_test_map[BORDER_Y + y][BORDER_X + x] == 0);
    }
    /* A battle-only UI uses the original draw and pixel paths. */
    Setup(true);
    REQUIRE(RuntimeUiObjectDraw(g_test_mask, NULL) == 2);
    REQUIRE(g_test_pixels[1] == 1 && g_test_frames[1] == 1);
    /* Random battle entry must cancel any pending menu return, in all phases. */
    for (int phase = CASTLE_TRANSITION_ENTERING; phase <= CASTLE_TRANSITION_RETURNING; ++phase) {
        Setup(true);
        BeginGrayMenu(0x36);
        g_transition.phase = (CastleTransitionPhase)phase;
        REQUIRE(RuntimeMapSuspend(g_test_scene, NULL) == 4);
        REQUIRE(g_transition.phase == CASTLE_TRANSITION_NONE);
        REQUIRE(RuntimeMapResume(g_test_scene, NULL) == 3);
        *(int32_t *)MAP_EFFECT_STATE_ADDRESS = 7;
        REQUIRE(RuntimeMapEffect() == 17 && g_test_effect_calls == 1);
    }
    Setup(true);
    BeginGrayMenu(0x35);
    g_factory_opcode = 0x35;
    REQUIRE(RuntimeMapSuspend(g_test_scene, NULL) == 4);
    REQUIRE(g_transition.phase == CASTLE_TRANSITION_FIXED);
    CheckGrayFrame(true, false);
}

typedef struct TestCursorInput {
    uint8_t padding[UI_CURSOR_POSITION_OFFSET];
    POINT position;
    POINT drawn;
    unsigned draws;
} TestCursorInput;

static void __fastcall TestDrawCursor(void *manager) {
    TestCursorInput *input = manager;
    input->drawn = input->position;
    ++input->draws;
}

static void CheckCursorDraw(TestCursorInput *input, LONG x, LONG y, LONG drawn_x) {
    input->position = (POINT){x, y};
    input->draws = 0;
    RuntimeUiCursorDraw(input, NULL);
    REQUIRE(input->draws == (drawn_x >= 0 ? 1u : 0u));
    if (input->draws != 0) REQUIRE(input->drawn.x == drawn_x && input->drawn.y == y);
    REQUIRE(input->position.x == x && input->position.y == y);
}

static void TestUiCursorBounds(void) {
    TestCursorInput input = {0};
    g_original_ui_cursor_draw = TestDrawCursor;
    g_widescreen = true;
    g_map_frame = 1;
    g_cursor_map_active = 0;
    *(uintptr_t *)ORDINARY_MENU_OBJECT_ADDRESS = 0;
    /* The startup crash entered this shift with the fixed viewport's sentinel. */
    CheckCursorDraw(&input, -1, -1, -1);
    CheckCursorDraw(&input, 0, 0, SIDE);
    CheckCursorDraw(&input, GAME_CLIENT_WIDTH - 1, GAME_CLIENT_HEIGHT - 1,
                    GAME_CLIENT_WIDTH - 1 + SIDE);
    CheckCursorDraw(&input, GAME_CLIENT_WIDTH, 0, -1);

    g_cursor_map_active = 1;
    CheckCursorDraw(&input, WIDESCREEN_CLIENT_WIDTH - 1, 0, WIDESCREEN_CLIENT_WIDTH - 1);
    CheckCursorDraw(&input, WIDESCREEN_CLIENT_WIDTH, 0, -1);
    CheckCursorDraw(&input, 100, -1, -1);
    CheckCursorDraw(&input, 100, GAME_CLIENT_HEIGHT, -1);

    g_widescreen = false;
    g_map_frame = g_cursor_map_active = 0;
    CheckCursorDraw(&input, -1, 0, -1);
    CheckCursorDraw(&input, 100, 100, 100);
}

int main(void) {
    TestSystemMenuReturn(true, false);
    TestSystemMenuReturn(true, true);
    TestSystemMenuReturn(false, false);
    TestSystemMenuReturn(false, true);
    TestOrdinaryMenuReturn(false);
    TestOrdinaryMenuReturn(true);
    TestOriginalDestinations();
    TestUiCursorBounds();
    puts("Castle runtime menu transition and cursor tests passed; no game code executed");
    return 0;
}

/* Unused assembly detours are linked only to satisfy the included runtime. */
#define TEST_ASM_STUB(name) void name(void) {}
TEST_ASM_STUB(SurfaceFormatHook)
TEST_ASM_STUB(RendererWidthHook)
TEST_ASM_STUB(MapRenderHook)
TEST_ASM_STUB(MapBackdropCaptureHook)
TEST_ASM_STUB(MapUiSpriteHook)
TEST_ASM_STUB(DialogTextNameHook)
TEST_ASM_STUB(DialogTextBodyHook)
TEST_ASM_STUB(GlyphBlitHook)
TEST_ASM_STUB(BinkFrameHook)
TEST_ASM_STUB(BinkPresentCallHook)
TEST_ASM_STUB(FullscreenVblankHook)
TEST_ASM_STUB(ExperienceGainHook)
TEST_ASM_STUB(ExperienceDisplayHook)
TEST_ASM_STUB(MoneyGainHook)
TEST_ASM_STUB(MoneyDisplayHook)
TEST_ASM_STUB(EncounterInitialHook)
TEST_ASM_STUB(EncounterRegenerationHook)
TEST_ASM_STUB(FramePacingTimerSetupHook)
TEST_ASM_STUB(GameLogicTickHook)
