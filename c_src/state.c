/* state.c - the one machine-state instance and the computed-address
 * accessors (see state.h). */
#include "state.h"
#include "progrom.h"
#include "vecrom.h"

machine_state g;

uint8_t cpu_rd(uint16_t addr)
{
    /* 15-bit address decode: A15 is ignored (HARDWARE_NOTES.md, "The 15-bit
     * address decode / ROM mirror") - $8000-$FFFF mirrors $0000-$7FFF, which
     * is how the 6502 vectors at $7FFA reach $FFFA. */
    uint16_t a = addr & 0x7FFF;
    if (a < 0x0400) return g.ram[a];
    if (a >= 0x2000 && a < 0x3000) return g.vram[a - 0x2000];
    if (a >= 0x3000 && a < 0x4000) return vecrom[a - 0x3000];
    if (a >= 0x5000 && a < 0x8000) return progrom[a - 0x5000];
    return 0x00;   /* $0400-$07FF, $0800-$1FFF (hardware, not reachable this
                     * way), $4000-$4FFF (unmapped): open bus */
}

void cpu_wr(uint16_t addr, uint8_t v)
{
    uint16_t a = addr & 0x7FFF;
    if (a < 0x0400) { g.ram[a] = v; return; }
    if (a >= 0x2000 && a < 0x3000) { g.vram[a - 0x2000] = v; return; }
    g.stray_writes++;
}
