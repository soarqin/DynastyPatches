#include "../runtime/widescreen_transition.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "line %d: %s\n", __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static const uintptr_t MAP_SCENE = 123u;
static uint8_t g_script[256];

static void TestFrameLayout(void) {
    for (int paced = 0; paced <= 1; ++paced) {
        CastleFrameLayout layout = {0};
        UpdateCastleFrameLayout(&layout, true, false, false, paced != 0);
        REQUIRE(layout.map_active && layout.cursor_map_active);
        /* Ordinary menus leave the map in the queue, but require centered input/upload. */
        UpdateCastleFrameLayout(&layout, true, false, true, paced != 0);
        REQUIRE(!layout.map_active && !layout.cursor_map_active);
        UpdateCastleFrameLayout(&layout, false, false, false, paced != 0);
        REQUIRE(!layout.map_active && !layout.cursor_map_active);
        UpdateCastleFrameLayout(&layout, false, false, false, paced != 0);
        REQUIRE(!layout.map_active && !layout.cursor_map_active);
        UpdateCastleFrameLayout(&layout, true, false, false, paced != 0);
        REQUIRE(layout.map_active && layout.cursor_map_active && !layout.ordinary_menu_frame);
        /* A suspended map can disappear from the queue on the first battle frame. */
        UpdateCastleFrameLayout(&layout, false, true, false, paced != 0);
        REQUIRE(!layout.map_active && !layout.cursor_map_active);
    }
    CastleFrameLayout layout = {0};
    UpdateCastleFrameLayout(&layout, true, false, false, true);
    for (int skipped = 0; skipped < 4; ++skipped) {
        UpdateCastleFrameLayout(&layout, false, false, false, false);
        REQUIRE(layout.map_active && !layout.cursor_map_active);
    }
    /* A real update can switch layouts even if it follows the map immediately. */
    UpdateCastleFrameLayout(&layout, false, false, false, true);
    REQUIRE(!layout.map_active);
}

static void WriteWord(size_t offset, uint32_t value) {
    memcpy(g_script + offset, &value, sizeof(value));
}

static void TestDestinations(void) {
    REQUIRE(!IsFixedUiCommand(0x37));
    for (uint32_t opcode = 0; opcode <= 0x9Au; ++opcode) {
        WriteWord(0, 64);
        WriteWord(4, 80);
        WriteWord(64, 0x53);
        WriteWord(80, opcode);
        REQUIRE(FindFixedUiAfterFade(g_script, sizeof(g_script), 0, 2, 0) ==
                (IsFixedUiCommand(opcode) ? opcode : 0));
    }
    WriteWord(8, 100);
    WriteWord(80, 0x01);
    WriteWord(100, 0x36);
    REQUIRE(FindFixedUiAfterFade(g_script, sizeof(g_script), 0, 3, 0) == 0x36);
    for (uint32_t opcode = 0x3Au; opcode <= 0x3Cu; ++opcode) {
        WriteWord(80, opcode);
        REQUIRE(FindFixedUiAfterFade(g_script, sizeof(g_script), 0, 3, 0) == 0);
    }
    WriteWord(80, 0x47); /* This opcode branches; do not look past it. */
    REQUIRE(FindFixedUiAfterFade(g_script, sizeof(g_script), 0, 3, 0) == 0);
    WriteWord(80, 0x00); /* End of event. */
    REQUIRE(FindFixedUiAfterFade(g_script, sizeof(g_script), 0, 3, 0) == 0);
    REQUIRE(FindFixedUiAfterFade(NULL, 0, 0, 2, 0) == 0);
    REQUIRE(FindFixedUiAfterFade(g_script, sizeof(g_script), UINT32_MAX, 2, 0) == 0);
    REQUIRE(FindFixedUiAfterFade(g_script, sizeof(g_script), 0, UINT32_MAX, 0) == 0);
    REQUIRE(FindFixedUiAfterFade(g_script, sizeof(g_script), 0, 2, 2) == 0);
    WriteWord(4, UINT32_MAX);
    REQUIRE(FindFixedUiAfterFade(g_script, sizeof(g_script), 0, 2, 0) == 0);
}

