/* bz_platform.h - the platform contract for the Battlezone C port (M7).
 *
 * Adapted from the Tempest port's platform/tempest_platform.h.  The core (the
 * translated modules + avg.c + mathbox.c + c012294.c + app_loop.c, the game
 * seam) is platform-agnostic and calls ONLY these functions for video, input,
 * audio and time.  Each backend under platform/<name>/ implements the whole
 * contract in plain C; the build links exactly one backend per binary.
 *
 * Backends:
 *   headless/  plat_headless.c: injectable inputs, captured segments, a
 *              simulated clock.  tests\bz_headless.exe.
 *   windows/   plat_win.c: window, GL beam renderer, audio, keyboard/joystick
 *              -> bz_win.exe (build_win.bat).
 *
 * Hardware truth (which port bit means what, polarity, POKEY clocking, NMI
 * timing) lives in app_loop.c.  Key bindings are backend policy.
 */
#ifndef BZ_PLATFORM_H
#define BZ_PLATFORM_H

#include <stdint.h>

/* ---- lifecycle --------------------------------------------------------- */
int  plat_init(void);  /* 0 = ok */
void plat_shutdown(void);

/* ---- video: the monochrome segment sink -----------------------------------
 * app_loop.c walks the AVG display list (avg.c) each time the NMI strobes
 * GOADD (every 6 NMIs, ~41 Hz) and hands every LIT beam movement to the
 * backend between begin and present.  Dark moves are not sent.
 *
 * Coordinates: AVG space, exactly avg.c's - origin = the beam centre (CNTR),
 * +x right, +y up, 1.0 = one LSB of a 13-bit VCTR delta at scale 1.0.  The
 * Battlezone monitor is mounted normally (no rotation, no flip).  The
 * visible tube is BZ_VIEW_* below; anything outside it is off-tube and the
 * backend must clip it (values measured with tests\bz_smoke.exe - app_loop.c's
 * extent stats; a backend should read them from here,
 * never hard-code its own).  The core has already applied the AVG's clip window
 * (the horizon window BIGWND arms), so segments arrive pre-clipped to it.
 *
 * rgb: R | G << 8 | B << 16 | 0xFF << 24 (colordefs.h MAKE_RGB order).  The
 *   core picks it: the upright cabinet's overlay film (red strip at the top
 *   for the radar/score, green below) is applied by the core, so a backend
 *   just draws the colour it is given.
 * intensity: 1..7 (the vector's own Z field). A zero-length segment is a dot.
 */
/* The tube: the self test's first DIATST pattern is a corner-to-corner
 * diagonal (BZSTST: "VCTR -512,-384" then "1023,767"), which at the scale
 * the AVG carries lands exactly on -256,-192 .. +256,+192 (tests\bz_smoke.exe
 * --test --diag 40).  The game stays inside it (x -253..253, y ..191; only
 * explosion debris goes below -192, off the tube as on the cabinet). */
#define BZ_VIEW_XMIN  (-256.0f)
#define BZ_VIEW_XMAX  ( 256.0f)
#define BZ_VIEW_YMIN  (-192.0f)
#define BZ_VIEW_YMAX  ( 192.0f)

void plat_video_begin(void);
void plat_video_line(float x0, float y0, float x1, float y1,
                     uint32_t rgb, int intensity);
void plat_video_present(void);

/* ---- input ---------------------------------------------------------------
 * Polled by app_loop.c once per MAIN pass (~15.6 Hz) and folded into the
 * hardware ports:
 *   left_fwd left_rev right_fwd right_rev
 *               the two tread levers, each a pair of momentary switches
 *               (POKEY ALLPOT b3 b2 b1 b0, active high).  A backend may also
 *               offer a single-stick mode that synthesizes these (e.g. up =
 *               both forward, left = left reverse + right forward).
 *   fire start1 start2
 *               ALLPOT b4 b5 b6, active high.
 *   coin1 coin2 slam test
 *               IN0 $0800 b0 b1 b3 b4, 1 = active (the core inverts: the
 *               lines are active low).  test = the self-test switch.
 *   diag        IN0 b5, diagnostic-step button, 1 = active (the core
 *               inverts: active low on the wire).  Held at power-on to
 *               enter BZSTST's diagnostic-step mode; pressed thereafter to
 *               advance a step / exit back to PWRON (bzstst.c stest7()).
 *   quit        host affordance, never reaches the machine.
 * All fields are 0/1. */
