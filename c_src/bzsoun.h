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
 * its PNTRS row names.
 * request = the effect's SNDWRD bit (SUPBON/SAUSND/DISINT/BLKSND/WARNG/BONER/
 *     BOING/RBEEP - state_defs.h K_* constants); if several bits are set the
 *     highest wins.  0 starts nothing (Atari's "use X as the index" path
 *     actually reads the stack pointer, past the table); no call site in
 *     this ROM passes 0. */
void sndon(uint8_t request);

/* MODSND ($79B9): continue every previously-started sound one frame tick.
 * No inputs (operates on the fixed 4-channel POINT/FRAMES/COUNT/CURRENT
 * state SNDON set up), no return. Channel 0 ("channel 1" in Atari's
 * comment) going idle shuts off all 4 channels this same call. */
void modsnd(void);

#endif  /* BZSOUN_H */
