/* plat_mac.c - Battlezone C port: the macOS backend (SDL2).
 *
 * Implements platform/bz_platform.h on SDL 2 (window + SDL_Renderer geometry
 * for the vector beams, keyboard/game-controller input, a callback audio
 * device that mixes the POKEY stream with the recorded samples).  Written for
 * Apple Silicon; nothing here is macOS-specific beyond the monotonic clock
 * and the bundle-relative resource search, so it should build on any SDL2
 * host.
 *
 * Link exactly one backend per binary; this file also owns main().
 *
 * Key bindings (same as the Windows backend, plus Mac fullscreen):
 *   E / D        left tread forward / reverse
 *   I / K        right tread forward / reverse
 *   arrow keys   single-stick driving (ORed with the tread keys)
 *   Space, LCtrl fire            5 / 6   coin 1 / 2        1 / 2  start 1 / 2
 *   F1 diag step   F2 toggle self-test   F3 reset   F4 slam
 *   Cmd+F, Ctrl+Cmd+F, Alt+Return        toggle fullscreen
 *   Esc, Cmd+Q, window close             quit
 * Game controller: left stick Y = left tread, right stick Y = right tread,
 * D-pad = single stick, A / right trigger = fire, Start = start 1,
 * Back = coin 1.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL.h>

#include "../bz_platform.h"

#define WIN_W   1024                      /* logical size; 4:3 like the tube */
#define WIN_H   768
#define BEAM_W  2.4f                      /* beam width, logical pixels */
#define AUDIO_RING   16384                /* frames; power of two */
#define AUDIO_PREFILL 360                 /* ~2 NMI pushes before we start */
#define AUDIO_MAXFILL 6000                /* beyond this, trim (clock drift) */
#define NCHAN   16
#define MAX_SMP 16

/* ---- state ----------------------------------------------------------------- */
static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static int           g_quit;
static int           g_fullscreen;
static int           g_show_stats;
static int           g_test_switch;       /* F2 / --test */
static int           g_mute;
static char          g_samples_dir[1024];
static const char   *g_shot_path;         /* --shot FILE.bmp: save a frame, then quit */
static double        g_shot_after_ms;
static double        g_t0_ms;
static int           g_demo, g_polls;     /* --demo: scripted coin/start/drive/fire */

static uint8_t       g_dsw_option = 0x15, g_dsw_opton2 = 0x02;

/* momentary buttons are latched on key-down so a tap shorter than the
 * ~64 ms main pass is not lost */
static plat_inputs   g_sticky;
static int           g_reset_latch;
static SDL_GameController *g_pad;

/* ---- video ------------------------------------------------------------------ */
static SDL_Vertex *g_vtx;
static int         g_nvtx, g_capvtx;
static const float k_sx = (float)WIN_W / (BZ_VIEW_XMAX - BZ_VIEW_XMIN);
static const float k_sy = (float)WIN_H / (BZ_VIEW_YMAX - BZ_VIEW_YMIN);

static int clip_seg(float *x0, float *y0, float *x1, float *y1)
{
    /* Liang-Barsky against the visible tube */
    float dx = *x1 - *x0, dy = *y1 - *y0, t0 = 0.0f, t1 = 1.0f;
    float p[4] = { -dx, dx, -dy, dy };
    float q[4] = { *x0 - BZ_VIEW_XMIN, BZ_VIEW_XMAX - *x0,
                   *y0 - BZ_VIEW_YMIN, BZ_VIEW_YMAX - *y0 };
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0.0f) { if (q[i] < 0.0f) return 0; continue; }
        float r = q[i] / p[i];
        if (p[i] < 0.0f) { if (r > t1) return 0; if (r > t0) t0 = r; }
        else             { if (r < t0) return 0; if (r < t1) t1 = r; }
    }
    float ax = *x0 + t0 * dx, ay = *y0 + t0 * dy;
    float bx = *x0 + t1 * dx, by = *y0 + t1 * dy;
    *x0 = ax; *y0 = ay; *x1 = bx; *y1 = by;
    return 1;
}

