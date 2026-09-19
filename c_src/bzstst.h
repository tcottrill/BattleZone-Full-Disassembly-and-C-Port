/* bzstst.h - BZSTST.MAC (power-on + self test): PWRON's game path (M7) and
 * the self test, RAMTST onward (M8, PLAN.md).
 */
#ifndef BZSTST_H
#define BZSTST_H

/* PWRON ($7ACC).  Clears RAM and vector RAM, resets POKEY, then either:
 *   - self-test switch OFF (IN0 b4 high): seeds both display-list buffers,
 *     the game state and the high-score table, JSR BATINT, and returns 0 -
 *     the caller then runs main_init()/main_pass() ("JMP MAIN").
 *   - self-test switch ON (IN0 b4 low): enables sound and runs
 *     selftest_boot() (RAMTST onward's one-time RAM/VRAM/ROM tests, which
 *     always find the port's g.ram/g.vram good - see selftest_boot()),
 *     then returns 1 - the caller repeatedly calls selftest_pass() ("JMP
 *     RAMTST" falls, through STOP0/STEST1/STEST3, into the first
 *     MBDIAG/STEST4 pass with no boundary of its own).
 */
int pwron(void);

/* selftest_boot() - RAMTST ($7BCB) through STEST3's tail ($7D3A): the
 * self test's one-time boot sequence, run once when pwron() finds the
 * switch ON.  RAMTST (zero page) and STEST1 (RAM pages 1-3 + all of vector
 * RAM) are moving-inversion RAM tests that cannot find a bad cell over this
 * port's real g.ram/g.vram - always take the "good" exit.  STEST3 computes
 * the 8 ROM checksums (PNTTBL) and sets TEMP3 from whether the diagnostic
 * button was held at power-on ($FF = plain self test, $00 = diagnostic
 * step mode). */
void selftest_boot(void);

/* selftest_pass() - one MBDIAG/STEST4/STEST6-STEST7 loop iteration
 * ("JMP MBDIAG" at STEST7_45/STEST7_70's loop): re-run the Mathbox
 * diagnostic (MBDIAG), rebuild the current self-test picture (STEST4,
 * STEST6 or the DIATST step pattern) and start the AVG, scan the switches
 * (STEST7) or step the diagnostic screen.  Returns 1 where the ROM does
 * "JMP PWRON" (STEST7_50: the self-test switch was turned off; STEST7_75's
 * L7FB6: the diag button was pressed on the spinner screen) - the caller
 * must call pwron() again (fresh boot, no recursion here) - or 0 to call
 * selftest_pass() again. */
int selftest_pass(void);

#endif  /* BZSTST_H */
