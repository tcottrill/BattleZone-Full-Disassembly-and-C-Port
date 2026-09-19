/* tests/bz_smoke.c - headless smoke test of the playable build's core (M7).
 *
 * A minimal bz_platform.h backend (simulated clock that jumps over every
 * idle, no audio, scripted inputs, capture of the last picture) driving the
 * real app_loop.c + every translated module, exactly as bz_win.exe does.
 *
 *   tests\bz_smoke.exe [passes] [--svg FILE] [--play] [--test]
 *     passes   MAIN passes to run (default 300 = ~19 s of machine time)
 *     --svg    write the last presented picture as an SVG
 *     --play   insert a coin at pass 20, press start at 40, then drive
 *              (forward, turn, fire) - a real game instead of attract
 *     --test   hold the self-test switch on from power-on (bzstst.c/M8)
 *              instead of playing; --svg then captures the last self-test
 *              picture
 *
 * Prints the pass/NMI/picture counts, lit segments and their bounding box
 * (what platform/bz_platform.h's BZ_VIEW_* must cover), and a few RAM cells.
 * Exit 0 when the game ran and drew something every so often.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../state.h"
#include "../platform/bz_platform.h"

void bz_app_stats(unsigned long *passes, unsigned long *nmis, unsigned long *pictures,
                  unsigned long *lit, float bbox[4]);

/* ---- the backend ---------------------------------------------------------- */
static double      sim_ms;
static plat_inputs next_in;

#define MAX_SEG 4096
typedef struct { float x0, y0, x1, y1; uint32_t rgb; int in; } seg;
static seg      cur[MAX_SEG], last[MAX_SEG];
static int      ncur, nlast;
static unsigned long pictures_with_lit;

int  plat_init(void) { return 0; }
void plat_shutdown(void) {}
void plat_video_begin(void) { ncur = 0; }
void plat_video_line(float x0, float y0, float x1, float y1, uint32_t rgb, int intensity)
{
    if (ncur < MAX_SEG) { seg s = { x0, y0, x1, y1, rgb, intensity }; cur[ncur++] = s; }
}
void plat_video_present(void)
{
    memcpy(last, cur, (size_t)ncur * sizeof cur[0]);
    nlast = ncur;
    if (ncur > 0) pictures_with_lit++;
}
void    plat_input_poll(plat_inputs *in) { *in = next_in; }
uint8_t plat_dsw_option(void) { return 0x15; }
uint8_t plat_dsw_opton2(void) { return 0x02; }
int     plat_audio_open(int rate) { (void)rate; return 1; }
void    plat_audio_push(const int16_t *pcm, int frames) { (void)pcm; (void)frames; }
void    plat_audio_close(void) {}
double  plat_now_ms(void) { return sim_ms; }
void    plat_sleep_ms(int ms) { double idle = bz_app_idle_ms(); sim_ms += (idle > ms ? idle : ms) + 1e-6; }
void    plat_status_text(const char *s) { (void)s; }
/* no sample playback headless: every sample "missing" */
int  plat_sample_load(const char *name) { (void)name; return -1; }
void plat_sample_start(int chan, int smp, int loop) { (void)chan; (void)smp; (void)loop; }
void plat_sample_stop(int chan) { (void)chan; }
int  plat_sample_playing(int chan) { (void)chan; return 0; }
void plat_sample_set_freq(int chan, int freq) { (void)chan; (void)freq; }
int  plat_sample_get_freq(int chan) { (void)chan; return 0; }
void plat_sample_set_volume(int chan, int vol) { (void)chan; (void)vol; }

