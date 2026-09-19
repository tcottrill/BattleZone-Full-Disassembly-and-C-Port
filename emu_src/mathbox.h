/* mathbox.h - the Atari Math Box (4 x Am2901 bit slice + 256 x 24-bit
 * microcode) behind $6040/$6060/$6070/$6080-$609F.
 *
 * Hardware, not a ROM routine: shared by the reference harness
 * (tests/refrun.c) and, later, the game's hardware seam, so both run the
 * SAME model.  It executes Mike Albaugh's real microcode (PROMs 136002-127..132
 * via mbprom.c) instead of a high-level transcription of the functions, so
 * its results are whatever the board's microcode computes, bit for bit, as far
 * as the documented 2901 behaviour goes (MBUDOC.DOC; see mathbox.c for the
 * two places the documentation leaves open).
 *
 * A 6502 write to $6080+n loads the uPC from the mapping PROM (entry n),
 * presents the byte on the D bus and clocks the ALU until an instruction with
 * STALL=1.  The model runs that to completion inside the write, so MSTAT D7
 * (busy) always reads 0.
 */
#ifndef MATHBOX_H
#define MATHBOX_H

#include <stdint.h>

typedef struct mathbox {
    uint16_t r[16];       /* 2901 RAM registers (16-bit, four slices)      */
    uint16_t q;           /* Q register                                    */
    uint16_t y;           /* Y bus as left by the last instruction          */
    uint8_t  jt;          /* jump-target latch (LDAB)                      */
    uint8_t  q0;          /* Q0 pin latch for the MULT (CADD) trick        */
    /* diagnostics */
    uint32_t starts;      /* writes that ran microcode                     */
    uint32_t steps;       /* microinstructions executed                    */
    uint32_t runaway;     /* writes that hit the step cap without STALL    */
    uint32_t xor_sign;    /* SIGN used after EXOR/EXNOR (OVR undocumented) */
} mathbox;

void    mb_reset(mathbox *m);
void    mb_write(mathbox *m, uint8_t offset, uint8_t data); /* $6080+offset */
uint8_t mb_status(const mathbox *m);                        /* $6040 read   */
uint8_t mb_ylow(const mathbox *m);                          /* $6060 read   */
uint8_t mb_yhigh(const mathbox *m);                         /* $6070 read   */

#endif /* MATHBOX_H */
