#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* Shared NASM also contains the hooks exercised by SwordmanSavePatchTest. */
void *g_swordman_player_turn_original;
void *g_swordman_loaded_stage;

typedef struct {
    uint32_t path;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t eflags;
} Probe;

void SwordmanExtraPatchProbe(Probe *probe, uint32_t moved, uint32_t copy);

int main(void) {
    Probe probe = {0};
    SwordmanExtraPatchProbe(&probe, 0, 0);
    assert(probe.path == 1);
    assert(probe.ecx == 0);
    assert(probe.edx == 0x13572468u);
    assert(probe.esi == 0x24681357u);

    memset(&probe, 0, sizeof(probe));
    SwordmanExtraPatchProbe(&probe, 1, 0);
    assert(probe.path == 2);
    assert(probe.ecx == 1);
    assert(probe.edx == 0x13572468u);
    assert(probe.esi == 0x24681357u);

    memset(&probe, 0, sizeof(probe));
    SwordmanExtraPatchProbe(&probe, 0, 1);
    assert(probe.path == 3);
    assert(probe.ecx == 0);
    assert(probe.edx == 0x13572468u);
    assert(probe.esi == 0x24681357u);
    return 0;
}
