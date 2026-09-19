/* hw.h - the hardware seam for the Battlezone C port.
 *
 * Translated code touches hardware ONLY through these calls (same rule as
 * the Tempest / Space Duel ports).  Implementations:
 *   app_loop.c        the game (later milestone: playable Windows build)
 *   tests/lockstep.c  (later milestone) replays, in order, the I/O the real
 *                     ROM did on tests/refrun.c's hardware model
 * Register names are the defines file's (state_defs.h / bzone_defines.asm).
 *
 * Map (HARDWARE_NOTES.md; PLAN.md section 1 "hardware seam"):
 *   IN0     $0800 R   coins b0/b1, self-test b4, diag step b5, VG HALT b6,
 *                     3 kHz clock b7 (HALT/$COINA/$LAM alias the same byte)
 *   OPTION  $0A00 R   DSW0: lives, missile threshold, bonus life, language
 *   OPTON2  $0C00 R   DSW1: coinage, coin multipliers, bonus coins
 *   EMCNT   $1000 W   electro-mechanical coin-counter pulses
 *   GOADD   $1200 W   start the AVG - the data byte IS the vector-RAM buffer
 *                     page ($20 or $28; see hw.c / NOTES_m0.md "GOADD")
 *   WTCHDG  $1400 W   clear the watchdog (strobe; any value)
 *   VSTOP   $1600 W   halt/reset the AVG (strobe; any value)
 *   Mathbox $1800/$1810/$1818 R, $1860-$187F W - hw_mb_*
 *   POKEY   $1820-$182F R/W - hw_pokey_read/write; ALLPOT ($1828 R) carries
 *                     the tread switches, fire and start buttons (not an
 *                     analogue pot - HARDWARE_NOTES.md "POKEY ($1820-$182F)")
 *   SOUND   $1840 W   discrete-sound/lamp latch
 */
#ifndef HW_H
#define HW_H

#include <stdint.h>

/* ---- inputs --------------------------------------------------------------
 * IN0 bits: D0 coin1, D1 coin2, D2-D3 unused, D4 self-test (1 = off/normal
 * play), D5 diag step, D6 VG HALT (1 = AVG done), D7 3 kHz clock. */
uint8_t hw_in0(void);
uint8_t hw_option(void);  /* OPTION $0A00 (DSW0) */
uint8_t hw_opton2(void);  /* OPTON2 $0C00 (DSW1) */

/* ---- POKEY $1820-$182F --------------------------------------------------- */
uint8_t hw_pokey_read(uint8_t reg);  /* reg = addr & 0x0F: ALLPOT $08, PRAND $0A */
void    hw_pokey_write(uint8_t reg, uint8_t val);

/* ---- Mathbox $1800 status, $1810/$1818 result, $1860-$187F start -------- */
uint8_t hw_mb_status(void);
uint8_t hw_mb_ylow(void);
uint8_t hw_mb_yhigh(void);
void    hw_mb_write(uint8_t offset, uint8_t val);  /* $1860+offset */

/* ---- outputs -------------------------------------------------------------
 * GOADD/VSTOP/WTCHDG are strobes on real hardware in the sense that ANY
 * write acts (BZONE.MAC: "STA GOADD"/"STA VSTOP"/"STA WTCHDG" never reads
 * the value back), but GOADD's WRITTEN BYTE is not ignored: it is the
 * vector-RAM buffer's page ($20 or $28), matching VGLIST's own value at
 * that point (see NOTES_m0.md).  hw_goadd() therefore takes the value. */
void hw_emcnt(uint8_t v);     /* EMCNT   $1000 */
void hw_goadd(uint8_t page);  /* GOADD   $1200: start the AVG at page<<8 */
void hw_wtchdg(void);         /* WTCHDG  $1400: clear the watchdog */
void hw_vstop(void);          /* VSTOP   $1600: halt the AVG */
void hw_sound(uint8_t v);     /* SOUND   $1840: discrete-sound/lamp latch */

/* ---- frame wait ------------------------------------------------------------
 * MAIN's own frame-sync spin (BZONE.MAC $5000: "LSR SYNC / BCC MAIN") is not
 * a hardware register access - SYNC is an ordinary RAM cell (state_defs.h)
 * that NMI increments every 16 ticks and MAIN itself shifts one bit out of,
 * looping until a 1 bit falls out.  Added to this seam (not a RAM macro
 * call directly in bzone_game.c) because a real build's implementation
 * (app_loop.c) also blocks on the real, wall-clock-timed frame boundary -
 * see Tempest's hw.h/alexec.c for the template this follows.  A verification
 * build (tests\lockstep.c) performs the SAME SYNC-shift RAM effect (so RAM
 * compares stay exact) without any real blocking. */
void hw_wait_frame(void);  /* MAIN's SYNC spin ($5000-$5002) */

/* MAIN_5's "LDX BUFRDY / BEQ MAIN_5" ($5007-$5009): spins on a RAM cell only
 * the NMI sets, AFTER JSR VOLCNO (so VOLCNO runs before the wait, as on the
 * board).  The game build services NMIs until BUFRDY != 0, charging the
 * loop's cycles; a verification build does nothing (the ROM's own BUFRDY is
 * already in the snapshot). */
void hw_wait_bufrdy(void);

/* ---- cycles ----------------------------------------------------------------
 * CYC(n): the 6502 cycles the ROM spends on the instructions a statement (or
 * block) translates - base cycles, +1 on the path where a conditional branch
 * is taken, no page-cross penalty (the model tests\refrun.c/lockstep.c run
 * the ROM with; tools\cyc.py prints it).  Each routine charges every
 * instruction it executes from its entry through its own RTS/RTI/tail JMP;
 * a JSR's 6 cycles are charged by the caller.  The game build (app_loop.c)
 * advances machine time and takes an NMI that falls due at the next charge;
 * tests\lockstep.c sums a call's charges and compares them with the ROM's
 * own cycle count for that call.  See TRANSLATING.md "Cycle counting". */
void hw_cycles(unsigned n);
#define CYC(n) hw_cycles(n)

#endif  /* HW_H */
