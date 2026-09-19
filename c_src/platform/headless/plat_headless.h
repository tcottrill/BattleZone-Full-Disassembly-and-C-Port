/* plat_headless.h - injection/capture hooks for the headless backend.
 *
 * Adapted from the Tempest port's platform/headless/plat_headless.h to
 * platform/bz_platform.h (M7).  A harness build (tests\bz_headless.exe, or
 * any other backend-agnostic test) links plat_headless.c: the whole contract
 * with no window, no sound device and no real clock.  Inputs and option
 * switches are injected; every plat_video_line segment of the current
 * picture is captured, plus the last presented picture (for a dump) and a
 * cumulative bounding box of every lit segment endpoint (what
 * bz_platform.h's BZ_VIEW_* needs to cover); the audio stream is counted
 * (frames, peak); time is a simulated clock the harness (or plat_sleep_ms)
 * advances.
 */
#ifndef PLAT_HEADLESS_H
#define PLAT_HEADLESS_H

#include <stdint.h>
#include "../bz_platform.h"

/* ---- injected inputs ---------------------------------------------------- */
extern plat_inputs hl_inputs;  /* copied out verbatim by plat_input_poll  */
extern uint8_t hl_dsw_option;  /* OPTION,  default 0x15 (bzone.cpp)       */
extern uint8_t hl_dsw_opton2;  /* OPTON2,  default 0x02                   */

/* ---- simulated clock ----------------------------------------------------- */
extern double hl_now_ms;           /* plat_now_ms returns this;
                                      plat_sleep_ms(ms) advances it           */
extern double hl_clock_step_ms;    /* > 0: every plat_now_ms call also advances
                                      the clock by this much (a stand-in for
                                      a wall clock that moves while the core
                                      spins; 0 = default, fully simulated)   */

/* ---- captured video ------------------------------------------------------ */
typedef struct {
    float    x0, y0, x1, y1;
    uint32_t rgb;
    int      intensity;
} hl_seg;

#define HL_MAX_SEGS 8192
extern hl_seg  hl_segs[HL_MAX_SEGS];  /* the current picture's segments        */
extern int     hl_nsegs;              /* reset by plat_video_begin             */
extern int     hl_frames;             /* pictures completed (present count)    */

extern hl_seg  hl_last_segs[HL_MAX_SEGS];  /* the last COMPLETE picture        */
extern int     hl_last_nsegs;

/* Cumulative bounding box of every lit segment endpoint since plat_init (or
 * the last hl_bbox_reset()) - this is the extent bz_platform.h's BZ_VIEW_*
 * macros must cover.  hl_bbox_valid is 0 until the first segment arrives. */
extern float hl_bbox_xmin, hl_bbox_xmax, hl_bbox_ymin, hl_bbox_ymax;
extern int   hl_bbox_valid;
void hl_bbox_reset(void);

/* Writes the last presented picture (hl_last_segs) as an SVG file: a black
 * background over bz_platform.h's BZ_VIEW_* box, lines in their rgb, y up
 * (a <g transform="scale(1,-1)"> flips SVG's y-down convention back to
 * avg.c's y-up).  Returns 0 on success. */
int hl_write_svg(const char *path);

/* optional hooks, NULL = just the buffer / discard */
extern void (*hl_vec_line_hook)(float x0, float y0, float x1, float y1,
                                uint32_t rgb, int intensity);
extern void (*hl_present_hook)(void);
extern void (*hl_audio_hook)(const int16_t *pcm, int frames);

/* ---- the audio stream, counted, not played -------------------------------- */
extern int      hl_audio_rate;    /* plat_audio_open's rate, 0 = closed     */
extern uint64_t hl_audio_frames;  /* frames pushed since open               */
extern int      hl_audio_peak;    /* loudest |sample| pushed since open     */

#endif  /* PLAT_HEADLESS_H */
