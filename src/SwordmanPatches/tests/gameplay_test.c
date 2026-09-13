#include <assert.h>
#include <stdint.h>

#include "../swordman.h"

static int GrowthDelta(int roll, int minimum, int span, uint32_t mode) {
    if (mode == SWORDMAN_GROWTH_MAXIMUM) return span + minimum;
    if (mode == SWORDMAN_GROWTH_MINIMUM) return minimum;
    return roll % (span + 1) + minimum;
}

static int CappedDelta(int base, int delta, int cap) {
    return base + delta > cap ? cap - base : delta;
}

int main(void) {
    assert(GrowthDelta(13, 2, 7, SWORDMAN_GROWTH_NORMAL) == 7);
    assert(GrowthDelta(13, 2, 7, SWORDMAN_GROWTH_MAXIMUM) == 9);
    assert(GrowthDelta(13, 2, 7, SWORDMAN_GROWTH_MINIMUM) == 2);
    assert(CappedDelta(495, GrowthDelta(13, 2, 7, SWORDMAN_GROWTH_MAXIMUM), 500) == 5);
    assert(CappedDelta(495, GrowthDelta(13, 2, 7, SWORDMAN_GROWTH_MAXIMUM), 999) == 9);
    assert(CappedDelta(995, GrowthDelta(13, 2, 7, SWORDMAN_GROWTH_MAXIMUM), 999) == 4);
    assert(CappedDelta(995, GrowthDelta(13, 2, 7, SWORDMAN_GROWTH_MINIMUM), 999) == 2);
    return 0;
}