static void vtx_push(float x, float y, SDL_Color c)
{
    if (g_nvtx == g_capvtx) {
        g_capvtx = g_capvtx ? g_capvtx * 2 : 24576;
        g_vtx = (SDL_Vertex *)realloc(g_vtx, (size_t)g_capvtx * sizeof *g_vtx);
        if (!g_vtx) { g_capvtx = g_nvtx = 0; return; }
    }
    SDL_Vertex *v = &g_vtx[g_nvtx++];
    v->position.x = x; v->position.y = y;
    v->color = c;
    v->tex_coord.x = v->tex_coord.y = 0.0f;
}

void plat_video_begin(void) { g_nvtx = 0; }

void plat_video_line(float x0, float y0, float x1, float y1,
                     uint32_t rgb, int intensity)
{
    if (!clip_seg(&x0, &y0, &x1, &y1)) return;
    if (intensity < 1) intensity = 1;
    if (intensity > 7) intensity = 7;
    float b = 0.2f + 0.8f * (float)intensity / 7.0f;
    SDL_Color c = { (Uint8)((rgb & 0xFF) * b), (Uint8)(((rgb >> 8) & 0xFF) * b),
                    (Uint8)(((rgb >> 16) & 0xFF) * b), 255 };

    /* to logical pixels, y flipped */
    float ax = (x0 - BZ_VIEW_XMIN) * k_sx, ay = (BZ_VIEW_YMAX - y0) * k_sy;
    float bx = (x1 - BZ_VIEW_XMIN) * k_sx, by = (BZ_VIEW_YMAX - y1) * k_sy;
    float dx = bx - ax, dy = by - ay, len = sqrtf(dx * dx + dy * dy);
    float h = BEAM_W * 0.5f, ux, uy;
    if (len < 1e-3f) { ux = 1.0f; uy = 0.0f; }    /* a dot: square of the beam */
    else             { ux = dx / len; uy = dy / len; }
    float nx = -uy * h, ny = ux * h;              /* normal */
    ax -= ux * h; ay -= uy * h;                   /* square end caps */
    bx += ux * h; by += uy * h;

    vtx_push(ax + nx, ay + ny, c); vtx_push(ax - nx, ay - ny, c); vtx_push(bx + nx, by + ny, c);
    vtx_push(bx + nx, by + ny, c); vtx_push(ax - nx, ay - ny, c); vtx_push(bx - nx, by - ny, c);
}

static void pump_events(void);

void plat_video_present(void)
{
    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
    SDL_RenderClear(g_ren);
    if (g_nvtx) {
        SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_ADD);
        SDL_RenderGeometry(g_ren, NULL, g_vtx, g_nvtx, NULL, 0);
    }
    if (g_shot_path && plat_now_ms() - g_t0_ms >= g_shot_after_ms) {
        int w, h;
        SDL_GetRendererOutputSize(g_ren, &w, &h);
        SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (sf && !SDL_RenderReadPixels(g_ren, NULL, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch))
            SDL_SaveBMP(sf, g_shot_path);
        else fprintf(stderr, "shot: %s\n", SDL_GetError());
        if (sf) SDL_FreeSurface(sf);
        g_quit = 1;
    }
    SDL_RenderPresent(g_ren);
    pump_events();
}

/* ---- input ------------------------------------------------------------------ */
static void set_fullscreen(int on)
{
    g_fullscreen = on;
    SDL_SetWindowFullscreen(g_win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_ShowCursor(on ? SDL_DISABLE : SDL_ENABLE);
}

static void open_pad(void)
{
    if (g_pad) return;
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i) && (g_pad = SDL_GameControllerOpen(i)) != NULL) break;
}

static void pump_events(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            g_quit = 1; break;
        case SDL_CONTROLLERDEVICEADDED:
            open_pad(); break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (g_pad && !SDL_GameControllerGetAttached(g_pad)) {
                SDL_GameControllerClose(g_pad); g_pad = NULL;
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_A:     g_sticky.fire = 1; break;
            case SDL_CONTROLLER_BUTTON_START: g_sticky.start1 = 1; break;
            case SDL_CONTROLLER_BUTTON_BACK:  g_sticky.coin1 = 1; break;
            default: break;
            }
            break;
        case SDL_KEYDOWN: {
            SDL_Keycode k = e.key.keysym.sym;
            Uint16 m = e.key.keysym.mod;
            if (k == SDLK_ESCAPE) { g_quit = 1; break; }
            if (e.key.repeat) break;
            if ((k == SDLK_f && (m & KMOD_GUI)) ||
                (k == SDLK_RETURN && (m & KMOD_ALT))) { set_fullscreen(!g_fullscreen); break; }
            switch (e.key.keysym.scancode) {
            case SDL_SCANCODE_SPACE:
            case SDL_SCANCODE_LCTRL: g_sticky.fire = 1; break;
            case SDL_SCANCODE_1: g_sticky.start1 = 1; break;
            case SDL_SCANCODE_2: g_sticky.start2 = 1; break;
            case SDL_SCANCODE_5: g_sticky.coin1 = 1; break;
            case SDL_SCANCODE_6: g_sticky.coin2 = 1; break;
            case SDL_SCANCODE_F1: g_sticky.diag = 1; break;
            case SDL_SCANCODE_F2: g_test_switch = !g_test_switch; break;
            case SDL_SCANCODE_F3: g_reset_latch = 1; break;
            case SDL_SCANCODE_F4: g_sticky.slam = 1; break;
            default: break;
            }
            break;
        }
        default: break;
        }
    }
}

