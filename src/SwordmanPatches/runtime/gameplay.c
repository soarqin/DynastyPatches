#include "runtime_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../swordman.h"
#include "gameplay_addresses.h"

void SwordmanSpeedDelayHook(void);
void SwordmanSpeedFrameValueHook(void);
void SwordmanSpeedActionMultiplierHook(void);
void SwordmanSpeedActionPositiveHook(void);
void SwordmanSpeedActionNegativeHook(void);
void SwordmanSpeedActionDamageHook(void);
void SwordmanSpeedActionDamageNegativeHook(void);
void SwordmanSpeedTimerHook(void);
void SwordmanChestHook(void);

void SwordmanAttributeCapHook0(void);
void SwordmanAttributeCapHook1(void);
void SwordmanAttributeCapHook2(void);
void SwordmanAttributeCapHook3(void);
void SwordmanAttributeCapHook4(void);

void SwordmanGrowthMaximumHook0(void);
void SwordmanGrowthMaximumHook1(void);
void SwordmanGrowthMaximumHook2(void);
void SwordmanGrowthMaximumHook3(void);
void SwordmanGrowthMaximumHook4(void);
void SwordmanGrowthMaximumHook5(void);
void SwordmanGrowthMaximumHook6(void);
void SwordmanGrowthMinimumHook0(void);
void SwordmanGrowthMinimumHook1(void);
void SwordmanGrowthMinimumHook2(void);
void SwordmanGrowthMinimumHook3(void);
void SwordmanGrowthMinimumHook4(void);
void SwordmanGrowthMinimumHook5(void);
void SwordmanGrowthMinimumHook6(void);
void SwordmanCastAfterMoveHook(void);
void SwordmanSummonItemCopyHook(void);
void SwordmanSaveAllowedHook(void);
void SwordmanLoadReadyHook(void);
void SwordmanPlayerTurnHook(void);

typedef struct {
    uintptr_t address;
    const uint8_t *expected;
    size_t length;
    LPVOID detour;
    LPVOID *original;
} GameplayHook;

static LPVOID g_speed_original[8];
static LPVOID g_chest_original;
static LPVOID g_attribute_original[5];
static LPVOID g_growth_original[7];
static LPVOID g_extra_original[2];
static LPVOID g_save_original[2];
LPVOID g_swordman_player_turn_original;
LPVOID g_swordman_loaded_stage;

static const uint8_t g_speed_delay_expected[] = {
    0xC7, 0x46, 0x28, 0x01, 0x00, 0x00, 0x00,
};
static const uint8_t g_speed_frame_expected[] = {
    0x66, 0x8B, 0x50, 0x02, 0x89, 0x56, 0x34,
};
static const uint8_t g_speed_action_multiplier_expected[] = {
    0xC7, 0x46, 0x20, 0x01, 0x00, 0x00, 0x00,
};
static const uint8_t g_speed_action_positive_expected[] = {
    0xC7, 0x46, 0x30, 0x01, 0x00, 0x00, 0x00,
};
static const uint8_t g_speed_action_negative_expected[] = {
    0xC7, 0x46, 0x30, 0xFF, 0xFF, 0xFF, 0xFF,
};
static const uint8_t g_speed_action_damage_expected[] = {
    0xC7, 0x46, 0x34, 0x01, 0x00, 0x00, 0x00,
};
static const uint8_t g_speed_action_damage_negative_expected[] = {
    0xC7, 0x46, 0x34, 0xFF, 0xFF, 0xFF, 0xFF,
};
static const uint8_t g_speed_timer_expected[] = {
    0xB8, 0x28, 0x00, 0x00, 0x00,
};
static const uint8_t g_chest_expected[] = {
    0x66, 0x83, 0x78, 0x0C, 0xFF, 0x74, 0x3A,
};
static const uint8_t g_attribute_expected[5][18] = {
    { 0x81, 0xFA, 0xF4, 0x01, 0x00, 0x00, 0x7E, 0x0A, 0xBA, 0xF4, 0x01, 0x00, 0x00, 0x2B, 0xD1, 0x89, 0x56, 0x34 },
    { 0x81, 0xFA, 0xF4, 0x01, 0x00, 0x00, 0x7E, 0x0A, 0xBA, 0xF4, 0x01, 0x00, 0x00, 0x2B, 0xD1, 0x89, 0x56, 0x38 },
    { 0x81, 0xFA, 0xF4, 0x01, 0x00, 0x00, 0x7E, 0x0A, 0xBA, 0xF4, 0x01, 0x00, 0x00, 0x2B, 0xD1, 0x89, 0x56, 0x3C },
    { 0x81, 0xFA, 0xF4, 0x01, 0x00, 0x00, 0x7E, 0x0A, 0xBA, 0xF4, 0x01, 0x00, 0x00, 0x2B, 0xD1, 0x89, 0x56, 0x40 },
    { 0x81, 0xFA, 0xF4, 0x01, 0x00, 0x00, 0x7E, 0x0A, 0xBA, 0xF4, 0x01, 0x00, 0x00, 0x2B, 0xD1, 0x89, 0x56, 0x44 },
};
static const uint8_t g_growth_expected[7][7] = {
    { 0x99, 0x41, 0x8B, 0x5E, 0x34, 0xF7, 0xF9 },
    { 0x99, 0x41, 0x8B, 0x5E, 0x38, 0xF7, 0xF9 },
    { 0x99, 0x41, 0x8B, 0x5E, 0x3C, 0xF7, 0xF9 },
    { 0x99, 0x41, 0x8B, 0x5E, 0x40, 0xF7, 0xF9 },
    { 0x99, 0x41, 0x8B, 0x6E, 0x34, 0xF7, 0xF9 },
    { 0x99, 0x41, 0xF7, 0xF9, 0x0F, 0xBF, 0xC5 },
    { 0x99, 0x41, 0xF7, 0xF9, 0x0F, 0xBF, 0xC5 },
};
static const uint8_t g_cast_after_move_expected[] = {
    0x75, 0x5A, 0x66, 0x83, 0x78, 0x18, 0x05,
};
static const uint8_t g_summon_item_copy_expected[] = {
    0x66, 0xC7, 0x06, 0xFF, 0xFF, 0x83, 0xC2, 0x04,
};
static const uint8_t g_save_allowed_expected[] = {
    0x8B, 0x89, 0x88, 0x00, 0x00, 0x00,
};
static const uint8_t g_load_ready_expected[] = {
    0xC7, 0x46, 0x0C, 0x09, 0x00, 0x00, 0x00,
};
static const uint8_t g_player_turn_expected[] = {
    0x33, 0xFF, 0x8B, 0x45, 0x18,
};

