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

    /* Reference-harness bookkeeping; native score arithmetic is explicit
     * decimal arithmetic and does not consult CPU flags. */
    uint8_t  dflag;
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

/* ---- unnamed cells (none yet) ------------------------------------------- */

#endif  /* STATE_H */
