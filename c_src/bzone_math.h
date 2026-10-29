/* bzone_math.h - BZONE maths and 3D pipeline (owner: agent "maths",
 * bzone_math.c).
 *
 * Routines: ROTATE ($5946)/ROTPNT ($5B1F)/WAIT ($5B80) - the translate-and-
 * rotate pass that fills PTBLX2/PTBLY2/PTBLO2; PNTPUT ($5D4A)/KLUDGE
 * ($5E05)/BOUND ($5E44) - per-object point projection; COS/SIN/SIN1
 * ($5E4B/$5E4E/$5E63), COSXTR/SINXTR ($5EF9/$5F17) - the sin/cos tables and
 * their extrapolation; ABSTMP ($6149); TRACK ($6810) and its TTAB dispatch
 * targets T_0/T_1/T_2/T_3 ($68D5/$68C6/$68CB/$68D0).
 *
 * Registered in SIGNATURES.md's "BZONE maths and 3D pipeline" section.
 * Full ENTRY/EXIT documentation and the Lxxxx-commented translation are in
 * bzone_math.c; this header only states the calling contract.  See
 * NOTES_math.md for judgement calls and open questions.
 */
#ifndef BZONE_MATH_H
#define BZONE_MATH_H

#include <stdint.h>
#include "regs.h"

/* Integration fix (2026-09-18, NOTES_integration.md): COS/SIN are named
 * `cos_`/`sin_` below, not the bare `cos`/`sin` SIGNATURES.md's naming rule
 * would otherwise give them - cl.exe reserves those two bare identifiers as
 * compiler intrinsics (the C library math functions) unconditionally: even
 * a mere declaration with a different signature is a hard error under
 * /O2 (build_all.bat's own flags) - "C2371: redefinition; different basic
 * types" - not just a warning, and no pragma disables it (`#pragma
 * function` only stops the compiler inlining an ALREADY-double-typed
 * definition, it does not let the name be repurposed).  This was in fact
 * anticipated: the "game" module's own TODO(seam) notes already used
 * `sin_`/`cos_` for these two (NOTES_game.md) before this header settled on
 * the plain names - this fix just makes the header (and bzone_math.c, and
 * every caller) match that. */

/* WAIT ($5B80): wait for the Mathbox, return the low byte of whatever
 * function was last started.  No inputs.  Callers that also need the high
 * byte must read hw_mb_yhigh() themselves right after (CALLING_NOTES.md
 * section 3: WAIT hands back only the low byte). */
uint8_t bz_mb_wait(void);

/* ROTATE ($5946): scan the playfield/tank/robot/shell/saucer tables,
 * translate each centre point by the tank's own position and rotate it
 * into view space via ROTPNT, keeping only objects in the field of view.
 * No inputs.  Fills PTBLX2/PTBLY2/PTBLO2 (RAM) as its only outputs; no
 * register is meaningful to the caller (MAIN calls it for effect only). */
void rotate(void);

/* ROTPNT ($5B1F): rotate one world point (already presented to the Mathbox
 * via ACOS/BSIN/XMATRX/YMATRX/YSTRT1 by the caller) and, if it passes the
 * view-frustum clip, append it to PTBLX2/PTBLY2.
 * In: y = destination index into PTBLX2/PTBLY2 (2 bytes/object).
 * Returns: the new y - advanced by 2 if the point was kept, unchanged if
 * discarded (CALLING_NOTES.md section 3: "the caller must track 'was a
 * point written' by comparing Y before/after" - returning the new y IS
 * that comparison). */
uint8_t rotpnt(uint8_t y);

/* PNTPUT ($5D4A): project one visible object's 3-D point table (found via
 * OBJPNT) into 2-D screen deltas in PNTTBL, using SIN/COS of the relative
 * bearing and the Mathbox's combined rotate-and-divide (YHSP) plus a
 * separate Z-adjusted divide (KLUDGE + SZXD) per point.
 * In: x = index into PTBLO2/PTBLX2/PTBLY2 (the caller's SAVE0/running
 * object index - Atari's header calls it "X=POINTER TO CENTER POINT").
 * No return; fills PNTTBL/TBLPTR (RAM). */
void pntput(uint8_t x);

