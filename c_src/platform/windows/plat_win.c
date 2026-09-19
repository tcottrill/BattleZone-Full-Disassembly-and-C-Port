/* plat_win.c - Battlezone C port: the Windows backend (M7).
 *
 * Adapted from the Tempest port's platform/windows/plat_win.c (itself from
 * the Space Duel port) to platform/bz_platform.h.  Win32 + OpenGL 3.3 on the
 * vendored framework files in this directory: raw input (key[] buffer),
 * the XAudio2 mixer's stream voice, the shader beam renderer (vector_draw.c),
 * DirectInput joystick, a DPI-aware window with ALT+ENTER borderless
 * fullscreen, ini config.
 *
 * Battlezone differences from the Tempest backend (see bz_platform.h):
 *  - monochrome-per-segment: the core already colours each segment (green,
 *    red overlay strip) and gives a 1..7 intensity - no colour RAM, no STAT
 *    intensity substitution;
 *  - the monitor is NOT rotated and NOT flipped: AVG space (BZ_VIEW_XMIN..
 *    XMAX, BZ_VIEW_YMIN..YMAX, from bz_platform.h - never hard-coded here)
 *    maps straight onto the window, letterboxed to keep its aspect ratio;
 *  - input is tank treads (left_fwd/left_rev/right_fwd/right_rev), not a
 *    spinner: E/D (left tread fwd/rev), I/K (right tread fwd/rev), plus a
 *    single-stick mode on the arrow keys / d-pad that synthesizes both
 *    treads, ORed with the direct tread keys;
 *  - no EAROM/NVRAM (Battlezone's self test / bookkeeping is not ported
 *    yet - test is accepted and ignored by the core), no cocktail, no
 *    picture-row stats, no --shot/--autoplay verification tooling (M7 has
 *    its own stub core, tests\plat_stub.c, for that);
 *  - files next to the exe: bz_win.ini, bz_win.log.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../bz_platform.h"
#include "framework.h"  /* Windows.h, glew, log */
#include "sys_gl.h"
#include "rawinput.h"
#include "joystick.h"
#include "mixer.h"
#include "vector_draw.h"
#include "mat4.h"
#include "ini.h"
#include "colordefs.h"
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#define APP_NAME "Battlezone"
#define KEY_HELP "E/D I/K treads, arrows/d-pad single-stick, Space/Ctrl fire, " \
                 "1/2 start, 5/6 coin, F2 test (toggle), F1 diag, F3 reset, F4 slam, Alt+Enter fullscreen, Esc quit"

/* ------------------------------------------------------------------ */
/* files next to the exe                                               */
/* ------------------------------------------------------------------ */

static char exe_dir[MAX_PATH];
static char path_ini[MAX_PATH], path_log[MAX_PATH];

static void resolve_paths(void)
{
    char *slash;
    DWORD n = GetModuleFileNameA(NULL, exe_dir, (DWORD)sizeof exe_dir);
    if (n == 0 || n >= sizeof exe_dir) exe_dir[0] = '\0';
    slash = strrchr(exe_dir, '\\');
    if (slash) slash[1] = '\0'; else exe_dir[0] = '\0';
    snprintf(path_ini, sizeof path_ini, "%sbz_win.ini", exe_dir);
    snprintf(path_log, sizeof path_log, "%sbz_win.log", exe_dir);
}

/* ------------------------------------------------------------------ */
/* option switches: [dips] in bz_win.ini                               */
/* ------------------------------------------------------------------ */
/* One key per option-switch group, each a word from its list, as the
 * Tempest / Space Duel / Gravitar ports do it.  The groups, bit values and
 * defaults are the AAE driver's INPUT_PORTS_START(bzone) (bzone.cpp): 3
 * lives, missile at 10000, bonus tank at 15k and 100k, English, 1 coin 1
 * play.  An unknown word falls back to the default; whatever was chosen is
 * written back, so the ini always shows the spelling that took effect.
 * The ROM reads both banks raw: OPTION ($0A00, DSW0) and OPTON2 ($0C00,
 * DSW1) - e.g. LIVES = (OPTION & 3) + 2 (NMI L5681). */

static uint8_t dsw_option = 0x15, dsw_opton2 = 0x02;

uint8_t plat_dsw_option(void) { return dsw_option; }
uint8_t plat_dsw_opton2(void) { return dsw_opton2; }

static uint8_t dip_pick(const char *name, const char *const *names,
                        const uint8_t *vals, int n, int def)
{
    char *s = get_config_string("dips", name, names[def]);
    int pick = def;
    if (s) {
        for (int i = 0; i < n; i++)
            if (_stricmp(s, names[i]) == 0) { pick = i; break; }
        free(s);
    }
    set_config_string("dips", name, names[pick]);
    return vals[pick];
}

