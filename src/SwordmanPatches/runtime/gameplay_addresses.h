#ifndef SWORDMAN_GAMEPLAY_ADDRESSES_H
#define SWORDMAN_GAMEPLAY_ADDRESSES_H

#include <stdint.h>

typedef struct {
    const char *name;
    const char *sha256;
    uintptr_t speed_delay;
    uintptr_t speed_frame_value;
    uintptr_t speed_action_multiplier;
    uintptr_t speed_action_positive;
    uintptr_t speed_action_negative;
    uintptr_t speed_action_damage;
    uintptr_t speed_action_damage_negative;
    uintptr_t speed_timer;
    uintptr_t chest_sentinel;
    uintptr_t attribute_caps[5];
    uintptr_t growth_random[7];
    uintptr_t cast_after_move;
    uintptr_t summon_item_copy;
    uintptr_t save_allowed;
    uintptr_t load_ready;
    uintptr_t player_turn_reset;
} SwordmanGameplayVersion;

/* All entries are VAs in the fixed-base Sm_win2.exe image. */
static const SwordmanGameplayVersion SWORDMAN_SM_WIN2 = {
    "Sm_win2.exe",
    "2ad46144eaca95b92a7d28ad946687df09fb0c3155cf3ccafa8381fba53afbb2",
    0x004056D7u,
    0x00405739u,
    0x0042B2E2u,
    0x0042B34Eu,
    0x0042B380u,
    0x0042B3B7u,
    0x0042B3E3u,
    0x0044F682u,
    0x0044EEE3u,
    {
        0x00430899u,
        0x004308B6u,
        0x004308D3u,
        0x004308F0u,
        0x0043090Du,
    },
    {
        0x0043080Du,
        0x00430828u,
        0x00430843u,
        0x0043085Eu,
        0x00430879u,
        0x00430930u,
        0x00430977u,
    },
    0x004248B4u,
    0x00426107u,
    0x00446380u,
    0x0044A936u,
    0x0044A2C7u,
};

#endif
