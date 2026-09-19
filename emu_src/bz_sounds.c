/* bz_sounds.c - Battlezone's SOUND latch ($1840) played from recorded samples.
 *
 * The latch (bzone.cpp BzoneSounds, HARDWARE_NOTES.md "$1840"; the NMI
 * writes it every tick):
 *   D0 explosion      D1 = which explosion (set: explode1, clear: explode2)
 *   D2 shell fired    D3 = loud / soft: set by the PLAYER's shot (M_NEX0
 *                     L62B0 ORA #LOUDSH) -> fire1; cleared by the ENEMY's
 *                     shot (FIREIT L65DB AND #SOFTSH) -> fire2
 *   D4 engine fast (tank moving)   D5 master sound enable   D6 start lamp
 *   D7 engine on
 *
 * Mapping as AAE's driver does it (same sample names, same channels 2/3/7),
 * with three deliberate differences, all marked "AAE:" below:
 *   1. one-shots start on the RISING edge of D0 / D2 - AAE restarts them on
 *      any latch change while the bit is set (e.g. when the start lamp D6
 *      blinks), which cuts them short / re-fires them;
 *   2. the engine's 1.0 -> 1.66 pitch change RAMPS with an RC-like time
 *      constant, as the PCB's engine oscillator does - AAE jumps (and its own
 *      comment says it is "really missing the ramp"; its set_freq(get_freq *
 *      1.66) also compounds on every latch change);
 *   3. D7 gates the engine (fading), so attract mode is silent like the PCB.
 *
 * Porting to AAE: replace plat_sample_* with sample_*, call bz_sounds_w()
 * from the $1840 write handler and bz_sounds_tick() once per NMI/frame.
 */
#include <math.h>
#include "bz_sounds.h"
#include "platform/bz_platform.h"

/* AAE's channel numbers (drivers/bzone.cpp) */
#define CH_SHELL   2
#define CH_ENGINE  3
#define CH_EXPLODE 7

#define ENGINE_FAST     1.66    /* bzone.cpp: fast rumble = 1.66 x the slow rate */
#define ENGINE_TAU_S    0.35    /* RC time constant of the ramp, seconds (by ear; tune) */
#define ENGINE_FADE_S   0.05    /* D7 on/off fade */

/* AAE's bzonesamples[] order */
enum { S_FIRE1, S_FIRE2, S_ENGINE1, S_ENGINE2, S_EXPLODE1, S_EXPLODE2, S_COUNT };
static const char *const names[S_COUNT] = {
    "fire1", "fire2", "engine1", "engine2", "explode1", "explode2"
};

static int     smp[S_COUNT];
static uint8_t last;             /* previous latch value */
static int     enabled;          /* D5 */
static int     engine_base;      /* engine1's native rate, Hz (0 = not known yet) */
static double  engine_rate = 1.0, engine_gain;

void bz_sounds_init(void)
{
    for (int i = 0; i < S_COUNT; i++) smp[i] = plat_sample_load(names[i]);
    last = 0; enabled = 0; engine_base = 0; engine_rate = 1.0; engine_gain = 0.0;
}

void bz_sounds_w(uint8_t data)
{
    uint8_t rise = (uint8_t)(data & ~last);
    last = data;

    if (data & 0x20) {                                   /* D5: sound on */
        enabled = 1;
        if (smp[S_ENGINE1] >= 0 && !plat_sample_playing(CH_ENGINE)) {
            plat_sample_start(CH_ENGINE, smp[S_ENGINE1], 1);
            plat_sample_set_volume(CH_ENGINE, (int)(engine_gain * 255.0));
            engine_base = plat_sample_get_freq(CH_ENGINE);
            engine_rate = 1.0;
        }
    } else {                                              /* all off */
        enabled = 0;
        plat_sample_stop(CH_ENGINE);
        plat_sample_stop(CH_EXPLODE);
        plat_sample_stop(CH_SHELL);
        return;
    }

    if (rise & 0x01) {                                    /* AAE: any change while set */
        int s = smp[(data & 0x02) ? S_EXPLODE1 : S_EXPLODE2];
        if (s >= 0) plat_sample_start(CH_EXPLODE, s, 0);
    }
    if (rise & 0x04) {
        int s = smp[(data & 0x08) ? S_FIRE1 : S_FIRE2];   /* player : enemy */
        if (s >= 0) plat_sample_start(CH_SHELL, s, 0);
    }
}

/* The engine: rate glides toward 1.66 (D4 set) or 1.0, volume toward D7. */
void bz_sounds_tick(double dt_s)
{
    double target = (last & 0x10) ? ENGINE_FAST : 1.0;   /* AAE: jumps */
    double gain_target = (enabled && (last & 0x80)) ? 1.0 : 0.0;
    int vol;

    if (!enabled || smp[S_ENGINE1] < 0) return;
    engine_rate += (target - engine_rate) * (1.0 - exp(-dt_s / ENGINE_TAU_S));
    engine_gain += (gain_target - engine_gain) * (1.0 - exp(-dt_s / ENGINE_FADE_S));
    if (engine_base > 0)
        plat_sample_set_freq(CH_ENGINE, (int)(engine_base * engine_rate + 0.5));
    vol = (int)(engine_gain * 255.0 + 0.5);
    plat_sample_set_volume(CH_ENGINE, vol);
}
