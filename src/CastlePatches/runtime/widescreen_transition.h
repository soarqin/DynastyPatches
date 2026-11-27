#ifndef CASTLE_WIDESCREEN_TRANSITION_H
#define CASTLE_WIDESCREEN_TRANSITION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { CASTLE_TRANSITION_COLOR = 15, CASTLE_TRANSITION_DURATION_MS = 200 };

typedef enum {
    CASTLE_TRANSITION_NONE,
    CASTLE_TRANSITION_ENTERING,
    CASTLE_TRANSITION_FIXED,
    CASTLE_TRANSITION_RETURNING,
} CastleTransitionPhase;

typedef struct {
    uintptr_t scene;
    uint32_t target;
    uint32_t started;
    CastleTransitionPhase phase;
    int color;
    int initial_color;
    bool native_fade;
    bool timed_fade;
    bool awaiting_map_frame;
} CastleTransition;

typedef struct {
    bool ordinary_menu_frame;
    bool map_active;
    bool cursor_map_active;
} CastleFrameLayout;

void UpdateCastleFrameLayout(CastleFrameLayout *layout, bool map_frame, bool map_suppressed,
                            bool ordinary_menu, bool logic_updated);

bool IsFixedUiCommand(uint32_t opcode);
/* Only follow known straight-line commands; a branch or map load ends the search. */
uint32_t FindFixedUiAfterFade(const uint8_t *script, size_t size, uint32_t table,
                            uint32_t count, uint32_t index);
void BeginFixedTransition(CastleTransition *transition, uintptr_t scene, uint32_t target);
void ReturnFromFixedTransition(CastleTransition *transition, uintptr_t scene);
void CancelFixedTransition(CastleTransition *transition);
/* -1 means that the original game effect must be used. */
int MapTransitionColor(CastleTransition *transition, uintptr_t scene, int effect_state,
                       int brightness, uint32_t now);

#endif
