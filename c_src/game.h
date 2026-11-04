/* game.h - entry points of the translated Battlezone program ROM.
 *
 * One C function per Atari label, lowercased ($ -> s_), ROM address in a
 * comment.  Inputs become parameters and results become return values
 * (TRANSLATING.md).  Modelled on the
 * Tempest port's game.h (C:\Source2026\Tempest Dissasembly\c_src\game.h).
 *
 * Status: VGUT (M1) translated and lockstep-verified.
 *
 * No CK()/checkpoint machinery yet (Tempest's game.h has one, added at its
 * M5 for IRQ placement inside translated code) - VGUT has no fall-through
 * hazard that needs one: every external call site into this module is a
 * genuine JSR/JMP at an entry point lockstep already hooks, and VGUT itself
 * calls nothing outside the module.  Add CK() here if a later milestone's
 * routine needs it (see NOTES_m1.md).
 */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>

/* ---- VGUT ($7A19-$7AC9) - translated, M1 --------------------------------
 * Owned by vgut.c; the canonical prototypes (with the calling contract of
 * each) are in vgut.h - the shared header other translation agents include
 * per SIGNATURES.md.  Re-exported here too so code that already includes
 * game.h (this port's usual per-module include, PLAN.md section 1) sees
 * them without an extra include. */
#include "vgut.h"

#endif  /* GAME_H */