static void read_dips(void)
{
    /* OPTION ($0A00, DSW0) */
    static const char *const lives_n[]   = { "2", "3", "4", "5" };
    static const uint8_t     lives_v[]   = { 0x00, 0x01, 0x02, 0x03 };
    static const char *const missile_n[] = { "5000", "10000", "20000", "30000" };
    static const uint8_t     missile_v[] = { 0x00, 0x04, 0x08, 0x0C };
    static const char *const bonus_n[]   = { "none", "15k_and_100k", "20k_and_100k", "50k_and_100k" };
    static const uint8_t     bonus_v[]   = { 0x00, 0x10, 0x20, 0x30 };
    static const char *const lang_n[]    = { "english", "german", "french", "spanish" };
    static const uint8_t     lang_v[]    = { 0x00, 0x40, 0x80, 0xC0 };
    /* OPTON2 ($0C00, DSW1) */
    static const char *const coin_n[]    = { "2_coins_1_play", "1_coin_1_play",
                                             "1_coin_2_plays", "free_play" };
    static const uint8_t     coin_v[]    = { 0x03, 0x02, 0x01, 0x00 };
    static const char *const right_n[]   = { "x1", "x4", "x5", "x6" };
    static const uint8_t     right_v[]   = { 0x00, 0x04, 0x08, 0x0C };
    static const char *const left_n[]    = { "x1", "x2" };
    static const uint8_t     left_v[]    = { 0x00, 0x10 };
    static const char *const badd_n[]    = { "none", "3_credits_2_coins", "5_credits_4_coins",
                                             "6_credits_4_coins", "6_credits_5_coins" };
    static const uint8_t     badd_v[]    = { 0x00, 0x20, 0x40, 0x60, 0x80 };

    uint8_t d0 = 0, d1 = 0;
    d0 |= dip_pick("lives",       lives_n,   lives_v,   4, 1);
    d0 |= dip_pick("missile_at",  missile_n, missile_v, 4, 1);
    d0 |= dip_pick("bonus_tank",  bonus_n,   bonus_v,   4, 1);
    d0 |= dip_pick("language",    lang_n,    lang_v,    4, 0);
    d1 |= dip_pick("coinage",     coin_n,    coin_v,    4, 1);
    d1 |= dip_pick("right_coin",  right_n,   right_v,   4, 0);
    d1 |= dip_pick("left_coin",   left_n,    left_v,    2, 0);
    d1 |= dip_pick("bonus_coins", badd_n,    badd_v,    5, 0);
    LOG_INFO("option switches: OPTION=%02X OPTON2=%02X", d0, d1);
    dsw_option = d0;
    dsw_opton2 = d1;
}

/* ------------------------------------------------------------------ */
/* geometry: AVG space -> the window, letterboxed, no rotation         */
/* ------------------------------------------------------------------ */
/* The tube is BZ_VIEW_XMIN..XMAX x BZ_VIEW_YMIN..YMAX (bz_platform.h),
 * mounted normally: the backend draws it straight, aspect-correct. */

/* Liang-Barsky against the tube window; 0 = nothing visible, 2 = cut at an edge. */
static int clip_seg(float *x0, float *y0, float *x1, float *y1)
{
    float t0 = 0.0f, t1 = 1.0f;
    float dx = *x1 - *x0, dy = *y1 - *y0;
    float p[4], q[4];
    int i;
    p[0] = -dx; q[0] = *x0 - BZ_VIEW_XMIN;
    p[1] =  dx; q[1] = BZ_VIEW_XMAX - *x0;
    p[2] = -dy; q[2] = *y0 - BZ_VIEW_YMIN;
    p[3] =  dy; q[3] = BZ_VIEW_YMAX - *y0;
    for (i = 0; i < 4; i++) {
        if (p[i] == 0.0f) {
            if (q[i] < 0.0f) return 0;
        } else {
            float t = q[i] / p[i];
            if (p[i] < 0.0f) { if (t > t1) return 0; if (t > t0) t0 = t; }
            else             { if (t < t0) return 0; if (t < t1) t1 = t; }
        }
    }
    if (t1 < 1.0f) { *x1 = *x0 + t1 * dx; *y1 = *y0 + t1 * dy; }
    if (t0 > 0.0f) { *x0 = *x0 + t0 * dx; *y0 = *y0 + t0 * dy; }
    return (t0 > 0.0f || t1 < 1.0f) ? 2 : 1;
}

/* ---- phosphor / frame store ------------------------------------------
 * A pass's segments are captured already clipped to the tube.  The newest
 * complete frame is what every present draws; with [vector] phosphor_ms > 0
 * older frames are replayed under it at exp(-age/tau) (default off: the
 * newest frame only). */
#define PH_FRAMES  8
#define PH_SEGS    4096
#define PH_FLOOR   0.02

typedef struct { float x0, y0, x1, y1; rgb_t rgb; int intensity; } ph_seg;
typedef struct { ph_seg seg[PH_SEGS]; int n; double t; int used; } ph_frame;

static ph_frame ph_buf[PH_FRAMES];
static int      ph_cur;        /* the newest COMPLETE frame */
static int      ph_fill = -1;  /* the frame being captured, -1 = none */
static double   ph_tau;
static int      ph_dropped;
static unsigned long seg_clipped, seg_cut, seg_total;

/* ------------------------------------------------------------------ */
/* window, framework externs                                           */
/* ------------------------------------------------------------------ */

static HWND hWnd;
static int  g_quit;
static int  window_active;
static int  test_switch;  /* F2 latch: 1 = self-test switch ON (bzstst.c/M8) */

int SCREEN_W = 800;  /* window client area, physical pixels */
int SCREEN_H = 600;
int DESIGN_W = 540;       /* the tube's own aspect rect ViewOrthoScaled fits;
                              computed from BZ_VIEW_* at plat_init */
int DESIGN_H = 405;

HWND win_get_window(void) { return hWnd; }