typedef struct {
    uint8_t left_fwd, left_rev, right_fwd, right_rev;
    uint8_t fire, start1, start2;
    uint8_t coin1, coin2, slam, test, diag;
    uint8_t reset;  /* host: the board's RESET button (edge; PWRON) */
    uint8_t quit;
} plat_inputs;

void plat_input_poll(plat_inputs *in);

/* Option switches, raw as the board reads them.  A backend returns the ini's
 * values, or the bzone.cpp defaults: OPTION ($0A00, DSW0) = $15 (3 lives,
 * missile tank at 10000, bonus 15k/100k, English), OPTON2 ($0C00, DSW1) =
 * $02 (1 coin 1 play). */
uint8_t plat_dsw_option(void);
uint8_t plat_dsw_opton2(void);

/* ---- audio ---------------------------------------------------------------
 * The core clocks the POKEY (c012294.c) on the machine timeline (1.512 MHz,
 * 6144 cycles per NMI), mixes in the discrete sounds driven by the SOUND
 * latch ($1840: explosion, shell, engine), and pushes ONE mono signed 16-bit
 * stream at the rate it opened, one NMI tick at a time (~179 frames per push
 * at 44100 Hz).  plat_audio_open returns 0 when the stream is up, nonzero
 * for no audio (the core keeps clocking the chips but pushes nothing). */
int  plat_audio_open(int sample_rate);
void plat_audio_push(const int16_t *pcm, int frames);
void plat_audio_close(void);

/* ---- recorded samples (the discrete sounds on the SOUND latch) -----------
 * The same calls, with the same meaning, as AAE's sample API (the Windows
 * backend is AAE's own mixer.c), so bz_sounds.c ports back to an AAE driver
 * by dropping the "plat_" prefix.  A backend with no sample support
 * implements load as "return -1" and the rest as no-ops.
 *   plat_sample_load(name)   load the wav for a logical sample name (AAE's
 *                            bzonesamples[] names: "fire1", "engine1", ...);
 *                            the backend maps the name to a file (ini).
 *                            Returns the sample number, -1 if missing.
 *   channels 0..15; freq in Hz, the sample's native rate = normal pitch;
 *   volume 0..255. */
int  plat_sample_load(const char *name);
void plat_sample_start(int chan, int smp, int loop);
void plat_sample_stop(int chan);
int  plat_sample_playing(int chan);
void plat_sample_set_freq(int chan, int freq);
int  plat_sample_get_freq(int chan);
void plat_sample_set_volume(int chan, int vol);

/* ---- time ---------------------------------------------------------------- */
double plat_now_ms(void);      /* monotonic, any epoch */
void   plat_sleep_ms(int ms);  /* scheduling hint; may return early */

/* ---- misc ---------------------------------------------------------------- */
void plat_status_text(const char *s);  /* ~1/s pacing stats; may be a no-op */

/* ---- app hooks: what a backend's main loop calls (app_loop.c) ------------
 *   bz_app_init()   power on: chips, PWRON's game path (RAM clear, POKEY
 *                   reset, high-score table, BATINT), up to MAIN.
 *   bz_app_step()   poll inputs, run ONE MAIN pass (main_pass()); the pass
 *                   blocks inside its frame wait until its NMIs are due on
 *                   plat_now_ms(), presenting a picture at every GOADD and
 *                   pushing audio at every NMI.  Returns 0.0.
 *   bz_app_exit()   close audio. */
void   bz_app_init(void);
double bz_app_step(double now_ms);
void   bz_app_exit(void);

/* Inside plat_sleep_ms: the time to the next NMI the core is idling for, ms.
 * A backend must not block longer. */
double bz_app_idle_ms(void);

#endif  /* BZ_PLATFORM_H */
