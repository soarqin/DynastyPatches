#include "widescreen_transition.h"

#include <string.h>

void UpdateCastleFrameLayout(CastleFrameLayout *layout, bool map_frame, bool map_suppressed,
                            bool ordinary_menu, bool frame_pacing) {
    /* Skipped logic ticks retain the previous CPU pixels, including their layout. */
    layout->ordinary_menu_frame = ordinary_menu || (layout->ordinary_menu_frame && !map_frame);
    if (layout->ordinary_menu_frame) {
        layout->map_frame_misses = 0;
        layout->map_active = false;
        layout->cursor_map_active = false;
    } else if (map_frame) {
        layout->map_frame_misses = 0;
        layout->map_active = true;
        layout->cursor_map_active = true;
    } else if (frame_pacing && layout->map_active && !map_suppressed) {
        layout->cursor_map_active = false;
        if (++layout->map_frame_misses >= 3) {
            layout->map_frame_misses = 0;
            layout->map_active = false;
        }
    } else {
        layout->map_frame_misses = 0;
        layout->map_active = false;
        layout->cursor_map_active = false;
    }
}

bool IsFixedUiCommand(uint32_t opcode) {
    return opcode == 0x35u || opcode == 0x36u || opcode == 0x39u || opcode == 0x3Eu;
}

static bool ReadWord(const uint8_t *script, size_t size, size_t offset, uint32_t *value) {
    if (script == NULL || offset > size || size - offset < sizeof(*value)) return false;
    memcpy(value, script + offset, sizeof(*value));
    return true;
}

uint32_t FindFixedUiAfterFade(const uint8_t *script, size_t size, uint32_t table,
                            uint32_t count, uint32_t index) {
    if (count == 0 || count > 65536u || index >= count || table > size ||
        count > (size - table) / sizeof(uint32_t)) return 0;
    for (uint32_t i = index + 1u; i < count && i - index <= 16u; ++i) {
        uint32_t record, opcode;
        if (!ReadWord(script, size, (size_t)table + (size_t)i * sizeof(uint32_t), &record) ||
            !ReadWord(script, size, record, &opcode)) return 0;
        if (IsFixedUiCommand(opcode)) return opcode;
        switch (opcode) {
            case 0x01u: /* Wait. */
                break;
            default:
                return 0;
        }
    }
    return 0;
}

void CancelFixedTransition(CastleTransition *transition) {
    memset(transition, 0, sizeof(*transition));
}

void BeginFixedTransition(CastleTransition *transition, uintptr_t scene, uint32_t target) {
    if (scene == 0 || !IsFixedUiCommand(target)) return;
    if (transition->scene == scene &&
        (transition->phase == CASTLE_TRANSITION_ENTERING ||
         transition->phase == CASTLE_TRANSITION_FIXED)) {
        /* A fade command and the UI command can describe the same entry. */
        transition->target = target;
        return;
    }
    int color = transition->scene == scene && transition->phase == CASTLE_TRANSITION_RETURNING
        ? transition->color : CASTLE_TRANSITION_COLOR;
    CancelFixedTransition(transition);
    transition->scene = scene;
    transition->target = target;
    transition->phase = CASTLE_TRANSITION_ENTERING;
    transition->color = color;
    transition->initial_color = color;
}

void ReturnFromFixedTransition(CastleTransition *transition, uintptr_t scene) {
    if (transition->scene != scene || transition->phase == CASTLE_TRANSITION_NONE ||
        transition->phase == CASTLE_TRANSITION_RETURNING) return;
    transition->phase = CASTLE_TRANSITION_RETURNING;
    transition->initial_color = transition->color;
    transition->native_fade = false;
    transition->timed_fade = false;
    transition->awaiting_map_frame = true;
}

static int ClampColor(int color) {
    if (color < 0) return 0;
    if (color > CASTLE_TRANSITION_COLOR) return CASTLE_TRANSITION_COLOR;
    return color;
}

static uint32_t TimedProgress(CastleTransition *transition, uint32_t now) {
    if (!transition->timed_fade) {
        transition->started = now;
        transition->timed_fade = true;
    }
    /* Unsigned subtraction also handles GetTickCount wrapping. */
    uint32_t elapsed = now - transition->started;
    return elapsed < CASTLE_TRANSITION_DURATION_MS ? elapsed : CASTLE_TRANSITION_DURATION_MS;
}

int MapTransitionColor(CastleTransition *transition, uintptr_t scene, int effect_state,
                       int brightness, uint32_t now) {
    if (transition->phase == CASTLE_TRANSITION_NONE || transition->scene != scene) return -1;
    /* Keep scripted flashes, inversion and custom color effects intact. */
    if (effect_state != 1 && effect_state != 2 && effect_state != 3 && effect_state != 7) return -1;
    if (transition->phase == CASTLE_TRANSITION_FIXED) return 0;
    if (transition->phase == CASTLE_TRANSITION_ENTERING) {
        if (effect_state == 3 || effect_state == 7) {
            transition->native_fade = true;
            transition->color = effect_state == 7 ? 0 : ClampColor(brightness);
        } else if (effect_state == 1 && !transition->native_fade) {
            uint32_t elapsed = TimedProgress(transition, now);
            transition->color = transition->initial_color -
                (int)(elapsed * (uint32_t)transition->initial_color / CASTLE_TRANSITION_DURATION_MS);
        } else {
            return -1;
        }
        if (transition->color == 0) transition->phase = CASTLE_TRANSITION_FIXED;
    } else {
        if (effect_state == 2) {
            transition->native_fade = true;
            transition->color = ClampColor(brightness);
        } else if (effect_state == 3 || effect_state == 7) {
            /* Hold the gray map until the menu's native fade-in starts. */
            transition->color = 0;
            transition->initial_color = 0;
            transition->timed_fade = false;
        } else if (transition->native_fade) {
            transition->color = CASTLE_TRANSITION_COLOR;
        } else {
            uint32_t elapsed = TimedProgress(transition, now);
            transition->color = transition->initial_color +
                (int)(elapsed * (uint32_t)(CASTLE_TRANSITION_COLOR - transition->initial_color) /
                      CASTLE_TRANSITION_DURATION_MS);
        }
        if (transition->color == CASTLE_TRANSITION_COLOR) {
            CancelFixedTransition(transition);
            return CASTLE_TRANSITION_COLOR;
        }
    }
    return transition->color;
}