static void msg_box(const char *title, const char *message)
{
    MessageBoxA(NULL, message, title, MB_ICONEXCLAMATION | MB_OK);
}

static void set_window_title(const char *title) { SetWindowTextA(hWnd, title); }

static float monitor_refresh_hz(HWND w)
{
    MONITORINFOEXW mi;
    DEVMODEW dm;
    HMONITOR m = MonitorFromWindow(w, MONITOR_DEFAULTTOPRIMARY);

    memset(&mi, 0, sizeof mi);
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(m, (MONITORINFO *)&mi)) return 0.0f;
    memset(&dm, 0, sizeof dm);
    dm.dmSize = sizeof dm;
    if (!EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
        return 0.0f;
    return dm.dmDisplayFrequency > 1 ? (float)dm.dmDisplayFrequency : 0.0f;
}

/* ------------------------------------------------------------------ */
/* DPI awareness                                                       */
/* ------------------------------------------------------------------ */

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif
#ifndef USER_DEFAULT_SCREEN_DPI
#define USER_DEFAULT_SCREEN_DPI 96
#endif
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

static void enable_dpi_awareness(void)
{
    typedef BOOL(WINAPI *SetProcessDpiAwarenessContext_t)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        SetProcessDpiAwarenessContext_t set_ctx = (SetProcessDpiAwarenessContext_t)
            GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (set_ctx && set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            return;
    }
    SetProcessDPIAware();
}

static UINT window_dpi(HWND hwnd)
{
    typedef UINT(WINAPI *GetDpiForWindow_t)(HWND);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    UINT dpi = 0;
    if (user32) {
        GetDpiForWindow_t get_dpi = (GetDpiForWindow_t)
            GetProcAddress(user32, "GetDpiForWindow");
        if (get_dpi) dpi = get_dpi(hwnd);
    }
    if (dpi == 0) {
        HDC hdc = GetDC(NULL);
        if (hdc) {
            dpi = (UINT)GetDeviceCaps(hdc, LOGPIXELSY);
            ReleaseDC(NULL, hdc);
        }
    }
    return dpi ? dpi : USER_DEFAULT_SCREEN_DPI;
}

/* The tube's aspect rect at the window's DPI, shrunk (keeping aspect) to fit
 * 90% of the monitor's work area, then scaled up a bit for legibility. */