static int axis_dir(int v)  /* -1, 0, +1 with a ~0.5 threshold; SDL y is down-positive */
{
    return v < -16384 ? -1 : v > 16384 ? 1 : 0;
}

void plat_input_poll(plat_inputs *in)
{
    const Uint8 *ks;
    int up, dn, lf, rt;

    SDL_PumpEvents();
    pump_events();
    ks = SDL_GetKeyboardState(NULL);

    up = ks[SDL_SCANCODE_UP];   dn = ks[SDL_SCANCODE_DOWN];
    lf = ks[SDL_SCANCODE_LEFT]; rt = ks[SDL_SCANCODE_RIGHT];
    int lfwd = ks[SDL_SCANCODE_E], lrev = ks[SDL_SCANCODE_D];
    int rfwd = ks[SDL_SCANCODE_I], rrev = ks[SDL_SCANCODE_K];
    int fire = ks[SDL_SCANCODE_SPACE] || ks[SDL_SCANCODE_LCTRL];

    if (g_pad) {
        lfwd |= axis_dir(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY)) < 0;
        lrev |= axis_dir(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY)) > 0;
        rfwd |= axis_dir(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_RIGHTY)) < 0;
        rrev |= axis_dir(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_RIGHTY)) > 0;
        up |= SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
        dn |= SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        lf |= SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        rt |= SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        fire |= SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_A) ||
                SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8192;
    }

    /* single-stick: up = both fwd, down = both rev, left = pivot left
     * (left rev + right fwd), right = pivot right; diagonals drive one tread */
    if (up && !dn) {
        if (lf && !rt)      rfwd = 1;
        else if (rt && !lf) lfwd = 1;
        else                lfwd = rfwd = 1;
    } else if (dn && !up) {
        if (lf && !rt)      lrev = 1;
        else if (rt && !lf) rrev = 1;
        else                lrev = rrev = 1;
    } else if (lf && !rt)   { lrev = 1; rfwd = 1; }
    else if (rt && !lf)     { lfwd = 1; rrev = 1; }

    memset(in, 0, sizeof *in);
    in->left_fwd = (uint8_t)(lfwd != 0);  in->left_rev = (uint8_t)(lrev != 0);
    in->right_fwd = (uint8_t)(rfwd != 0); in->right_rev = (uint8_t)(rrev != 0);
    in->fire   = (uint8_t)(fire || g_sticky.fire);
    in->start1 = (uint8_t)(ks[SDL_SCANCODE_1]  || g_sticky.start1);
    in->start2 = (uint8_t)(ks[SDL_SCANCODE_2]  || g_sticky.start2);
    in->coin1  = (uint8_t)(ks[SDL_SCANCODE_5]  || g_sticky.coin1);
    in->coin2  = (uint8_t)(ks[SDL_SCANCODE_6]  || g_sticky.coin2);
    in->slam   = (uint8_t)(ks[SDL_SCANCODE_F4] || g_sticky.slam);
    in->diag   = (uint8_t)(ks[SDL_SCANCODE_F1] || g_sticky.diag);
    in->test   = (uint8_t)g_test_switch;
    in->reset  = (uint8_t)g_reset_latch;
    in->quit   = (uint8_t)g_quit;
    if (g_demo) {                       /* ~15.6 polls/s: coin, start, then drive and fire */
        int n = ++g_polls;
        if (n == 20) in->coin1 = 1;
        if (n == 30) in->start1 = 1;
        if (n > 60) { in->left_fwd = in->right_fwd = 1; in->fire = (uint8_t)((n / 8) & 1); }
    }
    memset(&g_sticky, 0, sizeof g_sticky);
    g_reset_latch = 0;
}