static void write_svg(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return; }
    fprintf(f, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"%g %g %g %g\" width=\"1040\" height=\"800\">\n",
            (double)BZ_VIEW_XMIN, (double)-BZ_VIEW_YMAX,
            (double)(BZ_VIEW_XMAX - BZ_VIEW_XMIN), (double)(BZ_VIEW_YMAX - BZ_VIEW_YMIN));
    fprintf(f, "<rect x=\"%g\" y=\"%g\" width=\"100%%\" height=\"100%%\" fill=\"black\"/>\n",
            (double)BZ_VIEW_XMIN, (double)-BZ_VIEW_YMAX);
    for (int i = 0; i < nlast; i++) {
        const seg *s = &last[i];
        unsigned r = s->rgb & 0xFF, gg = (s->rgb >> 8) & 0xFF, b = (s->rgb >> 16) & 0xFF;
        double op = 0.3 + 0.1 * s->in;
        if (s->x0 == s->x1 && s->y0 == s->y1)
            fprintf(f, "<circle cx=\"%g\" cy=\"%g\" r=\"1.5\" fill=\"rgb(%u,%u,%u)\" fill-opacity=\"%.2f\"/>\n",
                    (double)s->x0, (double)-s->y0, r, gg, b, op);
        else
            fprintf(f, "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"rgb(%u,%u,%u)\" stroke-opacity=\"%.2f\" stroke-width=\"1.5\"/>\n",
                    (double)s->x0, (double)-s->y0, (double)s->x1, (double)-s->y1, r, gg, b, op);
    }
    fprintf(f, "</svg>\n");
    fclose(f);
}

/* ---- the driver ----------------------------------------------------------- */
int main(int argc, char **argv)
{
    unsigned long passes = 300, p, np, nn, npic, nlit;
    const char *svg = NULL;
    int play = 0, test = 0;
    unsigned long diag_every = 0, test_off = 0;
    float bb[4];

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--svg") && i + 1 < argc) svg = argv[++i];
        else if (!strcmp(argv[i], "--play")) play = 1;
        else if (!strcmp(argv[i], "--test")) test = 1;
        else if (!strcmp(argv[i], "--test-off") && i + 1 < argc) test_off = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--diag") && i + 1 < argc) diag_every = strtoul(argv[++i], NULL, 0);
        else passes = strtoul(argv[i], NULL, 0);
    }

    if (test) next_in.test = 1;   /* switch held on before power-on (bz_app_init reads it) */
    plat_init();
    bz_app_init();
    for (p = 0; p < passes; p++) {
        memset(&next_in, 0, sizeof next_in);
        if (test && (!test_off || p < test_off)) next_in.test = 1;   /* --test-off N: switch off at pass N */
        if (test && diag_every && p > 0 && p % diag_every < 2) next_in.diag = 1;   /* --diag N: press F1 every N passes */
        if (play) {
            if (p >= 20 && p < 23) next_in.coin1 = 1;
            if (p >= 40 && p < 43) next_in.start1 = 1;
            if (p >= 60) {
                unsigned phase = (unsigned)(p / 40) % 4;
                next_in.left_fwd = 1;
                next_in.right_fwd = phase != 1;
                next_in.fire = (p % 16) < 2;
            }
        }
        bz_app_step(sim_ms);
    }
    bz_app_stats(&np, &nn, &npic, &nlit, bb);
    printf("BZ_SMOKE: passes %lu, NMIs %lu, pictures %lu (%lu with lit segments), lit segments %lu\n",
           np, nn, npic, pictures_with_lit, nlit);
    printf("  machine time %.1f s; bbox x %.1f..%.1f y %.1f..%.1f; last picture %d segments\n",
           sim_ms / 1000.0, (double)bb[0], (double)bb[1], (double)bb[2], (double)bb[3], nlast);
    printf("  ATRACT=%02X GOVER=%02X LOGO=%02X CRACK=%02X LIVES=%02X HITS=%02X%02X S_S_CRDT=%02X TRAP1=%02X\n",
           ATRACT, GOVER, LOGO, CRACK, LIVES, HITS_AT(1), HITS, S_S_CRDT, TRAP1);
    if (svg) { write_svg(svg); printf("  wrote %s\n", svg); }
    bz_app_exit();
    plat_shutdown();
    return (npic > 0 && pictures_with_lit > 0) ? 0 : 1;
}
