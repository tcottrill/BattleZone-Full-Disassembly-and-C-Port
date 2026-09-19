/* plat_headless.c - Battlezone C port: headless backend (see plat_headless.h).
 *
 * The whole bz_platform.h contract with no window, no sound device and no
 * real clock.  Adapted from the Tempest port's plat_headless.c (M7).
 */
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "plat_headless.h"

plat_inputs hl_inputs;  /* zeroed = nothing pressed */
uint8_t hl_dsw_option = 0x15;
uint8_t hl_dsw_opton2 = 0x02;

double hl_now_ms = 0.0;
double hl_clock_step_ms = 0.0;

hl_seg  hl_segs[HL_MAX_SEGS];
int     hl_nsegs;
int     hl_frames;

hl_seg  hl_last_segs[HL_MAX_SEGS];
int     hl_last_nsegs;

float hl_bbox_xmin, hl_bbox_xmax, hl_bbox_ymin, hl_bbox_ymax;
int   hl_bbox_valid;

void (*hl_vec_line_hook)(float, float, float, float, uint32_t, int) = NULL;
void (*hl_present_hook)(void) = NULL;
void (*hl_audio_hook)(const int16_t *, int) = NULL;

int      hl_audio_rate;
uint64_t hl_audio_frames;
int      hl_audio_peak;

/* ---- bounding box -------------------------------------------------------- */

void hl_bbox_reset(void)
{
    hl_bbox_valid = 0;
    hl_bbox_xmin = hl_bbox_xmax = hl_bbox_ymin = hl_bbox_ymax = 0.0f;
}

static void bbox_add(float x, float y)
{
    if (!hl_bbox_valid) {
        hl_bbox_xmin = hl_bbox_xmax = x;
        hl_bbox_ymin = hl_bbox_ymax = y;
        hl_bbox_valid = 1;
        return;
    }
    if (x < hl_bbox_xmin) hl_bbox_xmin = x;
    if (x > hl_bbox_xmax) hl_bbox_xmax = x;
    if (y < hl_bbox_ymin) hl_bbox_ymin = y;
    if (y > hl_bbox_ymax) hl_bbox_ymax = y;
}

/* ---- lifecycle -------------------------------------------------------- */

int plat_init(void)
{
    hl_bbox_reset();
    return 0;
}

void plat_shutdown(void) { }

/* ---- video: capture --------------------------------------------------- */

void plat_video_begin(void) { hl_nsegs = 0; }

void plat_video_line(float x0, float y0, float x1, float y1, uint32_t rgb, int intensity)
{
    if (hl_nsegs < HL_MAX_SEGS) {
        hl_seg *s = &hl_segs[hl_nsegs++];
        s->x0 = x0; s->y0 = y0; s->x1 = x1; s->y1 = y1;
        s->rgb = rgb; s->intensity = intensity;
    }
    bbox_add(x0, y0);
    bbox_add(x1, y1);
    if (hl_vec_line_hook) hl_vec_line_hook(x0, y0, x1, y1, rgb, intensity);
}

void plat_video_present(void)
{
    hl_frames++;
    if (hl_nsegs > 0) memcpy(hl_last_segs, hl_segs, (size_t)hl_nsegs * sizeof hl_segs[0]);
    hl_last_nsegs = hl_nsegs;
    if (hl_present_hook) hl_present_hook();
}

int hl_write_svg(const char *path)
{
    FILE *f;
    int i;
    double xmin = (double)BZ_VIEW_XMIN, ymax = (double)BZ_VIEW_YMAX;
    double w = (double)(BZ_VIEW_XMAX - BZ_VIEW_XMIN);
    double h = (double)(BZ_VIEW_YMAX - BZ_VIEW_YMIN);

    f = fopen(path, "w");
    if (!f) return 1;
    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"%g %g %g %g\">\n",
            xmin, -ymax, w, h);
    fprintf(f, "  <rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" fill=\"black\"/>\n",
            xmin, -ymax, w, h);
    fprintf(f, "  <g transform=\"scale(1,-1)\">\n");  /* avg.c's y is up; SVG's is down */
    for (i = 0; i < hl_last_nsegs; i++) {
        const hl_seg *s = &hl_last_segs[i];
        int r = (int)(s->rgb & 0xFF);
        int g = (int)((s->rgb >> 8) & 0xFF);
        int b = (int)((s->rgb >> 16) & 0xFF);
        fprintf(f, "    <line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"rgb(%d,%d,%d)\" stroke-width=\"1\"/>\n",
                (double)s->x0, (double)s->y0, (double)s->x1, (double)s->y1, r, g, b);
    }
    fprintf(f, "  </g>\n</svg>\n");
    fclose(f);
    return 0;
}

/* ---- input -------------------------------------------------------------- */

void plat_input_poll(plat_inputs *in) { *in = hl_inputs; }

uint8_t plat_dsw_option(void) { return hl_dsw_option; }
uint8_t plat_dsw_opton2(void) { return hl_dsw_opton2; }

/* ---- audio: counted, not played ------------------------------------------ */

int plat_audio_open(int sample_rate)
{
    hl_audio_rate   = sample_rate;
    hl_audio_frames = 0;
    hl_audio_peak   = 0;
    return 0;
}

void plat_audio_push(const int16_t *pcm, int frames)
{
    int i;
    if (!pcm || frames <= 0) return;
    hl_audio_frames += (uint64_t)frames;
    for (i = 0; i < frames; i++) {
        int v = pcm[i] < 0 ? -(int)pcm[i] : (int)pcm[i];
        if (v > hl_audio_peak) hl_audio_peak = v;
    }
    if (hl_audio_hook) hl_audio_hook(pcm, frames);
}

void plat_audio_close(void) { hl_audio_rate = 0; }

/* ---- time: simulated ----------------------------------------------------- */

double plat_now_ms(void)     { hl_now_ms += hl_clock_step_ms; return hl_now_ms; }
void   plat_sleep_ms(int ms) { hl_now_ms += (double)ms; }

/* ---- misc ------------------------------------------------------------------ */

void plat_status_text(const char *s) { (void)s; }

/* no sample playback headless: every sample "missing" */
int  plat_sample_load(const char *name) { (void)name; return -1; }
void plat_sample_start(int chan, int smp, int loop) { (void)chan; (void)smp; (void)loop; }
void plat_sample_stop(int chan) { (void)chan; }
int  plat_sample_playing(int chan) { (void)chan; return 0; }
void plat_sample_set_freq(int chan, int freq) { (void)chan; (void)freq; }
int  plat_sample_get_freq(int chan) { (void)chan; return 0; }
void plat_sample_set_volume(int chan, int vol) { (void)chan; (void)vol; }