static bool InstallGameplayHooks(const GameplayHook *hooks, size_t count) {
    for (size_t index = 0; index < count; ++index) {
        const GameplayHook *hook = &hooks[index];
        if (!CreateGameHook(hook->address,
                            hook->expected,
                            hook->length,
                            hook->detour,
                            hook->original)) {
            return false;
        }
    }
    return true;
}

bool CreateGameplayHooks(const SwordmanOptions *options) {
    if (options == NULL || options->growth > SWORDMAN_GROWTH_MINIMUM) return false;

    const SwordmanGameplayVersion *version = &SWORDMAN_SM_WIN2;

    if ((options->flags & SWORDMAN_SPEED) != 0) {
        const GameplayHook hooks[] = {
            { version->speed_delay, g_speed_delay_expected, sizeof(g_speed_delay_expected),
              HOOK_ADDRESS(SwordmanSpeedDelayHook), &g_speed_original[0] },
            { version->speed_frame_value, g_speed_frame_expected, sizeof(g_speed_frame_expected),
              HOOK_ADDRESS(SwordmanSpeedFrameValueHook), &g_speed_original[1] },
            { version->speed_action_multiplier, g_speed_action_multiplier_expected,
              sizeof(g_speed_action_multiplier_expected), HOOK_ADDRESS(SwordmanSpeedActionMultiplierHook),
              &g_speed_original[2] },
            { version->speed_action_positive, g_speed_action_positive_expected,
              sizeof(g_speed_action_positive_expected), HOOK_ADDRESS(SwordmanSpeedActionPositiveHook),
              &g_speed_original[3] },
            { version->speed_action_negative, g_speed_action_negative_expected,
              sizeof(g_speed_action_negative_expected), HOOK_ADDRESS(SwordmanSpeedActionNegativeHook),
              &g_speed_original[4] },
            { version->speed_action_damage, g_speed_action_damage_expected,
              sizeof(g_speed_action_damage_expected), HOOK_ADDRESS(SwordmanSpeedActionDamageHook),
              &g_speed_original[5] },
            { version->speed_action_damage_negative, g_speed_action_damage_negative_expected,
              sizeof(g_speed_action_damage_negative_expected),
              HOOK_ADDRESS(SwordmanSpeedActionDamageNegativeHook), &g_speed_original[6] },
            { version->speed_timer, g_speed_timer_expected, sizeof(g_speed_timer_expected),
              HOOK_ADDRESS(SwordmanSpeedTimerHook), &g_speed_original[7] },
        };
        if (!InstallGameplayHooks(hooks, sizeof(hooks) / sizeof(hooks[0]))) return false;
    }

    if ((options->flags & SWORDMAN_CHESTS) != 0) {
        const GameplayHook hook = {
            version->chest_sentinel,
            g_chest_expected,
            sizeof(g_chest_expected),
            HOOK_ADDRESS(SwordmanChestHook),
            &g_chest_original,
        };
        if (!InstallGameplayHooks(&hook, 1)) return false;
    }

    if ((options->flags & SWORDMAN_ATTRIBUTE_999) != 0) {
        const LPVOID detours[] = {
            HOOK_ADDRESS(SwordmanAttributeCapHook0),
            HOOK_ADDRESS(SwordmanAttributeCapHook1),
            HOOK_ADDRESS(SwordmanAttributeCapHook2),
            HOOK_ADDRESS(SwordmanAttributeCapHook3),
            HOOK_ADDRESS(SwordmanAttributeCapHook4),
        };
        GameplayHook hooks[sizeof(detours) / sizeof(detours[0])];
        for (size_t index = 0; index < sizeof(hooks) / sizeof(hooks[0]); ++index) {
            hooks[index].address = version->attribute_caps[index];
            hooks[index].expected = g_attribute_expected[index];
            hooks[index].length = sizeof(g_attribute_expected[0]);
            hooks[index].detour = detours[index];
            hooks[index].original = &g_attribute_original[index];
        }
        if (!InstallGameplayHooks(hooks, sizeof(hooks) / sizeof(hooks[0]))) return false;
    }

    if (options->growth != SWORDMAN_GROWTH_NORMAL) {
        const LPVOID maximum_detours[] = {
            HOOK_ADDRESS(SwordmanGrowthMaximumHook0),
            HOOK_ADDRESS(SwordmanGrowthMaximumHook1),
            HOOK_ADDRESS(SwordmanGrowthMaximumHook2),
            HOOK_ADDRESS(SwordmanGrowthMaximumHook3),
            HOOK_ADDRESS(SwordmanGrowthMaximumHook4),
            HOOK_ADDRESS(SwordmanGrowthMaximumHook5),
            HOOK_ADDRESS(SwordmanGrowthMaximumHook6),
        };
        const LPVOID minimum_detours[] = {
            HOOK_ADDRESS(SwordmanGrowthMinimumHook0),
            HOOK_ADDRESS(SwordmanGrowthMinimumHook1),
            HOOK_ADDRESS(SwordmanGrowthMinimumHook2),
            HOOK_ADDRESS(SwordmanGrowthMinimumHook3),
            HOOK_ADDRESS(SwordmanGrowthMinimumHook4),
            HOOK_ADDRESS(SwordmanGrowthMinimumHook5),
            HOOK_ADDRESS(SwordmanGrowthMinimumHook6),
        };
        GameplayHook hooks[sizeof(maximum_detours) / sizeof(maximum_detours[0])];
        for (size_t index = 0; index < sizeof(hooks) / sizeof(hooks[0]); ++index) {
            hooks[index].address = version->growth_random[index];
            hooks[index].expected = g_growth_expected[index];
            hooks[index].length = sizeof(g_growth_expected[0]);
            hooks[index].detour = options->growth == SWORDMAN_GROWTH_MAXIMUM
                                      ? maximum_detours[index]
                                      : minimum_detours[index];
            hooks[index].original = &g_growth_original[index];
        }
        if (!InstallGameplayHooks(hooks, sizeof(hooks) / sizeof(hooks[0]))) return false;
    }

    if ((options->flags & SWORDMAN_CAST_AFTER_MOVE) != 0) {
        const GameplayHook hook = {
            version->cast_after_move,
            g_cast_after_move_expected,
            sizeof(g_cast_after_move_expected),
            HOOK_ADDRESS(SwordmanCastAfterMoveHook),
            &g_extra_original[0],
        };
        if (!InstallGameplayHooks(&hook, 1)) return false;
    }

    if ((options->flags & SWORDMAN_SUMMON_ITEM_COPY) != 0) {
        const GameplayHook hook = {
            version->summon_item_copy,
            g_summon_item_copy_expected,
            sizeof(g_summon_item_copy_expected),
            HOOK_ADDRESS(SwordmanSummonItemCopyHook),
            &g_extra_original[1],
        };
        if (!InstallGameplayHooks(&hook, 1)) return false;
    }

    if ((options->flags & SWORDMAN_SAVE_ANYTIME) != 0) {
        const GameplayHook hooks[] = {
            { version->save_allowed, g_save_allowed_expected, sizeof(g_save_allowed_expected),
              HOOK_ADDRESS(SwordmanSaveAllowedHook), &g_save_original[0] },
            { version->load_ready, g_load_ready_expected, sizeof(g_load_ready_expected),
              HOOK_ADDRESS(SwordmanLoadReadyHook), &g_save_original[1] },
            { version->player_turn_reset, g_player_turn_expected, sizeof(g_player_turn_expected),
              HOOK_ADDRESS(SwordmanPlayerTurnHook), &g_swordman_player_turn_original },
        };
        if (!InstallGameplayHooks(hooks, sizeof(hooks) / sizeof(hooks[0]))) return false;
    }

    return true;
}
