/* vgut.h - VGUT ($7A19-$7AC9): vector generator utility subroutines.
 *
 * Owner: M1 agent (vgut.c).  Registered in SIGNATURES.md's VGUT section -
 * other modules include this header to call these routines; do not
 * re-prototype them locally.
 *
 * Inputs become parameters and a result becomes the return value;
 * everything else (VGLIST, VGBRIT, XCOMP, the display buffer itself) goes
 * through RAM (state_defs.h) / vector RAM exactly as the ROM does it.
 *
 * Full ENTRY/EXIT/USES documentation and the Lxxxx-commented translation are
 * in vgut.c; this header only states the calling contract.
 */
#ifndef VGUT_H
#define VGUT_H

#include <stdint.h>

/* VGRTSL ($7A19): append RTSL ($C0,$C0) to the display buffer at VGLIST.
 * No inputs. Dead code in this ROM (never called) - translated anyway. */
void vgrtsl(void);

/* VGHALT ($7A1D): centre the beam (VGCNTR), then append HALT ($20,$20).
 * No inputs. */
void vghalt(void);

/* VGHEXZ ($7A29): display one BCD digit with zero suppression.
 * digit: low 4 bits used; suppress = 1 to show a leading zero as blank.
 * Returns the suppress flag for the next digit in a chain - 1 means "still
 * suppressing" (a zero was shown as blank), 0 means a non-zero digit showed
 * (from here on nothing more is suppressed). Reads/writes VGLIST, VGBRIT
 * untouched. */
int vghexz(uint8_t digit, int suppress);

/* VGHEX ($7A2F): display one BCD digit, no suppression (digit: low 4 bits
 * used).  Returns the suppress flag for a following digit, always 0. */
int vghex(uint8_t digit);

/* VGJMPL ($7A49): append a JMPL word to CPU address a:x (MSB:LSB), 13-bit
 * AVG address space. No return. Dead code in this ROM (never called). */
void vgjmpl(uint8_t a, uint8_t x);

/* VGJSRL ($7A5A): append a JSRL word to CPU address a:x (MSB:LSB).
 * No return. Used by MAIN, mountain rendering, D_SPCL, the radar and
 * BZSTST. */
void vgjsrl(uint8_t a, uint8_t x);

/* VGSTA1 ($7A61): VGSTAT with the brightness taken from VGBRIT (RAM) instead
 * of a parameter. In: a = the hi/low + in/out + enable flag bits.
 * Dead code in this ROM (every real caller already has the brightness in Y
 * and calls VGSTAT directly). */
void vgsta1(uint8_t a);

/* VGSTAT ($7A63): append a STAT word (intensity latch, or arm one corner of
 * the clip window - see ../disasm/CALLING_NOTES.md section 1).
 * In: a = the hi/low + in/out + enable flag bits (0 = intensity-only latch);
 *     y = the brightness / low byte. No return. */
void vgstat(uint8_t a, uint8_t y);

/* VGCNTR ($7A6A): append CNTR ($40,$80) - beam to the AVG's centre.
 * No inputs, no return. */
void vgcntr(void);

/* VGADD2 ($7A6E): append two raw bytes (a, then x) to the display buffer.
 * In: a = first byte, x = second byte. No return. */
void vgadd2(uint8_t a, uint8_t x);

/* VGADD ($7A76): VGLIST += y + 1 (advance the display-buffer pointer past
 * bytes already stored by the caller). In: y. No return. */
void vgadd(uint8_t y);

/* VGSCA1 ($7A81): VGSCAL with the linear scale forced to 0 (full size).
 * In: a = the binary scale (0..7). No return. */
void vgsca1(uint8_t a);

/* VGSCAL ($7A83): append a SCAL word.
 * In: a = the binary scale (0..7); y = the linear scale (0 = full size,
 * $FF = 1/256). No return. */
void vgscal(uint8_t a, uint8_t y);

/* VGDOT ($7A8A): a zero-length vector (a dot) at the current beam position.
 * In: a = the intensity (becomes VGBRIT). No return. */
void vgdot(uint8_t a);

/* VGVTR ($7A8E): "short form" VGVCTR call - move the beam and set the
 * brightness in one call.
 * In: a = delta X/4 (signed, -$80..+$7F); x = delta Y/4 (signed);
 *     y = the new brightness (stored to VGBRIT). No return. */
void vgvtr(uint8_t a, uint8_t x, uint8_t y);

/* VGVTR1 ($7A90): as VGVTR but leaves VGBRIT alone (the caller already set
 * it). In: a = delta X/4, x = delta Y/4. No return. */
void vgvtr1(uint8_t a, uint8_t x);

/* VGVTR2 ($7AAB): position the beam from XCOMP (the caller has already
 * filled XCOMP..XCOMP+3, e.g. from CURNTX/CURNTY). No inputs, no return. */
void vgvtr2(void);

/* VGVCTR ($7AAD): append a VCTR word from four zero-page cells at x (x lsb,
 * x msb, y msb, y lsb - the ROM's own comment order); the top 3 bits of
 * VGBRIT become the intensity.
 * In: x = the zero-page base address (every caller in this ROM passes
 * XCOMP, i.e. $04, but the routine - and this translation - is generic).
 * No return. */
void vgvctr(uint8_t x);

#endif  /* VGUT_H */
