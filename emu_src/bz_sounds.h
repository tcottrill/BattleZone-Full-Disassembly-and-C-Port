/* bz_sounds.h - the SOUND latch ($1840) -> recorded samples (the board's
 * discrete explosion / shell / engine circuits).  Written against AAE's
 * sample API shape (platform/bz_platform.h "recorded samples") so it drops
 * back into an AAE driver: see bz_sounds.c. */
#ifndef BZ_SOUNDS_H
#define BZ_SOUNDS_H

#include <stdint.h>

void bz_sounds_init(void);          /* load the samples (once, at power-on)         */
void bz_sounds_w(uint8_t data);     /* every write to SOUND $1840 (AAE: BzoneSounds) */
void bz_sounds_tick(double dt_s);   /* once per NMI: the engine's rate ramp          */

#endif /* BZ_SOUNDS_H */