static void size_window_for_dpi(HWND hwnd, UINT dpi, DWORD style)
{
    typedef BOOL(WINAPI *AdjustWindowRectExForDpi_t)(LPRECT, DWORD, BOOL, DWORD, UINT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    AdjustWindowRectExForDpi_t adjust_for_dpi = NULL;
    MONITORINFO mi;
    RECT wr;
    int cw = MulDiv(DESIGN_W * 2, (int)dpi, USER_DEFAULT_SCREEN_DPI);
    int ch = MulDiv(DESIGN_H * 2, (int)dpi, USER_DEFAULT_SCREEN_DPI);

    memset(&mi, 0, sizeof mi);
    mi.cbSize = sizeof mi;
    if (GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
        int avail_h = (mi.rcWork.bottom - mi.rcWork.top) * 9 / 10 - 40;
        if (avail_h > 200 && ch > avail_h) {
            ch = avail_h;
            cw = ch * DESIGN_W / DESIGN_H;
        }
    }

    wr.left = 0;
    wr.top = 0;
    wr.right = cw;
    wr.bottom = ch;

    if (user32)
        adjust_for_dpi = (AdjustWindowRectExForDpi_t)
            GetProcAddress(user32, "AdjustWindowRectExForDpi");
    if (adjust_for_dpi)
        adjust_for_dpi(&wr, style, FALSE, 0, dpi);
    else
        AdjustWindowRect(&wr, style, FALSE);

    SetWindowPos(hwnd, NULL, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* ------------------------------------------------------------------ */
/* borderless fullscreen, ALT+ENTER                                    */
/* ------------------------------------------------------------------ */

static int      is_fullscreen;
static RECT     windowed_rect;
static LONG_PTR windowed_style;
static LONG_PTR windowed_exstyle;

int IsFullscreen(void) { return is_fullscreen; }

void ToggleFullscreen(void)
{
    if (is_fullscreen) {
        SetWindowLongPtr(hWnd, GWL_STYLE, windowed_style);
        SetWindowLongPtr(hWnd, GWL_EXSTYLE, windowed_exstyle);
        SetWindowPos(hWnd, HWND_NOTOPMOST,
                     windowed_rect.left, windowed_rect.top,
                     windowed_rect.right - windowed_rect.left,
                     windowed_rect.bottom - windowed_rect.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        is_fullscreen = 0;
    } else {
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &mi);
        GetWindowRect(hWnd, &windowed_rect);
        windowed_style = GetWindowLongPtr(hWnd, GWL_STYLE);
        windowed_exstyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);
        SetWindowLongPtr(hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowLongPtr(hWnd, GWL_EXSTYLE, WS_EX_APPWINDOW | WS_EX_TOPMOST);
        SetWindowPos(hWnd, HWND_TOPMOST,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        is_fullscreen = 1;
    }
}

/* ------------------------------------------------------------------ */
/* beam width                                                          */
/* ------------------------------------------------------------------ */
/* [vector] linewidth = the beam in PIXELS AT THE DEFAULT DESIGN_W-WIDE
 * window (scales with the picture as the window resizes); [vector]
 * line_smoothing = the AA feather in physical pixels on any screen.  The
 * beam renderer takes both in projection units: DESIGN_W across (the tube's
 * own span, BZ_VIEW_XMAX - BZ_VIEW_XMIN). */
static float beam_px = 2.0f;
static float beam_feather_px = 1.0f;
static float beam_proj[16];

static void update_beam_width_for(double vw)
{
    double span = (double)DESIGN_W, feather_du, core_px;
    if (vw < 1.0) vw = 1.0;
    feather_du = beam_feather_px * span / vw;
    beam_set_smoothing((float)feather_du);
    core_px = beam_px - beam_feather_px;
    if (core_px < 0.0) core_px = 0.0;
    beam_set_linewidth((float)(core_px * span / (double)DESIGN_W + feather_du));
}

static void update_beam_width(void)
{
    double vw = (double)SCREEN_W;
    if ((double)SCREEN_H * DESIGN_W < vw * DESIGN_H)
        vw = (double)SCREEN_H * DESIGN_W / (double)DESIGN_H;
    update_beam_width_for(vw);
}

/* ------------------------------------------------------------------ */
/* presentation                                                        */
/* ------------------------------------------------------------------ */

static int    swap_mode;  /* [main] vsync: 0/1/2 */
static double refresh_ms = 1000.0 / 60.0;
static double last_swap_ms;
static int    present_pending;
static unsigned long n_swaps, n_frames_in;
static int    last_nsegs;

static void ph_replay(void)
{
    double now = plat_now_ms();
    int i;

    beam_clear();
    for (i = 0; i < PH_FRAMES; i++) {
        /* oldest first: start one past the newest and wrap */
        int idx = (ph_cur + 1 + i) % PH_FRAMES;
        ph_frame *f = &ph_buf[idx];
        double fac;
        int k;

        if (!f->used || idx == ph_fill) continue;
        if (idx == ph_cur) fac = 1.0;
        else if (ph_tau <= 0.0) continue;
        else {
            fac = exp(-(now - f->t) / ph_tau);
            if (fac < PH_FLOOR) continue;
        }
        for (k = 0; k < f->n; k++) {
            const ph_seg *s = &f->seg[k];
            /* the vector's own 1..7 Z field scales brightness linearly */
            double b = (double)s->intensity / 7.0 * fac;
            int r = (int)(RGB_RED(s->rgb) * b + 0.5);
            int gg = (int)(RGB_GREEN(s->rgb) * b + 0.5);
            int bl = (int)(RGB_BLUE(s->rgb) * b + 0.5);
            if (r <= 0 && gg <= 0 && bl <= 0) continue;
            beam_add_line(s->x0, s->y0, s->x1, s->y1, 0xFF, MAKE_RGB(r, gg, bl));
        }
    }
    beam_draw_all(beam_proj);
}

static void present_now(void)
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ph_replay();
    glSwap();
    last_swap_ms = plat_now_ms();
    present_pending = 0;
    n_swaps++;
}

/* Called from the idle path and once per main-loop pass: swaps the newest
 * frame (vsync=0: free-running, never blocks; vsync=1/2: SetSwapMode already
 * makes glSwap wait for the display, so an occasional swap here may add a
 * little jitter to the NMI-driven pacing - vsync=0 is the tested default). */
static void present_idle(void)
{
    double now;
    if (!hWnd) return;
    now = plat_now_ms();
    if (!present_pending && !(ph_tau > 0.0 && now - last_swap_ms >= refresh_ms)) return;
    present_now();
}

/* ------------------------------------------------------------------ */
/* window procedure                                                    */
/* ------------------------------------------------------------------ */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            SCREEN_W = LOWORD(lParam);
            SCREEN_H = HIWORD(lParam);
            ViewOrthoScaled(SCREEN_W, SCREEN_H, DESIGN_W, DESIGN_H);
            update_beam_width();
            present_pending = 1;
        }
        return 0;

    case WM_DPICHANGED: {
        const RECT *suggested = (const RECT *)lParam;
        SetWindowPos(hwnd, NULL, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_ACTIVATE:
        window_active = LOWORD(wParam) != WA_INACTIVE;
        if (!window_active) memset(key, 0, sizeof key);  /* no stuck keys */
        return 0;

    case WM_SYSKEYDOWN:
        /* ALT+ENTER; lParam bit 29 is the recorded ALT context flag. */
        if (wParam == VK_RETURN && (lParam & (1 << 29))) {
            ToggleFullscreen();
            key[KEY_ENTER] = 0;
            return 0;
        }
        return DefWindowProc(hwnd, message, wParam, lParam);

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        ValidateRect(hwnd, NULL);
        present_pending = 1;
        return 0;

    case WM_SYSCOMMAND:
        /* A lone ALT release would open the window menu's modal loop (a
         * multi-second freeze); no menu, so eat it. */
        if ((wParam & 0xFFF0) == SC_KEYMENU) return 0;
        return DefWindowProc(hwnd, message, wParam, lParam);

    case WM_CLOSE:
        LOG_INFO("quit: window closed (WM_CLOSE)");
        g_quit = 1;
        return 0;

    case WM_INPUT:
        RawInput_ProcessInput(hwnd, wParam, lParam);
        return DefWindowProc(hwnd, message, wParam, lParam);

    case WM_DEVICECHANGE:
        joystick_device_change();
        return DefWindowProc(hwnd, message, wParam, lParam);

    case WM_DESTROY:
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { LOG_INFO("quit: Esc"); g_quit = 1; return 0; }
        if (wParam == VK_F2 && !(lParam & 0x40000000)) {  /* ignore auto-repeat (lParam bit 30) */
            test_switch = !test_switch;
        /* not saved to the ini: F2 is for this session only (user request) */
            LOG_INFO("self-test switch: %s (F2)", test_switch ? "ON" : "OFF");
            return 0;
        }
        return 0;

    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
}

static void pump_messages(void)
{
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { g_quit = 1; break; }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

static int pokey_volume = 100;      /* [sound] pokey_volume, percent */
static int audio_prime_blocks = 6;  /* [sound] latency_blocks */
static int samples_volume = 100;    /* [sound] samples_volume, percent */
static int mixer_ok;                /* mixer_init() succeeded */

int plat_init(void)
{
    WNDCLASS wc;
    DWORD dwStyle;
    RECT wr;
    HINSTANCE hInstance = GetModuleHandle(NULL);

    /* the tube's own span, never hard-coded: bz_platform.h's BZ_VIEW_* */
    DESIGN_W = (int)(BZ_VIEW_XMAX - BZ_VIEW_XMIN);
    DESIGN_H = (int)(BZ_VIEW_YMAX - BZ_VIEW_YMIN);
    if (DESIGN_W < 1) DESIGN_W = 1;
    if (DESIGN_H < 1) DESIGN_H = 1;

    resolve_paths();
    log_open(path_log);
    log_set_level(LOG_LEVEL_INFO);
    LOG_INFO("bz_win backend starting");

    enable_dpi_awareness();

    memset(&wc, 0, sizeof wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"BzWin";
    if (!RegisterClass(&wc)) {
        msg_box(APP_NAME, "Failed to register the window class.");
        return 1;
    }

    dwStyle = WS_CAPTION | WS_POPUPWINDOW | WS_CLIPSIBLINGS
            | WS_CLIPCHILDREN | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;
    wr.left = 0; wr.top = 0; wr.right = 800; wr.bottom = 600;
    AdjustWindowRect(&wr, dwStyle, FALSE);
    hWnd = CreateWindow(L"BzWin", L"Battlezone", dwStyle,
                        CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
                        NULL, NULL, hInstance, NULL);
    if (!hWnd) {
        msg_box(APP_NAME, "Failed to create the main window.");
        return 1;
    }

    size_window_for_dpi(hWnd, window_dpi(hWnd), dwStyle);

    set_config_file(path_ini);
    read_dips();

    /* [main] vsync: 0 off (default) / 1 on / 2 adaptive. */
    swap_mode = get_config_int("main", "vsync", 0);
    set_config_int("main", "vsync", swap_mode);

    /* [main] test_switch: 0 off (default) / 1 on - the self-test switch's
     * latch (F2 flips it at runtime; a --test command-line flag also sets
     * it, checked in WinMain after plat_init() returns). */
    test_switch = get_config_int("main", "test_switch", 0);
    set_config_int("main", "test_switch", test_switch);

    if (FAILED(RawInput_Initialize(hWnd))) {
        msg_box(APP_NAME, "Failed to register for raw input; keyboard will not work.");
        LOG_ERROR("RawInput_Initialize failed");
    }

    install_joystick();

    pokey_volume = get_config_int("sound", "pokey_volume", 100);
    if (pokey_volume < 0)   pokey_volume = 0;
    if (pokey_volume > 100) pokey_volume = 100;
    set_config_int("sound", "pokey_volume", pokey_volume);
    audio_prime_blocks = get_config_int("sound", "latency_blocks", 6);
    if (audio_prime_blocks < 1) audio_prime_blocks = 1;
    if (audio_prime_blocks > 8) audio_prime_blocks = 8;
    set_config_int("sound", "latency_blocks", audio_prime_blocks);
    samples_volume = get_config_int("sound", "samples_volume", 100);
    if (samples_volume < 0)   samples_volume = 0;
    if (samples_volume > 100) samples_volume = 100;
    set_config_int("sound", "samples_volume", samples_volume);
    if (pokey_volume > 0 || samples_volume > 0) {
        if (mixer_init() != 0)
            LOG_ERROR("mixer_init failed; continuing without audio");
        else
            mixer_ok = 1;
        stream_set_prime_blocks(audio_prime_blocks);
    }

    if (!CreateGLContext()) {
        msg_box(APP_NAME, "Failed to create an OpenGL context.");
        return 1;
    }
    swap_mode = SetSwapMode(swap_mode);
    ViewOrthoScaled(SCREEN_W, SCREEN_H, DESIGN_W, DESIGN_H);

    beam_px = get_config_float("vector", "linewidth", 2.0f);
    beam_feather_px = get_config_float("vector", "line_smoothing", 1.0f);
    if (beam_px < 0.1f) beam_px = 0.1f;
    if (beam_feather_px < 0.0f) beam_feather_px = 0.0f;
    set_config_float("vector", "linewidth", beam_px);
    set_config_float("vector", "line_smoothing", beam_feather_px);

    if (!beam_init()) {
        msg_box(APP_NAME, "Beam renderer failed to build (needs OpenGL 3.3) - see bz_win.log.");
        return 1;
    }
    beam_set_color_mode(1);
    update_beam_width();
    LOG_INFO("beam: %.2f px wide at a %d-wide window (proportional), %.2f px feather",
             beam_px, DESIGN_W, beam_feather_px);

    ph_tau = (double)get_config_float("vector", "phosphor_ms", 0.0f);
    set_config_float("vector", "phosphor_ms", (float)ph_tau);

    {
        float hz = get_config_float("main", "refresh_hz", 0.0f);
        set_config_float("main", "refresh_hz", hz);
        if (hz <= 0.0f) hz = monitor_refresh_hz(hWnd);
        if (hz < 20.0f || hz > 500.0f) hz = 60.0f;
        refresh_ms = 1000.0 / hz;
        LOG_INFO("display refresh %.2f Hz, swap mode %d", hz, swap_mode);
    }

    mat4_ortho(beam_proj, BZ_VIEW_XMIN, BZ_VIEW_XMAX, BZ_VIEW_YMIN, BZ_VIEW_YMAX, -1.0f, 1.0f);
    LOG_INFO("picture: AVG x %.0f..%.0f / y %.0f..%.0f, no rotation, no flip",
             BZ_VIEW_XMIN, BZ_VIEW_XMAX, BZ_VIEW_YMIN, BZ_VIEW_YMAX);

    timeBeginPeriod(1);

    set_window_title(APP_NAME " - " KEY_HELP);
    ShowWindow(hWnd, SW_SHOW);
    SetForegroundWindow(hWnd);
    window_active = GetForegroundWindow() == hWnd;

    return 0;
}

void plat_shutdown(void)
{
    timeEndPeriod(1);
    beam_shutdown();
    remove_joystick();
    plat_audio_close();
    mixer_end();
    DeleteGLContext();
    LOG_INFO("bz_win backend closing: lit segments %lu: %lu outside the tube (dropped), %lu cut at its edge",
             seg_total, seg_clipped, seg_cut);
    log_close();
    if (IsWindow(hWnd)) DestroyWindow(hWnd);
}

/* ------------------------------------------------------------------ */
/* video                                                               */
/* ------------------------------------------------------------------ */

void plat_video_begin(void)
{
    ph_fill = (ph_cur + 1) % PH_FRAMES;
    ph_buf[ph_fill].n = 0;
    ph_buf[ph_fill].used = 1;
}

void plat_video_line(float x0, float y0, float x1, float y1, uint32_t rgb, int intensity)
{
    ph_frame *f;
    ph_seg *s;
    int vis;
    if (ph_fill < 0) return;
    seg_total++;
    vis = clip_seg(&x0, &y0, &x1, &y1);
    if (vis == 0) { seg_clipped++; return; }
    if (vis == 2) seg_cut++;
    f = &ph_buf[ph_fill];
    if (f->n >= PH_SEGS) {
        if (!ph_dropped) {
            ph_dropped = 1;
            LOG_WARN("display list exceeded %d segments; frames are truncated", PH_SEGS);
        }
        return;
    }
    s = &f->seg[f->n++];
    s->x0 = x0; s->y0 = y0; s->x1 = x1; s->y1 = y1;
    s->rgb = (rgb_t)rgb;
    s->intensity = intensity < 1 ? 1 : intensity > 7 ? 7 : intensity;
}

void plat_video_present(void)
{
    if (ph_fill < 0) return;
    ph_buf[ph_fill].t = plat_now_ms();
    last_nsegs = ph_buf[ph_fill].n;
    ph_cur = ph_fill;
    ph_fill = -1;
    n_frames_in++;
    present_pending = 1;
    if (swap_mode == 0 && hWnd) present_now();  /* vsync off: swap right here */
}

/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */

/* Folds a 4-way digital direction (up/down/left/right, e.g. the arrow keys
 * or a d-pad hat) into the tread pair the single-stick mode synthesizes;
 * ORed into whatever the direct tread keys already set. */
static void synth_treads(int up, int down, int left, int right,
                         int *lf, int *lr, int *rf, int *rr)
{
    if (up && left)         *rf |= 1;                /* pivot: right fwd only */
    else if (up && right)   *lf |= 1;                /* pivot: left fwd only */
    else if (down && left)  *lr |= 1;                /* pivot: left rev only */
    else if (down && right) *rr |= 1;                /* pivot: right rev only */
    else if (up)            { *lf |= 1; *rf |= 1; }  /* both forward */
    else if (down)          { *lr |= 1; *rr |= 1; }  /* both reverse */
    else if (left)          { *lr |= 1; *rf |= 1; }  /* pivot left */
    else if (right)         { *lf |= 1; *rr |= 1; }  /* pivot right */
}

void plat_input_poll(plat_inputs *in)
{
    int lf = 0, lr = 0, rf = 0, rr = 0;

    memset(in, 0, sizeof *in);

    if (window_active) {
        lf |= key[KEY_E] ? 1 : 0;
        lr |= key[KEY_D] ? 1 : 0;
        rf |= key[KEY_I] ? 1 : 0;
        rr |= key[KEY_K] ? 1 : 0;

        synth_treads(key[KEY_UP] != 0, key[KEY_DOWN] != 0, key[KEY_LEFT] != 0, key[KEY_RIGHT] != 0,
                     &lf, &lr, &rf, &rr);

        in->fire   = (key[KEY_SPACE] || key[KEY_LCONTROL]) ? 1 : 0;
        in->start1 = key[KEY_1] ? 1 : 0;
        in->start2 = key[KEY_2] ? 1 : 0;
        in->coin1  = key[KEY_5] ? 1 : 0;
        in->coin2  = key[KEY_6] ? 1 : 0;
        in->diag   = key[KEY_F1] ? 1 : 0;
        in->slam   = key[KEY_F4] ? 1 : 0;
        in->reset  = key[KEY_F3] ? 1 : 0;  /* the board's RESET (core edge-detects) */
    }
    in->test = (uint8_t)test_switch;  /* F2 latch, toggled in WndProc's WM_KEYDOWN */
    in->quit = (key[KEY_ESC] || g_quit) ? 1 : 0;
    if (in->quit) g_quit = 1;

    /* joystick/gamepad: left stick Y = left tread, right stick Y ("Ry", the
     * usual DirectInput slot for a second analog stick) = right tread
     * (built-in ~0.5 digital threshold, see joystick.c's d1/d2); d-pad hat =
     * single-stick mode; button 1 = fire, Start = start1, Back = coin1. */
    poll_joystick();
    if (num_joysticks > 0 && joystick_is_connected(0)) {
        int s;
        lf |= joy[0].stick[0].axis[1].d1;
        lr |= joy[0].stick[0].axis[1].d2;
        for (s = 1; s < joy[0].num_sticks; s++) {
            if (joy[0].stick[s].name && !strcmp(joy[0].stick[s].name, "Ry")) {
                rf |= joy[0].stick[s].axis[0].d1;
                rr |= joy[0].stick[s].axis[0].d2;
                break;
            }
        }
        for (s = 0; s < joy[0].num_sticks; s++) {
            if (joy[0].stick[s].name && !strcmp(joy[0].stick[s].name, "hat")) {
                synth_treads(joy[0].stick[s].axis[1].d1, joy[0].stick[s].axis[1].d2,
                             joy[0].stick[s].axis[0].d1, joy[0].stick[s].axis[0].d2,
                             &lf, &lr, &rf, &rr);
                break;
            }
        }
        if (joy[0].num_buttons > 0 && joy[0].button[0].b) in->fire   = 1;
        if (joy[0].num_buttons > 7 && joy[0].button[7].b) in->start1 = 1;
        if (joy[0].num_buttons > 6 && joy[0].button[6].b) in->coin1  = 1;
    }

    in->left_fwd  = (uint8_t)(lf ? 1 : 0);
    in->left_rev  = (uint8_t)(lr ? 1 : 0);
    in->right_fwd = (uint8_t)(rf ? 1 : 0);
    in->right_rev = (uint8_t)(rr ? 1 : 0);
}

/* ------------------------------------------------------------------ */
/* audio: the core's mixed mono stream (POKEY + discrete sounds)       */
/* ------------------------------------------------------------------ */

static unsigned long audio_pushes;
static int audio_peak;
static int audio_open_ok;

int plat_audio_open(int sample_rate)
{
    if (pokey_volume <= 0) {
        LOG_INFO("audio stream off ([sound] pokey_volume=0)");
        return -1;
    }
    if (stream_open(sample_rate, 1) != 0) {
        LOG_ERROR("audio: stream_open(%d Hz mono) failed; no sound", sample_rate);
        return -1;
    }
    stream_set_volume(mixer_percent_to_byte(pokey_volume));
    LOG_INFO("audio device open: XAudio2 stream, %d Hz mono int16, %d prime blocks, volume %d%%",
             sample_rate, audio_prime_blocks, pokey_volume);
    audio_open_ok = 1;
    return 0;
}

void plat_audio_push(const int16_t *pcm, int frames)
{
    int i;
    audio_pushes++;
    for (i = 0; i < frames; i++) {
        int v = pcm[i] < 0 ? -(int)pcm[i] : (int)pcm[i];
        if (v > audio_peak) audio_peak = v;
    }
    stream_push(pcm, frames);
}

void plat_audio_close(void)
{
    stream_close();
    audio_open_ok = 0;
}

/* ------------------------------------------------------------------ */
/* time                                                                */
/* ------------------------------------------------------------------ */

double plat_now_ms(void)
{
    LARGE_INTEGER cnt;
    static LARGE_INTEGER freq;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart * 1000.0 / (double)freq.QuadPart;
}

/* The core's idle hint: pump the window, present the newest frame (a
 * vsynced swap is the sleep), else sleep - never longer than
 * bz_app_idle_ms() reports is safe, whatever ms was asked for. */
void plat_sleep_ms(int ms)
{
    unsigned long swaps0 = n_swaps;
    double idle = bz_app_idle_ms();
    if (idle >= 0.0 && (double)ms > idle) ms = (int)idle;
    if (ms < 0) ms = 0;
    pump_messages();
    if (g_quit) return;
    present_idle();
    if (n_swaps == swaps0 && ms > 0) Sleep((DWORD)ms);
}

/* ------------------------------------------------------------------ */
/* recorded samples: AAE's mixer.c, thin                               */
/* ------------------------------------------------------------------ */

/* [samples] dir = the folder (relative to the exe's folder unless absolute),
 * default samples (c_src\samples); [samples] <name> = the wav file for a logical sample
 * name (AAE's bzonesamples[] names), default <NAME>.WAV - with fire1 ->
 * FIRE.WAV, the one name the MAME .SAM set spells differently.  Swap a sound
 * by pointing its key at another file. */
int plat_sample_load(const char *name)
{
    char dir[MAX_PATH], def[64], file[MAX_PATH], path[MAX_PATH * 2];
    const char *s;
    int n, i;

    if (!mixer_ok || samples_volume <= 0) return -1;

    s = get_config_string("samples", "dir", "samples");
    snprintf(dir, sizeof dir, "%s", s);
    set_config_string("samples", "dir", dir);

    if (!strcmp(name, "fire1")) snprintf(def, sizeof def, "FIRE.WAV");
    else {
        for (i = 0; name[i] && i < (int)sizeof def - 5; i++)
            def[i] = (char)((name[i] >= 'a' && name[i] <= 'z') ? name[i] - 32 : name[i]);
        snprintf(def + i, sizeof def - (size_t)i, ".WAV");
    }
    s = get_config_string("samples", name, def);
    snprintf(file, sizeof file, "%s", s);
    set_config_string("samples", name, file);

    if (dir[0] == '\\' || dir[0] == '/' || (dir[0] && dir[1] == ':'))
        snprintf(path, sizeof path, "%s\\%s", dir, file);
    else
        snprintf(path, sizeof path, "%s%s\\%s", exe_dir, dir, file);

    n = load_sample(NULL, path);
    if (n < 0) LOG_ERROR("sample '%s': cannot load %s (silent)", name, path);
    else       LOG_INFO("sample '%s' = %s (#%d)", name, path, n);
    return n;
}

void plat_sample_start(int chan, int smp, int loop)
{
    if (!mixer_ok || smp < 0) return;
    sample_start(chan, smp, loop);
    sample_set_volume(chan, samples_volume * 255 / 100);
}
void plat_sample_stop(int chan)            { if (mixer_ok) sample_stop(chan); }
int  plat_sample_playing(int chan)         { return mixer_ok ? sample_playing(chan) : 0; }
void plat_sample_set_freq(int chan, int f) { if (mixer_ok) sample_set_freq(chan, f); }
int  plat_sample_get_freq(int chan)        { return mixer_ok ? sample_get_freq(chan) : 0; }
void plat_sample_set_volume(int chan, int vol)
{
    if (mixer_ok) sample_set_volume(chan, vol * samples_volume / 100);
}

/* ------------------------------------------------------------------ */
/* status + main loop                                                  */
/* ------------------------------------------------------------------ */

/* The core calls this ~1/s.  Its line (passes, NMIs, pictures ...) goes to
 * the log; the title bar shows the frame rate, as the other ports do: pictures
 * presented per second (plat_video_present), a rolling average over the last
 * FPS_AVG_N calls so one crowded or empty second does not make it jump. */
#define FPS_AVG_N 5
void plat_status_text(const char *s)
{
    static double fps_frames[FPS_AVG_N], fps_secs[FPS_AVG_N], last_ms;
    static unsigned long last_frames;
    static int fps_slot;
    char buf[320];
    char st[160];
    double now = plat_now_ms(), fps = 0.0, fsum = 0.0, ssum = 0.0;
    int k;

    if (last_ms > 0.0 && now > last_ms) {
        fps_frames[fps_slot] = (double)(n_frames_in - last_frames);
        fps_secs[fps_slot] = (now - last_ms) / 1000.0;
        fps_slot = (fps_slot + 1) % FPS_AVG_N;
        for (k = 0; k < FPS_AVG_N; k++) { fsum += fps_frames[k]; ssum += fps_secs[k]; }
        if (ssum > 0.0) fps = fsum / ssum;
    }
    last_ms = now;
    last_frames = n_frames_in;

    snprintf(buf, sizeof buf, APP_NAME "  %.1f fps%s  - " KEY_HELP,
             fps, test_switch ? "  [SELF TEST]" : "");
    set_window_title(buf);
    LOG_INFO("%s  (%.1f fps)", s, fps);
    if (audio_open_ok) {
        stream_stats(st, sizeof st);
        LOG_INFO("%s", st);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int iCmdShow)
{
    double t_start;
    (void)hInstance; (void)hPrevInstance; (void)iCmdShow;

    if (plat_init()) { log_close(); return 1; }
    if (lpCmdLine && strstr(lpCmdLine, "--test")) {
        test_switch = 1;  /* --test: switch held on from power-on */
            /* --test: this session only, not saved to the ini */
        LOG_INFO("self-test switch: ON (--test)");
    }
    bz_app_init();
    LOG_INFO("power-on done: entering the main loop");

    t_start = plat_now_ms();

    /* The core calls plat_status_text ~1/s on its own (bz_platform.h); the
     * backend just pumps messages, runs one pass and presents. */
    while (!g_quit) {
        pump_messages();
        if (g_quit) break;

        bz_app_step(plat_now_ms());
        present_idle();
    }

    LOG_INFO("exit: %.2f s, %lu frames in, %lu swaps, %lu audio pushes (peak %d), last picture %d segs",
             (plat_now_ms() - t_start) / 1000.0, n_frames_in, n_swaps, audio_pushes, audio_peak, last_nsegs);

    bz_app_exit();
    plat_shutdown();
    return 0;
}