uint8_t plat_dsw_option(void) { return g_dsw_option; }
uint8_t plat_dsw_opton2(void) { return g_dsw_opton2; }

/* ---- audio ------------------------------------------------------------------- */
typedef struct {
    int16_t *pcm;
    int      frames, rate;
} smp_t;

typedef struct {
    int    smp, loop, active, freq, vol;
    double pos;
} chan_t;

static SDL_AudioDeviceID g_adev;
static SDL_AudioSpec     g_aspec;
static int16_t           g_ring[AUDIO_RING];
static unsigned          g_rd, g_wr;          /* free-running frame counters */
static int               g_primed;            /* playing out the ring */
static smp_t             g_smp[MAX_SMP];
static int               g_nsmp;
static chan_t            g_chan[NCHAN];

static void audio_cb(void *ud, Uint8 *stream, int len)
{
    int16_t *out = (int16_t *)stream;
    int n = len / (int)sizeof(int16_t);
    (void)ud;

    for (int i = 0; i < n; i++) {
        int mix = 0;
        if (!g_primed && (int)(g_wr - g_rd) >= AUDIO_PREFILL) g_primed = 1;
        if (g_primed) {
            if (g_wr == g_rd) g_primed = 0;          /* underrun: rebuffer */
            else mix = g_ring[g_rd++ & (AUDIO_RING - 1)];
        }
        for (int c = 0; c < NCHAN; c++) {
            chan_t *ch = &g_chan[c];
            if (!ch->active) continue;
            const smp_t *s = &g_smp[ch->smp];
            int i0 = (int)ch->pos;
            if (i0 >= s->frames) {
                if (ch->loop) { ch->pos -= s->frames; i0 = (int)ch->pos; }
                else { ch->active = 0; continue; }
            }
            int i1 = i0 + 1 < s->frames ? i0 + 1 : (ch->loop ? 0 : i0);
            double f = ch->pos - i0;
            double v = s->pcm[i0] + (s->pcm[i1] - s->pcm[i0]) * f;
            mix += (int)(v * ch->vol / 255.0);
            ch->pos += (double)ch->freq / (double)g_aspec.freq;
        }
        out[i] = (int16_t)(mix > 32767 ? 32767 : mix < -32768 ? -32768 : mix);
    }
}

int plat_audio_open(int sample_rate)
{
    SDL_AudioSpec want;
    if (g_mute) return 1;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return 1;
    SDL_zero(want);
    want.freq = sample_rate; want.format = AUDIO_S16SYS; want.channels = 1;
    want.samples = 512; want.callback = audio_cb;
    g_adev = SDL_OpenAudioDevice(NULL, 0, &want, &g_aspec, 0);
    if (!g_adev) { fprintf(stderr, "SDL audio: %s\n", SDL_GetError()); return 1; }
    if (g_aspec.format != AUDIO_S16SYS || g_aspec.channels != 1) {
        SDL_CloseAudioDevice(g_adev); g_adev = 0; return 1;
    }
    g_rd = g_wr = 0; g_primed = 0;
    SDL_PauseAudioDevice(g_adev, 0);
    return 0;
}

void plat_audio_push(const int16_t *pcm, int frames)
{
    if (!g_adev) return;
    SDL_LockAudioDevice(g_adev);
    if ((int)(g_wr - g_rd) > AUDIO_MAXFILL)      /* the device clock lags ours: drop the backlog */
        g_rd = g_wr - AUDIO_PREFILL;
    for (int i = 0; i < frames; i++) g_ring[g_wr++ & (AUDIO_RING - 1)] = pcm[i];
    SDL_UnlockAudioDevice(g_adev);
}

void plat_audio_close(void)
{
    if (g_adev) { SDL_CloseAudioDevice(g_adev); g_adev = 0; }
}

/* ---- samples ----------------------------------------------------------------- */
/* Where the wavs are: --samples DIR, $BZ_SAMPLES, then next to the binary
 * (samples/), in an .app bundle (Contents/Resources/samples), one level up
 * (build/mac/ -> c_src/samples), and finally ./samples. */
