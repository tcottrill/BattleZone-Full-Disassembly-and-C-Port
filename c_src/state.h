/* state.h - Battlezone C port: the whole machine state.
 *
 * Same state model as the Tempest / Space Duel ports (see Tempest's
 * DESIGN-equivalent reasoning in its state.h): the machine's own memory
 * arrays, byte-diffable against tests/refrun.exe and checked call by call
 * by a later lockstep verifier.  Named cells go through the generated
 * aliases in state_defs.h (tools/gen_state.py from ../disasm/bzone_defines.asm).
 *
 * Battlezone differs from Tempest here: RAM is $0000-$03FF (1 KB, not 2 KB),
 * there is no colour RAM (the AVG is monochrome - HARDWARE_NOTES.md /
 * MATHBOX_NOTES.md), and no EAROM (high scores are volatile RAM).
 */
#ifndef STATE_H
#define STATE_H

#include <stdint.h>

typedef struct {
    uint8_t  ram[0x400];    /* $0000-$03FF: zero page, stack page, game RAM */
    uint8_t  vram[0x1000];  /* $2000-$2FFF: real AVG display-list bytes     */

    /* ---- seam bookkeeping (not CPU RAM) ---------------------------------- */
    uint32_t irq_count;       /* NMIs serviced since reset (Battlezone's NMI is
                                  the "IRQ" of the Tempest/Space Duel template) */
    uint32_t pass_count;    /* MAIN passes since reset                       */
    uint32_t stray_writes;  /* cpu_wr() outside RAM/vector RAM               */

    /* ---- CPU facts that outlive one routine (future milestones) ---------- */
    uint8_t  dflag;           /* the 6502 D flag: BZONE.MAC brackets the HITS
                                  (score) BCD adds with SED/CLD, and COIN65's
                                  math is always CLD first (CPORT_READINESS.md,
                                  "Decimal (BCD) mode").  adc6502/sbc6502 below
                                  honour it; not used until M3+ translates HITS. */
} machine_state;

extern machine_state g;

#include "state_defs.h"

/* Generic accessors for indexed / indirect idioms. */
#define RAM(a)   (g.ram[(a) & 0x3FF])
#define VRAM(a)  (g.vram[((a) - 0x2000) & 0xFFF])  /* a = CPU $2000-$2FFF */

/* The display-list pointer is the real zero-page pair VGLIST/VGLIST+1. */
#define VGLIST_ADDR ((uint16_t)(g.ram[A_VGLIST] | ((uint16_t)g.ram[A_VGLIST + 1] << 8)))

/* A 6502 read / write at a computed CPU address ((zp),Y stores, ROM table
 * reads): RAM, vector RAM, vector ROM, program ROM (and its $8000-$FFFF
 * mirror - the board decodes only 15 address bits, HARDWARE_NOTES.md "The
 * 15-bit address decode / ROM mirror").  Hardware registers are NOT
 * reachable this way - translated code calls the hw_* seam for those. */
uint8_t cpu_rd(uint16_t addr);
void    cpu_wr(uint16_t addr, uint8_t v);

/* ---- 6502 ADC / SBC with the NMOS decimal mode (g.dflag) ----------------
 * Bit-for-bit the shared core's op_adc/op_sbc (C:\Source2026\shared\ref6502),
 * copied from the Tempest port's state.h: decimal ADC takes Z from the binary
 * sum; SBC flags are always binary.  *c is the carry in and out; *z (may be
 * NULL) receives the Z flag.  Not exercised until a routine that runs in
 * decimal mode (HITS, the score) is translated - kept here now so state.h
 * matches the template and needs no interface change later. */
static inline uint8_t adc6502(uint8_t a, uint8_t v, unsigned *c, int *z)
{
    unsigned cin = *c ? 1u : 0u;
    if (g.dflag) {
        unsigned al = (a & 0x0Fu) + (v & 0x0Fu) + cin;
        unsigned t;
        if (al >= 0x0A) al = ((al + 0x06) & 0x0F) + 0x10;
        t = (a & 0xF0u) + (v & 0xF0u) + al;
        if (z) *z = (uint8_t)(a + v + cin) == 0;
        if (t >= 0xA0) t += 0x60;
        *c = t >= 0x100;
        return (uint8_t)t;
    } else {
        unsigned sum = (unsigned)a + v + cin;
        *c = sum > 0xFF;
        if (z) *z = (uint8_t)sum == 0;
        return (uint8_t)sum;
    }
}

static inline uint8_t sbc6502(uint8_t a, uint8_t v, unsigned *c, int *z)
{
    int cin = *c ? 1 : 0;
    int bin = (int)a - (int)v - (1 - cin);
    *c = bin >= 0;
    if (z) *z = (uint8_t)bin == 0;
    if (g.dflag) {
        int al = (int)(a & 0x0F) - (int)(v & 0x0F) - (1 - cin);
        int t;
        if (al < 0) al = ((al - 0x06) & 0x0F) - 0x10;
        t = (int)(a & 0xF0) - (int)(v & 0xF0) + al;
        if (t < 0) t -= 0x60;
        return (uint8_t)(t & 0xFF);
    }
    return (uint8_t)bin;
}

/* ---- unnamed cells (none yet) ------------------------------------------- */

#endif  /* STATE_H */
