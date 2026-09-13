#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <string.h>

void *g_swordman_loaded_stage;
void *g_swordman_player_turn_original;

uint32_t SwordmanTestSaveAllowed(void);
void SwordmanTestLoadReady(void *stage);
void SwordmanTestPlayerTurn(void *stage);
void *SwordmanTestPlayerTurnOriginalAddress(void);

uint32_t g_original_calls;

static void CheckState(uint8_t *stage, uint32_t marker, uint8_t value) {
    assert(*(uint32_t *)(stage + 0x0C) == value);
    assert(g_swordman_loaded_stage == (marker != 0 ? stage : NULL));
}

int main(void) {
    uint8_t stage[0x20] = {0};
    uint8_t other_stage[0x20] = {0};
    uint8_t saved[sizeof(stage)];

    assert(SwordmanTestSaveAllowed() == 1);
    stage[0x0C] = 0x44;
    stage[0x10] = 0x99;
    memcpy(saved, stage, sizeof(stage));
    SwordmanTestLoadReady(stage);
    assert(*(uint32_t *)(stage + 0x0C) == 9);
    assert(*(uint8_t *)(stage + 0x10) == 0x99);
    assert(g_swordman_loaded_stage == stage);

    /* A different stage takes the original path and leaves the load marker. */
    g_swordman_player_turn_original = SwordmanTestPlayerTurnOriginalAddress();
    SwordmanTestPlayerTurn(other_stage);
    assert(g_swordman_loaded_stage == stage);
    assert(g_original_calls == 1);

    /* The matching stage consumes the marker once and leaves saved bytes intact. */
    SwordmanTestPlayerTurn(stage);
    assert(g_swordman_loaded_stage == NULL);
    assert(memcmp(stage, saved, sizeof(stage)) != 0);
    assert(*(uint32_t *)(stage + 0x0C) == 9);
    assert(*(uint8_t *)(stage + 0x10) == 0x99);

    /* The next ordinary turn uses the original path. */
    SwordmanTestPlayerTurn(stage);
    assert(g_original_calls == 2);
    CheckState(stage, 0, 9);
    return 0;
}
