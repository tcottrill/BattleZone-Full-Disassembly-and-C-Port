/* bzsoun.h - BZSOUN.MAC ($7864-$7A18): the POKEY sound-effect sequencer.
 *
 * Two public entries, both JSR'd from elsewhere (../disasm/CALLING_NOTES.md
 * section 5): SNDON starts an effect; MODSND is ticked once per NMI to
 * step every channel's envelope and write POKEY.
 */
#ifndef BZSOUN_H
#define BZSOUN_H

#include <stdint.h>

/* SNDON ($7985): start a sound effect on whichever of the 4 POKEY channels
 * PNTRS lists a nonzero pointer for.
 * In: a = the effect's SNDWRD bit (SUPBON/SAUSND/DISINT/BLKSND/WARNG/BONER/
 *     BOING/RBEEP - state_defs.h K_* constants), or 0 to use x as the sound
 *     index directly (SNDON's own fallback path - CALLING_NOTES.md flags
 *     this as never actually exercised by this ROM: every real call site
 *     passes a nonzero a). x = the fallback sound index (0-7), only read
 *     when a == 0.
 * SNDON's own internal "LDA $0103,X" stack peek (L798B) always reads back
 * the A it was entered with (proven from the PHA/TXA/PHA/TYA/PHA/TSX offsets
 * - see NOTES_nmi_sound_coin.md) - translated here simply as using the
 * entry a, per the module brief.
 * No return; A/X/Y are restored to their entry values on real hardware
 * (PLA/TAY/PLA/TAX/PLA) - nothing for a caller to observe. */
void sndon(uint8_t a, uint8_t x);

/* MODSND ($79B9): continue every previously-started sound one frame tick.
 * No inputs (operates on the fixed 4-channel POINT/FRAMES/COUNT/CURRENT
 * state SNDON set up), no return. Channel 0 ("channel 1" in Atari's
 * comment) going idle shuts off all 4 channels this same call. */
void modsnd(void);

#endif  /* BZSOUN_H */
