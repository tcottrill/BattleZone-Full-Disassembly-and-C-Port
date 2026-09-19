/* coin65.h - COIN65/TC65: the coin/credit routine (MOOLAH, assembled inline
 * in BZONE.MAC at $56C3-$57C7 via ".INCLUDE TC65").
 *
 * One public entry: MOOLAH, JSR'd once per NMI tick from bzone_nmi.c's
 * nmi() (../disasm/CALLING_NOTES.md section 6). S_DETCT/S_BONUS/S_EXTB
 * (a.k.a. S_CNVRT)/S_EXT are reached only by MOOLAH's own fall-through and
 * internal JMP/loop, never JSR'd from anywhere else in this ROM - they live
 * as the loop and static helpers inside coin65.c, not as separate entries
 * here (same "internal fall-through -> static helper" convention vgut.c
 * already established for VGUT.MAC's chain).
 *
 * This is an EARLIER COIN65 than Tempest's/Space Duel's: no `$BC`
 * bonus-coin cell - S_BONUS increments $CNCT (S_CNCT) directly (see
 * NOTES_nmi_sound_coin.md).
 */
#ifndef COIN65_H
#define COIN65_H

/* MOOLAH ($56C3): detect coins on the three mechs (X=2 right, 1 centre,
 * 0 left), debounce, apply mech multipliers and the bonus-adder, convert
 * unit-coins to credits, and run the electro-mechanical coin-counter pulse
 * timers. No register inputs; no documented register outputs (writes
 * S_S_CRDT/S_LMTIM/S_PSTSL/S_CNSTT/S_CNCT/S_BCCNT/S_CCTIM as side effects).
 * Runs entirely in binary mode (nmi() CLDs before calling this). */
void moolah(void);

#endif  /* COIN65_H */
