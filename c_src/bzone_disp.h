/* bzone_disp.h - BZONE display module (agent "display"): the AVG display-list
 * builders - logo, windshield cracks, high-score table/entry, mountains/
 * volcano, DRAW (object edge-list walker), radar, shell position update,
 * message/number output, INFO.  See NOTES_disp.md for status and judgement
 * calls; SIGNATURES.md "BZONE display" section is the registered contract.
 *
 * One C function per Atari label that is a routine entry (JSR/JMP target,
 * dispatch-table entry, or fall-through entry other code jumps to), per
 * TRANSLATING.md.  Internal fall-through-only helpers (D_VCT1, MSTART's
 * language legs' shared tail, etc.) are file-static in bzone_disp.c and not
 * declared here.
 */
#ifndef BZONE_DISP_H
#define BZONE_DISP_H

#include <stdint.h>
#include "regs.h"
#include "bzone_game.h"   /* pass_signal, for wnshld_body()/ckscor_body()/hiscre_body()/
                            * distbl_body() below - see bzone_game.h's own comment
                            * ("Integration fix... trampoline") for the full design;
                            * NOTES_integration.md "CRITICAL fix... open item 1 resolved". */

/* ---- BATTLE ZONE logo / windshield / high score --------------------------- */
void bigwnd(void);  /* BIGWND  $51D9 */
void wnshld(void);  /* WNSHLD  $51E9 */
void battle(void);  /* BATTLE  $5268 */
void batint(void);  /* BATINT  $531F */
void ckscor(void);  /* CKSCOR  $5344 */
void hiscre(void);  /* HISCRE  $53C5 */

/* _body(): the real translated code, returning a pass_signal instead of
 * tail-calling b_end()/bz_main()/one another - see bzone_game.h.  The
 * plain wnshld()/ckscor()/hiscre()/distbl() above each just run their own
 * _body() once and discard the signal (unchanged call signature for every
 * existing caller, including tests\lockstep.c's adapters). */
pass_signal wnshld_body(void);
pass_signal ckscor_body(void);
pass_signal hiscre_body(void);
pass_signal distbl_body(void);
void letchk(void);                      /* LETCHK  $5465  (A is read only to feed the POTGO strobe write, whose
                                          * value hardware discards - not modelled as a parameter; see NOTES_disp.md) */
void distbl(void);       /* DISTBL  $549B */
void dspalp(uint8_t a);  /* DSPALP  $556A */
void finish(void);       /* FINISH  $5574 */

/* ---- mountains / volcano ---------------------------------------------------*/
void mounts(void);  /* MOUNTS  $57C8 */
void volcno(void);  /* VOLCNO  $58BA */

/* ---- DRAW: object edge-list walker (DTAB push/RTS dispatch -> switch) ------ */
void draw(uint8_t a);    /* DRAW    $5C5C  ENTRY A = object # */
void dovect(uint8_t a);  /* DOVECT  $5D11  ENTRY A = brightness */
void d_next(void);       /* D_NEXT  $5D41  (DRAW's loop back-edge; exposed for lockstep, not a real JSR target) */

/* ---- radar / shells --------------------------------------------------------*/
void dradar(void);  /* DRADAR  $6AE9 */
void shupdt(void);  /* SHUPDT  $6C66 */

/* ---- messages / info -------------------------------------------------------*/
void msgs(uint8_t x);  /* MSGS    $6C98  ENTRY X = message # * 2 */
void info(void);       /* INFO    $6D2E */

/* ---- score/number display (BZSTST address range, used by display) --------- */
void digt2s(uint8_t a);                    /* DIGT2S  $7B9E  ENTRY A = zero-page addr of 2-byte number */
void digtys(uint8_t a, uint8_t y);         /* DIGTYS  $7BA0  ENTRY A = zp addr, Y = digit-pair count */
void digits(uint8_t a, uint8_t y, int c);  /* DIGITS $7BA1  ENTRY A = zp addr, Y = pair count, C = suppress */

#endif  /* BZONE_DISP_H */