static int dir_has(const char *dir)
{
    char p[1200];
    snprintf(p, sizeof p, "%s/FIRE.WAV", dir);
    FILE *f = fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}

static void find_samples_dir(void)
{
    char *base = SDL_GetBasePath();     /* trailing '/' */
    const char *env = getenv("BZ_SAMPLES");
    char cand[1100];
    const char *rel[] = { "samples", "../Resources/samples", "../samples", "../../samples" };

    if (g_samples_dir[0] && dir_has(g_samples_dir)) return;
    g_samples_dir[0] = 0;
    if (env && dir_has(env)) { snprintf(g_samples_dir, sizeof g_samples_dir, "%s", env); goto out; }
    if (base)
        for (size_t i = 0; i < sizeof rel / sizeof *rel; i++) {
            snprintf(cand, sizeof cand, "%s%s", base, rel[i]);
            if (dir_has(cand)) { snprintf(g_samples_dir, sizeof g_samples_dir, "%s", cand); goto out; }
        }
    if (dir_has("samples")) snprintf(g_samples_dir, sizeof g_samples_dir, "samples");
out:
    SDL_free(base);
}

int plat_sample_load(const char *name)
{
    char file[64], path[1300];
    SDL_AudioSpec spec;
    Uint8 *buf = NULL;
    Uint32 blen = 0;
    SDL_AudioCVT cvt;
    int i;

    if (!g_adev || g_nsmp >= MAX_SMP) return -1;
    if (!g_samples_dir[0]) find_samples_dir();
    if (!g_samples_dir[0]) { fprintf(stderr, "samples: directory not found (silent)\n"); return -1; }

    if (!strcmp(name, "fire1")) snprintf(file, sizeof file, "FIRE.WAV");
    else {
        for (i = 0; name[i] && i < (int)sizeof file - 5; i++)
            file[i] = (char)((name[i] >= 'a' && name[i] <= 'z') ? name[i] - 32 : name[i]);
        snprintf(file + i, sizeof file - (size_t)i, ".WAV");
    }
    snprintf(path, sizeof path, "%s/%s", g_samples_dir, file);
    if (!SDL_LoadWAV(path, &spec, &buf, &blen)) {
        fprintf(stderr, "sample '%s': %s (silent)\n", name, SDL_GetError());
        return -1;
    }
    /* to mono S16 at the sample's own rate; the mixer resamples on the fly */
    if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq,
                          AUDIO_S16SYS, 1, spec.freq) < 0) { SDL_FreeWAV(buf); return -1; }
    cvt.len = (int)blen;
    cvt.buf = (Uint8 *)SDL_malloc((size_t)blen * (size_t)cvt.len_mult);
    if (!cvt.buf) { SDL_FreeWAV(buf); return -1; }
    memcpy(cvt.buf, buf, blen);
    SDL_FreeWAV(buf);
    if (cvt.needed && SDL_ConvertAudio(&cvt) != 0) { SDL_free(cvt.buf); return -1; }

    g_smp[g_nsmp].frames = cvt.len_cvt / (int)sizeof(int16_t);
    g_smp[g_nsmp].rate = spec.freq;
    g_smp[g_nsmp].pcm = (int16_t *)cvt.buf;
    return g_nsmp++;
}

void plat_sample_start(int chan, int smp, int loop)
{
    if (!g_adev || chan < 0 || chan >= NCHAN || smp < 0 || smp >= g_nsmp) return;
    SDL_LockAudioDevice(g_adev);
    g_chan[chan].smp = smp; g_chan[chan].loop = loop; g_chan[chan].pos = 0.0;
    g_chan[chan].freq = g_smp[smp].rate; g_chan[chan].vol = 255;
    g_chan[chan].active = 1;
    SDL_UnlockAudioDevice(g_adev);
}

void plat_sample_stop(int chan)
{
    if (!g_adev || chan < 0 || chan >= NCHAN) return;
    SDL_LockAudioDevice(g_adev);
    g_chan[chan].active = 0;
    SDL_UnlockAudioDevice(g_adev);
}

int plat_sample_playing(int chan)
{
    int r;
    if (!g_adev || chan < 0 || chan >= NCHAN) return 0;
    SDL_LockAudioDevice(g_adev);
    r = g_chan[chan].active;
    SDL_UnlockAudioDevice(g_adev);
    return r;
}