static void TestNativeFade(void) {
    CastleTransition transition = {0};
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 3, 0, 0) == -1);
    for (uint32_t target = 0x35u; target <= 0x3Eu; ++target) {
        if (!IsFixedUiCommand(target)) continue;
        BeginFixedTransition(&transition, MAP_SCENE, target);
        for (int brightness = 15; brightness >= 0; --brightness) {
            REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 3, brightness, 10) == brightness);
        }
        REQUIRE(transition.phase == CASTLE_TRANSITION_FIXED);
        REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 7, 0, 11) == 0);
        BeginFixedTransition(&transition, MAP_SCENE, target);
        REQUIRE(transition.color == 0); /* Repeated command cannot restart the fade. */
        ReturnFromFixedTransition(&transition, MAP_SCENE);
        for (int brightness = 0; brightness <= 15; ++brightness) {
            REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 2, brightness, 20) == brightness);
        }
        REQUIRE(transition.phase == CASTLE_TRANSITION_NONE);
        REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 1, 15, 21) == -1);
    }
    BeginFixedTransition(&transition, MAP_SCENE, 0x35);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 3, -4, 0) == 0);
    ReturnFromFixedTransition(&transition, MAP_SCENE);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 2, 19, 0) == 15);
}

static void TestTimedReturn(void) {
    CastleTransition transition = {0};
    BeginFixedTransition(&transition, MAP_SCENE, 0x36);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 1, 15, 1000) == 15);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 1, 15, 1100) == 8);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 1, 15, 1200) == 0);
    ReturnFromFixedTransition(&transition, MAP_SCENE);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 1, 15, UINT32_MAX - 100u) == 0);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 1, 15, UINT32_MAX) == 7);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 1, 15, 99) == 15);
    REQUIRE(transition.phase == CASTLE_TRANSITION_NONE);

    BeginFixedTransition(&transition, MAP_SCENE, 0x35);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 3, 0, 0) == 0);
    ReturnFromFixedTransition(&transition, MAP_SCENE);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 2, 11, 1) == 11);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 1, 15, 2) == 15);

    /* Closing a menu midway reverses from the color already on screen. */
    BeginFixedTransition(&transition, MAP_SCENE, 0x36);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 1, 15, 1000) == 15);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 1, 15, 1100) == 8);
    ReturnFromFixedTransition(&transition, MAP_SCENE);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 1, 15, 1200) == 8);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 1, 15, 1300) == 11);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 1, 15, 1400) == 15);
}

static void TestCancellation(void) {
    CastleTransition transition = {0};
    BeginFixedTransition(&transition, MAP_SCENE, 0x37);
    REQUIRE(transition.phase == CASTLE_TRANSITION_NONE);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 7, 0, 100) == -1);
    BeginFixedTransition(&transition, MAP_SCENE, 0x35);
    CancelFixedTransition(&transition);
    /* Changing maps or loading a save removes both directions of the effect. */
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 3, 0, 0) == -1);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE + 1, 2, 0, 0) == -1);
    BeginFixedTransition(&transition, MAP_SCENE + 1, 0x35);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE, 3, 0, 0) == -1);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE + 1, 4, 0, 0) == -1);
    REQUIRE(MapTransitionColor(&transition, MAP_SCENE + 1, 6, 0, 0) == -1);
    ReturnFromFixedTransition(&transition, MAP_SCENE);
    REQUIRE(transition.phase == CASTLE_TRANSITION_ENTERING);
}

int main(void) {
    TestFrameLayout();
    TestDestinations();
    TestNativeFade();
    TestTimedReturn();
    TestCancellation();
    puts("Castle widescreen transition tests passed");
    return 0;
}
