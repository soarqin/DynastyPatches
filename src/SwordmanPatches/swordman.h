#ifndef SWORDMAN_H
#define SWORDMAN_H

#include <stdint.h>

enum {
    SWORDMAN_NO_CD = 1u << 0,
    SWORDMAN_RESIZABLE = 1u << 1,
    SWORDMAN_MOVIE = 1u << 2,
    SWORDMAN_SMOOTH = 1u << 3,
    SWORDMAN_MOVIE_CLEANUP = 1u << 4,
    SWORDMAN_SOFTWARE_MOVIE = 1u << 5,
    SWORDMAN_SPEED = 1u << 6,
    SWORDMAN_CHESTS = 1u << 7,
    SWORDMAN_ATTRIBUTE_999 = 1u << 8,
    SWORDMAN_CAST_AFTER_MOVE = 1u << 9,
    SWORDMAN_SUMMON_ITEM_COPY = 1u << 10,
    SWORDMAN_SAVE_ANYTIME = 1u << 11,
    SWORDMAN_ALL_FLAGS = (1u << 12) - 1u,
};

enum {
    SWORDMAN_GROWTH_NORMAL,
    SWORDMAN_GROWTH_MAXIMUM,
    SWORDMAN_GROWTH_MINIMUM,
};

typedef struct {
    uint32_t flags;
    uint32_t growth;
    uint32_t scale2; /* Half-units: 2 = 1x, 3 = 1.5x, ... 6 = 3x. */
} SwordmanOptions;

#endif
