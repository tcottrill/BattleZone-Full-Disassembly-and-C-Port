/* tests/plat_stub.c - a stub bz_platform.h core for platform verification (M7).
 *
 * Stands in for app_loop.c while it does not exist yet (or while it does not
 * build): implements bz_app_init/step/exit/idle_ms only, so a backend
 * (platform/windows/plat_win.c or platform/headless/plat_headless.c) can be
 * built and exercised entirely on its own.  Draws a rotating hexagon inside
 * the BZ_VIEW_* rectangle (bz_platform.h - never hard-coded here) at several
 * intensities in green, a red strip near the top (the overlay's radar/score
 * band) and a centre dot (a zero-length segment), paced at ~41 Hz like the
 * real GOADD picture rate; pushes a quiet 220 Hz sine to audio.  Holding fire
 * turns the hexagon red, to eyeball that input reaches here at all.
 *
 * tests\build_plat_stub.bat links this with platform\windows\plat_win.c into
 * tests\plat_stub.exe.
 */
#include <math.h>
#include <stdint.h>
#include "../platform/bz_platform.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAKE_RGB(r, g, b) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | 0xFF000000u)

#define VIDEO_HZ           41.0
#define VIDEO_INTERVAL_MS  (1000.0 / VIDEO_HZ)
#define AUDIO_RATE         44100
#define AUDIO_TONE_HZ      220.0
#define AUDIO_AMPLITUDE    1500.0  /* quiet: well under full scale (32767) */

static double next_video_ms;
static double angle;
static double audio_phase;
static int    audio_ok;
static int    fire_latched;

void bz_app_init(void)
{
    next_video_ms = plat_now_ms();
    angle = 0.0;
    audio_phase = 0.0;
    fire_latched = 0;
    audio_ok = (plat_audio_open(AUDIO_RATE) == 0);
}

static void push_audio(double ms_elapsed)
{
    int16_t buf[512];
    int total, frames, i;

    if (!audio_ok) return;
    total = (int)(ms_elapsed / 1000.0 * AUDIO_RATE + 0.5);
    while (total > 0) {
        frames = total > 512 ? 512 : total;
        for (i = 0; i < frames; i++) {
            buf[i] = (int16_t)(sin(audio_phase) * AUDIO_AMPLITUDE);
            audio_phase += 2.0 * M_PI * AUDIO_TONE_HZ / AUDIO_RATE;
            if (audio_phase > 2.0 * M_PI) audio_phase -= 2.0 * M_PI;
        }
        plat_audio_push(buf, frames);
        total -= frames;
    }
}

static void draw_pattern(void)
{
    float cx = (BZ_VIEW_XMIN + BZ_VIEW_XMAX) * 0.5f;
    float cy = (BZ_VIEW_YMIN + BZ_VIEW_YMAX) * 0.5f;
    float rx = (BZ_VIEW_XMAX - BZ_VIEW_XMIN) * 0.35f;
    float ry = (BZ_VIEW_YMAX - BZ_VIEW_YMIN) * 0.35f;
    uint32_t hexcol = fire_latched ? MAKE_RGB(255, 0, 0) : MAKE_RGB(0, 255, 0);
    const int n = 6;
    float px[6], py[6];
    int i;

    plat_video_begin();

    /* the rotating hexagon, edges cycling through intensity 1..7 */
    for (i = 0; i < n; i++) {
        float a = (float)angle + (float)i * (2.0f * (float)M_PI / (float)n);
        px[i] = cx + rx * (float)cos(a);
        py[i] = cy + ry * (float)sin(a);
    }
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        int intensity = 1 + (i % 7);
        plat_video_line(px[i], py[i], px[j], py[j], hexcol, intensity);
    }

    /* the overlay's red strip: a horizontal line near the top of the tube */
    {
        float y = BZ_VIEW_YMAX - (BZ_VIEW_YMAX - BZ_VIEW_YMIN) * 0.08f;
        plat_video_line(BZ_VIEW_XMIN, y, BZ_VIEW_XMAX, y, MAKE_RGB(255, 0, 0), 5);
    }

    /* a centre dot: a zero-length segment, full intensity */
    plat_video_line(cx, cy, cx, cy, MAKE_RGB(0, 255, 0), 7);

    plat_video_present();
}

double bz_app_step(double now_ms)
{
    plat_inputs in;

    plat_input_poll(&in);
    fire_latched = in.fire ? 1 : 0;

    while (now_ms < next_video_ms) {
        int ms = (int)(next_video_ms - now_ms + 0.5);
        if (ms < 1) ms = 1;
        plat_sleep_ms(ms);
        now_ms = plat_now_ms();
    }

    angle += 0.6;
    if (angle > 2.0 * M_PI * 1000.0) angle = fmod(angle, 2.0 * M_PI);

    draw_pattern();
    push_audio(VIDEO_INTERVAL_MS);

    next_video_ms += VIDEO_INTERVAL_MS;
    if (next_video_ms < now_ms) next_video_ms = now_ms + VIDEO_INTERVAL_MS;
    return 0.0;
}

void bz_app_exit(void)
{
    plat_audio_close();
}

double bz_app_idle_ms(void)
{
    double d = next_video_ms - plat_now_ms();
    return d > 0.0 ? d : 0.0;
}