void plat_sample_set_freq(int chan, int freq)
{
    if (!g_adev || chan < 0 || chan >= NCHAN) return;
    if (freq < 1) freq = 1;
    if (freq > 192000) freq = 192000;
    SDL_LockAudioDevice(g_adev);
    g_chan[chan].freq = freq;
    SDL_UnlockAudioDevice(g_adev);
}

int plat_sample_get_freq(int chan)
{
    int r;
    if (!g_adev || chan < 0 || chan >= NCHAN) return 0;
    SDL_LockAudioDevice(g_adev);
    r = g_chan[chan].freq;
    SDL_UnlockAudioDevice(g_adev);
    return r;
}

void plat_sample_set_volume(int chan, int vol)
{
    if (!g_adev || chan < 0 || chan >= NCHAN) return;
    SDL_LockAudioDevice(g_adev);
    g_chan[chan].vol = vol < 0 ? 0 : vol > 255 ? 255 : vol;
    SDL_UnlockAudioDevice(g_adev);
}

/* ---- time -------------------------------------------------------------------- */
double plat_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* The core's idle hint: keep the window serviced, never sleep past the next
 * NMI (bz_app_idle_ms()). */
void plat_sleep_ms(int ms)
{
    double idle = bz_app_idle_ms();
    if (idle >= 0.0 && (double)ms > idle) ms = (int)idle;
    if (ms < 0) ms = 0;
    pump_events();
    if (ms > 0 && !g_quit) SDL_Delay((Uint32)ms);
}

void plat_status_text(const char *s)
{
    if (g_show_stats && g_win) {
        char t[256];
        snprintf(t, sizeof t, "Battlezone - %s", s);
        SDL_SetWindowTitle(g_win, t);
    }
}

/* ---- lifecycle ----------------------------------------------------------------- */
int plat_init(void)
{
    const char *e;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_SetHint(SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK, "0");
    g_win = SDL_CreateWindow("Battlezone", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             WIN_W * 3 / 4, WIN_H * 3 / 4,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, 0);
    if (!g_ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }
    SDL_RenderSetLogicalSize(g_ren, WIN_W, WIN_H);
    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
    SDL_RenderClear(g_ren);
    SDL_RenderPresent(g_ren);
    open_pad();

    if ((e = getenv("BZ_DSW_OPTION")) != NULL) g_dsw_option = (uint8_t)strtoul(e, NULL, 16);
    if ((e = getenv("BZ_DSW_OPTON2")) != NULL) g_dsw_opton2 = (uint8_t)strtoul(e, NULL, 16);
    if (g_fullscreen) set_fullscreen(1);
    return 0;
}

void plat_shutdown(void)
{
    if (g_pad) { SDL_GameControllerClose(g_pad); g_pad = NULL; }
    free(g_vtx); g_vtx = NULL;
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_win) SDL_DestroyWindow(g_win);
    g_ren = NULL; g_win = NULL;
    SDL_Quit();
}

/* ---- main ---------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--test")) g_test_switch = 1;      /* self-test held from power-on */
        else if (!strcmp(argv[i], "--fullscreen") || !strcmp(argv[i], "-f")) g_fullscreen = 1;
        else if (!strcmp(argv[i], "--mute")) g_mute = 1;
        else if (!strcmp(argv[i], "--stats")) g_show_stats = 1;
        else if (!strcmp(argv[i], "--demo")) g_demo = 1;
        else if (!strcmp(argv[i], "--shot") && i + 2 < argc) {   /* --shot FILE.bmp SECONDS */
            g_shot_path = argv[++i]; g_shot_after_ms = atof(argv[++i]) * 1000.0;
        }
        else if (!strcmp(argv[i], "--samples") && i + 1 < argc)
            snprintf(g_samples_dir, sizeof g_samples_dir, "%s", argv[++i]);
        else if (!strncmp(argv[i], "-psn_", 5)) continue;        /* Finder launch */
        else {
            fprintf(stderr, "usage: %s [--test] [--fullscreen] [--mute] [--stats] [--samples DIR] [--demo] [--shot FILE.bmp SECS]\n", argv[0]);
            return 2;
        }
    }
    if (plat_init()) return 1;
    g_t0_ms = plat_now_ms();
    bz_app_init();
    while (!g_quit) bz_app_step(plat_now_ms());
    bz_app_exit();
    plat_shutdown();
    return 0;
}