/* KLUDGE ($5E05): add the Z-axis offset for explosion-piece objects into
 * the pending Y-divide dividend (DDEND), apply BOUNCE (collision view
 * shake) if any, then load the Mathbox's Z-high word (DIVOUT/DIVOUT+1) and
 * start the SZXD divide (ZDIV).  No inputs (reads SAVE1/PTBLO2/DDEND/
 * EXPOSZ/BOUNCE); no return - caller (PNTPUT) reads the result via WAIT. */
void kludge(void);

/* BOUND ($5E44): halve BOUNCE (the collision-bounce view-shake amount)
 * toward zero, once per call.  No inputs, no return. */
void bound(void);

/* COS ($5E4B): COS(a) = SIN(a + $40) (quarter-turn phase shift), then falls
 * into SIN.  In: a = angle (0..$FF = 0..360 degrees).  Returns: r.a = low
 * byte, r.x = high byte of the cosine (both consumed by every real caller -
 * ROTATE, PNTPUT, COSXTR).  Named `cos_` (trailing underscore), not the
 * bare ROM-label-derived `cos` - see the integration-fix comment above. */
regs cos_(uint8_t a);

/* SIN ($5E4E): sine of an 8-bit angle. In: a = angle.  Returns: r.a = low
 * byte, r.x = high byte.  Named `sin_` for the same reason as `cos_` above. */
regs sin_(uint8_t a);

/* SIN1 ($5E63): SIN(a) for 0 <= a < $80 (folds the second quadrant via
 * EOR #$7F).  In: a.  Returns: r.a = low byte, r.x = high byte, read from
 * the 65-entry table at its real ROM address $5E77 (SIN1_99 in this
 * disassembly's naming) - CALLING_NOTES.md section 3: the ROM source
 * indexes this table by a self-relative local label, not a plain symbol;
 * a C port has no equivalent and must use the real address. */
regs sin1(uint8_t a);

/* COSXTR ($5EF9): extrapolate COS between the SIN1 table's angle steps.
 * In: x = angle, a = division of angle (only bit 0 used: 0 or 1).
 * Returns: r.a = low byte, r.x = high byte.  Also writes GARBAG (RAM
 * scratch - Atari's own header note, "GARBAG"). */
regs cosxtr(uint8_t a, uint8_t x);

/* SINXTR ($5F17): SIN counterpart of COSXTR - same entry/exit shape,
 * shares the XTRAP interpolation tail. */
regs sinxtr(uint8_t a, uint8_t x);

/* ABSTMP ($6149): TEMP1/TEMP1+1 := |TEMP1/TEMP1+1| (16-bit absolute value).
 * No inputs/return (RAM in, RAM out; no caller reads A afterward). */
void abstmp(void);

/* TRACK ($6810): angle from the robot tank to the player tank via the
 * Mathbox divide and the ARCTAN table, quadrant-corrected through the
 * T_0..T_3 dispatch (TTAB, $68BE - CALLING_NOTES.md section 3).
 * No inputs (reads TPOSX/TPOSY for both tanks). Returns: the new angle
 * (Atari's header: "(A)=NEW ANGLE TO DIRECT ROBOT"). Also leaves
 * TEMP1/TEMP2 as scratch (documented: "TEMP1=ABS VALUE(Y), TEMP2=ABS
 * VALUE(X)" - not meaningful outputs to any caller, just where the ROM
 * left its scratch words). */
uint8_t track(void);

/* T_0/T_1/T_2/T_3 ($68D5/$68C6/$68CB/$68D0): TRACK's TTAB quadrant-fix-up
 * dispatch targets (push-address-then-RTS, CALLING_NOTES.md section 0/3).
 * Reached only via track()'s internal switch in this port, exposed here
 * (as vgut.c/vgut.h expose VGUT's own unreachable-by-the-game routines)
 * for direct lockstep coverage.  In/out: a = the raw ARCTAN-table angle in,
 * the quadrant-corrected angle out; x/y untouched. */
uint8_t t_0(uint8_t a);
uint8_t t_1(uint8_t a);
uint8_t t_2(uint8_t a);
uint8_t t_3(uint8_t a);

#endif  /* BZONE_MATH_H */
