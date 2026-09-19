/* refrun.c - Battlezone reference harness: the real ROM on ref6502.
 *
 * The oracle the C port will be diffed against once translation starts
 * (PLAN.md, "Verification").  Runs the program ROM ($5000-$7FFF) and vector
 * ROM ($3000-$3FFF), as generated into progrom.c / vecrom.c, on the shared
 * NMOS core C:\Source2026\shared\ref6502 (never a new core; copied verbatim
 * into tests\ref6502\), inside the Battlezone memory map documented in
 * ../../disasm/HARDWARE_NOTES.md (cross-checked against ../bzone.cpp), and
 * dumps RAM $0000-$03FF + vector RAM $2000-$2FFF at every pass through the
 * top of MAIN ($5000).
 *
 * Adapted from the Tempest port's tests/refrun.c (same command-line shape
 * and dump layout); differences follow from Battlezone's own hardware:
 *   - NMI, not IRQ: the 6502's NMI line is edge-triggered and unmaskable
 *     (no I-flag polling needed); the board's free-running NMI clock is
 *     itself gated by IN0 bit 4 (self-test switch) at the moment it fires
 *     (HARDWARE_NOTES.md "NMI: rate, source, and a header typo").
 *   - GOADD's write value is not a bare strobe: it is the vector-RAM
 *     buffer's page byte ($20 or $28), matching VGLIST at that point - see
 *     "GOADD" below and NOTES_m0.md.
 *   - No EAROM, no colour RAM (Battlezone's AVG is monochrome).
 *   - POKEY RANDOM/ALLPOT go through the real c012294 chip model (harvested
 *     verbatim from the Tempest port), not an inline LFSR - ALLPOT carries
 *     the tread switches, fire and start buttons (HARDWARE_NOTES.md "POKEY
 *     ($1820-$182F)"), which the harness needs for input scripting.
 *
 *   tests\refrun.exe [--frames N] [--outdir DIR (tests\ref)] [--capture-every K]
 *                     [--snap-pc HEX (5004)] [--no-dumps] [--mbtest] [--script FILE]
 *                     [--watchdog-cycles N (1000000)]
 *
 * Hardware model (the CONTRACT - the port's hw_* seam must reproduce it):
 *  - Clock 1.512 MHz (HARDWARE_NOTES.md "CPU and clock").  Cycles = standard
 *    NMOS base-cycle table, +1 for a taken branch, no page-cross penalty,
 *    7 per interrupt entry.
 *  - NMI: free-running board clock, 6144 CPU cycles (246.09 Hz), delivered
 *    only while IN0 bit 4 is SET (self-test off = normal play).  Battlezone's
 *    NMI is a true edge-triggered 6502 NMI (unlike Tempest's IRQ-wired-as-NMI
 *    template): taken unconditionally at the next instruction boundary once
 *    raised, no I-flag gate.
 *  - IN0 $0800: b0/b1 coin1/coin2 (active low, idle 1), b2/b3 unused (idle 1,
 *    though BZONE.MAC reads b3 as the slam switch - HARDWARE_NOTES.md flags
 *    the AAE driver as not wiring it; scriptable anyway), b4 self-test
 *    (active low; idle 1 = off = normal play - board default, bzone.cpp
 *    PORT_DIPSETTING(0x10,Off)), b5 diag step (active low, idle 1), b6 VG
 *    HALT (derived from the display list, see vg_halted()), b7 3 kHz clock
 *    = (cycle >> 8) & 1 (same 256-cycle half period as Tempest's board -
 *    same 12.096 MHz / 4096 chain, HARDWARE_NOTES.md).
 *  - GOADD ($1200 write): the data byte is the vector-RAM buffer's page
 *    ($20 or $28 - BZONE.MAC's NMI computes exactly this value into A and
 *    "STA GOADD"s it unchanged, right after using the SAME value for
 *    BUFRDY).  MAIN's own code sets VGLIST = (that page << 8) + 2 - the
 *    display list therefore starts two bytes into the page (the 2-byte
 *    "header" FINISH sometimes uses for the TRAP1 security byte, never
 *    read by the AVG).  Modelled: vg_start_addr = (page << 8) + 2.  VSTOP
 *    ($1600 write) halts at once.  VG HALT (IN0 b6, polarity checked against
 *    BZONE.MAC's BIT HALT/BVS and against ../bzone.cpp's avgdvg_done()) is
 *    derived from the AUTHENTIC state-PROM draw time, not an approximation:
 *    a list that loops back to its own start (or is malformed) never halts
 *    on its own; one that reaches HALT reads halted ceil(avg_cycles/8) CPU
 *    cycles after the GOADD that started it, where avg_cycles is the real
 *    per-opcode + per-vector state-PROM tick sum (avg_walk(), avg.h/avg.c -
 *    see its own header comment - the AVG state PROM is byte-identical
 *    between Battlezone and Tempest, so this is the same hardware timing
 *    Tempest's oracle uses, not a Battlezone-specific approximation).
 *  - OPTION $0A00 / OPTON2 $0C00: driver defaults (bzone.cpp INPUT_PORTS_START
 *    (bzone), read directly - PORT_DIPNAME default values) OPTION=$15 (3
 *    lives, missile at 10000, bonus 15k/100k, English), OPTON2=$02 (1 coin/1
 *    play, coin B x1, coin A x1, no bonus coins).
 *  - POKEY 1 chip at $1820, clocked at 1.512 MHz (HARDWARE_NOTES.md "POKEY
 *    ($1820-$182F)"): the real c012294 model (c012294.c, harvested verbatim
 *    from the Tempest port per PLAN.md).  RANDOM ($182A) is the chip's own
 *    RNG chain.  ALLPOT ($1828) is set from the script's switch state
 *    (right-down/up, left-down/up tread bits, fire, start1, start2 - the
 *    "IN3" bit layout bzone_IN3_r builds, idle 0 = no switches closed) via
 *    ad_pokey_set_allpot(); the game strikes POTGO then reads ALLPOT.
 *  - Mathbox: mathbox.c, the real microcode PROMs (mbprom.c, generated by
 *    tools/gen_roms.py from the game's own PROM dump and verified
 *    byte-identical to Tempest's there), completes inside the write.
 *  - Watchdog $1400 (WTCHDG): counted; the NMI handler kicks it every NMI
 *    (~4.06 ms) in normal play, so any reasonable timeout is never
 *    approached in an attract run - tracked (kicks, max gap) and flagged in
 *    the summary if a gap exceeds --watchdog-cycles, but this harness does
 *    NOT reset the CPU on a bite (M0 does not need self-test/watchdog-bite
 *    scripting; PLAN.md's exit criterion is just that it never fires).
 *
 * Frame = one pass through the top of MAIN.  MAIN ($5000) is "LSR SYNC /
 * BCC MAIN" (MAINLINE_NOTES.md "Frame sync") - a spin loop revisited many
 * times per NMI while waiting for SYNC's carry bit, so capturing PC==$5000
 * itself would produce many dumps per real frame.  --snap-pc instead
 * defaults to $5004 (JSR VOLCNO, the instruction right after the BCC falls
 * through - reached exactly once per real frame, symmetric with the
 * Tempest/Space Duel oracles' "after the frame wait" capture point).
 * Captured: frames 1-16, then every K-th (default 32).  Writes
 * DIR/frame_NNNN.ram (1024) .vram (4096), DIR/frame_sched.txt (NMI count and
 * the latest GOADD's authentic AVG draw time, in CPU cycles and ms, at every
 * pass - see "AVG draw time" above, avg_walk() (avg.h) below), DIR/ref_index.json
 * and DIR/coverage_pcs.txt.  The summary also reports the run's average/min/max
 * draw time and how many 6-NMI VTIMER windows (VSTOP writes) found the AVG
 * still busy from the previous GOADD.
 *
 * Scripted inputs (--script FILE) - lines "PASS PORT [VALUE]", '#' comments,
 * non-decreasing PASS order, applied at the start of the given pass (PASS 0
 * = at power-on, before cpu_reset):
 *  - byte ports: in0, option, opton2 (whole-byte; a whole-byte in0 line sets
 *    the test/diag bits too).
 *  - bit ports, VALUE 1 = active (pressed / closed / on), 0 = released:
 *      IN0 ($0800, active low): coin1 b0, coin2 b1, slam b3, test b4
 *                                (self-test switch), diag b5
 *      ALLPOT switches (active high, HARDWARE_NOTES.md "IN3"): rt_down b0,
 *                                rt_up b1, lt_down b2, lt_up b3, fire b4,
 *                                start1 b5, start2 b6
 *  - "PASS end" stops the run there.
 *
 * Exit 0 = the run reached N passes with the checks in the summary passing.
 */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir((p), 0777)
#endif

#include "ref6502.h"
#include "mathbox.h"
#include "c012294.h"
#include "progrom.h"
#include "vecrom.h"
#include "avg.h"
#include "../state_defs.h"   /* A_ATRACT / A_GOVER / A_SYNC address constants only -
                               * this file keeps its own local ram[]/vram[], not
                               * machine_state g, so the g-based lvalue macros in
                               * state_defs.h are simply unused, never expanded */

/* ------------------------------------------------------------------ */
/* machine                                                             */
/* ------------------------------------------------------------------ */

#define NMI_CYCLES 6144u  /* 246.09 Hz at 1.512 MHz - HARDWARE_NOTES.md */

static uint8_t ram[0x0400];   /* $0000-$03FF */
static uint8_t vram[0x1000];  /* $2000-$2FFF */

static uint64_t cyc;  /* cycle count at the current instruction */
static uint64_t vg_busy_until;

static uint8_t peek(uint16_t addr);  /* no side effects; defined below */

static mathbox mb;
static ad_pokey pokey;
static uint64_t pokey_cyc;

/* statistics */
static unsigned long n_nmi_raised, n_nmi_taken, n_nmi_lost;
static unsigned long n_vggo, n_vgstop, n_wdog, n_vram_writes;
static unsigned long n_random, n_allpot_reads, n_pokey_w, n_emcnt_w, n_sound_w;
static unsigned long n_unmapped_r, n_unmapped_w, n_brk;
static unsigned long n_dummy_wait_r;  /* $6B79 CMP ($00,X) - harmless, see mem_read_io() */
static uint16_t first_unmapped_r[8], first_unmapped_w[8];
static uint16_t first_unmapped_r_pc[8], first_unmapped_w_pc[8];  /* PC that did the access */
static uint8_t  pc_hit[0x10000 / 8];
static unsigned long n_vg_walks;
static unsigned long n_goadd_draws, n_vstop_while_busy;
static uint64_t sum_draw_cycles, min_draw_cycles = (uint64_t)-1, max_draw_cycles;
static uint32_t last_draw_cycles;

#define WD_DEFAULT_CYCLES 1000000u   /* generous; the NMI kicks it every ~6144
                                       * cycles in normal play - see header */
static uint64_t wd_cycles = WD_DEFAULT_CYCLES;
static uint64_t last_kick_cyc, max_kick_gap_cyc;

enum { AVG_HALT, AVG_LOOP, AVG_BAD, AVG_CAP };
static int shape_walk(uint16_t start_pc, unsigned *vectors, unsigned *bright, unsigned *ops);

/* ---- AVG draw time: authentic state-PROM timing, via c_src/avg.c --------
 *
 * PLAN.md M1 item 3: c_src/avg.c now exposes avg_result.cycles (M0v's Gate
 * T), so the local timing stand-in this file used to carry (avg_draw_cycles/
 * avg_norm_shifts/avg_timer_reg, transcribed from Tempest's avg.c as an
 * interim measure - see NOTES_m0.md "AVG timing") is gone; every GOADD below
 * calls avg_walk() (avg.h, the real Gate-V/Gate-T walker) for the draw-cycle
 * count instead.  This file's OWN shape_walk() (below - renamed from
 * avg_walk() to stop colliding with avg.h's identifier of the same name; no
 * behaviour change) still does the separate HALT/LOOP/BAD/CAP classification
 * used for vg_ends_in_halt and the final display-list summary print - both
 * walkers decode the identical opcode stream (already cross-checked against
 * ../../disasm/avg.py - NOTES_m0.md), so replacing only the cycle-counting
 * engine is a pure deletion, not a behaviour change; NOTES_m1.md confirms
 * `--frames 600`'s draw-time statistics are unchanged before/after this
 * swap. */

/* VG HALT (IN0 b6; polarity checked against BZONE.MAC's "BIT HALT/BVS" and
 * BZSTST.MAC's self-test poll, both of which treat bit 6 set as "done" -
 * HARDWARE_NOTES.md's IN0 table, "D6 IN0_VG_HALT ... set when the vector
 * generator has finished/halted", matches ../bzone.cpp's avgdvg_done()).
 * GOADD's data byte is the buffer page (see the GOADD case in mem_write);
 * VGLIST is set to (page<<8)+2 by MAIN right before the buffer is built, so
 * that is where the AVG is modelled as starting too.  Draw time and the
 * HALT/LOOP/BAD classification are both computed eagerly at the GOADD that
 * starts the list (mem_write), not lazily here: a list that does not reach
 * HALT (loops, or a malformed list) never reports halted on its own - only
 * VSTOP or the next GOADD change vg_running - exactly like a real AVG that
 * free-runs a looping list forever until reset. */
static int      vg_running;
static uint16_t vg_start_addr = 0x2002;
static uint64_t vg_start_cyc;
static int      vg_ends_in_halt;

static int vg_halted(void)
{
    if (!vg_running) return 1;
    if (!vg_ends_in_halt) return 0;
    return cyc >= vg_busy_until;
}

/* ---- scripted inputs (--script FILE; syntax in the header) --------------- */
static uint8_t in_in0 = 0x3F;        /* coins/slam/test/diag idle; b6/b7 synthesized */
static uint8_t in_option = 0x15;     /* DSW0 default (bzone.cpp) */
static uint8_t in_opton2 = 0x02;     /* DSW1 default (bzone.cpp) */
static uint8_t in_allpot_sw = 0x00;  /* IN3-style switch byte (see header) */

#define MAX_SCRIPT 4096
enum { SC_BYTE, SC_BIT_LOW, SC_BIT_HIGH, SC_END };
static struct { unsigned pass; int kind; uint8_t *port; uint8_t mask; uint8_t val; } script[MAX_SCRIPT];
static int script_n, script_pos;
static const char *script_path;

static const struct { const char *name; int kind; uint8_t *port; uint8_t mask; } script_ports[] = {
    { "in0", SC_BYTE, &in_in0, 0 },      { "option", SC_BYTE, &in_option, 0 },
    { "opton2", SC_BYTE, &in_opton2, 0 }, { "allpot", SC_BYTE, &in_allpot_sw, 0 },
    { "coin1", SC_BIT_LOW, &in_in0, 0x01 }, { "coin2", SC_BIT_LOW, &in_in0, 0x02 },
    { "slam", SC_BIT_LOW, &in_in0, 0x08 },  { "test", SC_BIT_LOW, &in_in0, 0x10 },
    { "diag", SC_BIT_LOW, &in_in0, 0x20 },
    { "rt_down", SC_BIT_HIGH, &in_allpot_sw, 0x01 }, { "rt_up", SC_BIT_HIGH, &in_allpot_sw, 0x02 },
    { "lt_down", SC_BIT_HIGH, &in_allpot_sw, 0x04 }, { "lt_up", SC_BIT_HIGH, &in_allpot_sw, 0x08 },
    { "fire", SC_BIT_HIGH, &in_allpot_sw, 0x10 },
    { "start1", SC_BIT_HIGH, &in_allpot_sw, 0x20 }, { "start2", SC_BIT_HIGH, &in_allpot_sw, 0x40 },
    { "end", SC_END, NULL, 0 },
};

static int load_script(const char *path)
{
    char line[256], port[32];
    unsigned pass, last_pass = 0;
    int val;
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "REFRUN: cannot open script %s\n", path); return 1; }
    while (fgets(line, sizeof line, f)) {
        char *h = strchr(line, '#');
        if (h) *h = 0;
        int nf = sscanf(line, "%u %31s %i", &pass, port, &val);
        if (nf < 2) continue;
        int k = -1;
        for (size_t i = 0; i < sizeof script_ports / sizeof script_ports[0]; i++)
            if (!strcmp(port, script_ports[i].name)) { k = (int)i; break; }
        int is_event = k >= 0 && script_ports[k].kind == SC_END;
        if (k < 0 || script_n >= MAX_SCRIPT || (is_event ? nf != 2 : nf != 3) || pass < last_pass ||
            (script_ports[k].kind >= SC_BIT_LOW && !is_event && val != 0 && val != 1)) {
            fprintf(stderr, "REFRUN: bad script line: %s", line);
            fclose(f);
            return 1;
        }
        last_pass = pass;
        script[script_n].pass = pass;
        script[script_n].kind = script_ports[k].kind;
        script[script_n].port = script_ports[k].port;
        script[script_n].mask = script_ports[k].mask;
        script[script_n].val = is_event ? 0 : (uint8_t)val;
        script_n++;
    }
    fclose(f);
    return 0;
}

/* Applies the lines up to `pass`.  Returns SC_END if an end line was hit. */
static int apply_script(unsigned pass)
{
    int ev = 0;
    while (script_pos < script_n && script[script_pos].pass <= pass) {
        uint8_t *p = script[script_pos].port, m = script[script_pos].mask, v = script[script_pos].val;
        switch (script[script_pos].kind) {
        case SC_BYTE:     *p = v; break;
        case SC_BIT_LOW:  *p = (uint8_t)(v ? (*p & ~m) : (*p | m)); break;
        case SC_BIT_HIGH: *p = (uint8_t)(v ? (*p | m) : (*p & ~m)); break;
        default:          ev = SC_END; break;
        }
        script_pos++;
    }
    ad_pokey_set_allpot(&pokey, in_allpot_sw);
    return ev;
}

static uint8_t in0_byte(void)
{
    uint8_t v = (uint8_t)(in_in0 & 0x3F);
    if (vg_halted())     v |= 0x40;  /* b6: VG HALT */
    if ((cyc >> 8) & 1u) v |= 0x80;  /* b7: 3 kHz */
    return v;
}

static uint16_t cur_pc;  /* PC of the instruction currently executing (forward use below) */

static void note_unmapped(uint16_t *list, uint16_t *pcs, unsigned long *n, uint16_t a)
{
    if (*n < 8) { list[*n] = a; pcs[*n] = cur_pc; }
    (*n)++;
}

static uint8_t peek(uint16_t addr)  /* no side effects */
{
    uint16_t a = (uint16_t)(addr & 0x7FFF);  /* 15-bit decode, HARDWARE_NOTES.md */
    if (a < 0x0400) return ram[a];
    if (a >= 0x2000 && a < 0x3000) return vram[a - 0x2000];
    if (a >= 0x3000 && a < 0x4000) return vecrom[a - 0x3000];
    if (a >= 0x5000 && a < 0x8000) return progrom[a - 0x5000];
    return 0;
}

static void pokey_sync(void)
{
    if (cyc > pokey_cyc) {
        ad_pokey_advance(&pokey, (uint32_t)(cyc - pokey_cyc));
        pokey_cyc = cyc;
    }
}

static uint8_t mem_read_io(uint16_t a)
{
    if (a >= 0x1820 && a < 0x1830) {
        pokey_sync();
        uint8_t reg = (uint8_t)(a & 0x0F);
        if (reg == R_RANDOM) n_random++;
        if (reg == R_ALLPOT) n_allpot_reads++;
        return ad_pokey_read(&pokey, reg);
    }
    switch (a) {
    case 0x0800: return in0_byte();
    case 0x0A00: return in_option;
    case 0x0C00: return in_opton2;
    case 0x1800: return mb_status(&mb);
    case 0x1810: return mb_ylow(&mb);
    case 0x1818: return mb_yhigh(&mb);
    default: break;
    }
    /* PLAN.md M1 item 4: the harmless dummy read at $6B79 (CMP ($00,X)) is a
     * deliberate Mathbox-wait "waste time" instruction (../../disasm/
     * CALLING_NOTES.md, the WAIT section) - an open-bus read with no effect,
     * not a genuine unmapped access, so it is counted and reported
     * separately instead of going through note_unmapped(). */
    if (cur_pc == 0x6B79) { n_dummy_wait_r++; return 0x00; }
    note_unmapped(first_unmapped_r, first_unmapped_r_pc, &n_unmapped_r, a);
    return 0x00;
}

static uint8_t mem_read(uint16_t a)
{
    if (a < 0x0400 || (a >= 0x2000 && a < 0x4000) || a >= 0x5000) return peek(a);
    return mem_read_io(a);
}

static void mem_write(uint16_t a, uint8_t v)
{
    if (a < 0x0400) { ram[a] = v; return; }
    if (a >= 0x2000 && a < 0x3000) { vram[a - 0x2000] = v; n_vram_writes++; return; }
    if (a >= 0x1820 && a < 0x1830) { pokey_sync(); ad_pokey_write(&pokey, (uint8_t)(a & 0x0F), v); n_pokey_w++; return; }
    if (a >= 0x1860 && a < 0x1880) { mb_write(&mb, (uint8_t)(a - 0x1860), v); return; }
    switch (a) {
    case 0x1000: n_emcnt_w++; return;
    case 0x1200: {  /* GOADD: data byte = buffer page (see header) */
        unsigned vv, bb, oo;
        n_vggo++;
        vg_running = 1;
        vg_start_addr = (uint16_t)(((uint16_t)v << 8) + 2);
        vg_start_cyc = cyc;
        n_vg_walks++;
        vg_ends_in_halt = (shape_walk(vg_start_addr, &vv, &bb, &oo) == AVG_HALT);
        {  /* PLAN.md M1 item 3: avg.c's real walker */
            avg_mem am;
            avg_result ar;
            am.vram = vram;
            am.vrom = vecrom;
            ar = avg_walk(&am, vg_start_addr, NULL, NULL);
            /* ar.cycles is in AVG master-clock cycles (avg.h TIMING); convert
             * to CPU cycles - ceil(avg_cycles/8), exactly as this file's own
             * header already documents and as Tempest's app_loop.c does
             * (`(r.cycles + AVG_CYC_PER_CPU - 1u) / AVG_CYC_PER_CPU`).  Bug
             * found by NOTES_m1.md's before/after check: without this
             * division, draw times and the VTIMER-busy count came out ~8x
             * too high (avg 124.9 ms instead of 15.6 ms per list, 99.8%
             * VSTOP-while-busy instead of 0.2% - see NOTES_m1.md). */
            last_draw_cycles = (ar.cycles + AVG_CYC_PER_CPU - 1u) / AVG_CYC_PER_CPU;
                                                            /* computed regardless: logged even
                                                             * for a looping/bad list */
        }
        vg_busy_until = vg_start_cyc + last_draw_cycles;
        n_goadd_draws++;
        sum_draw_cycles += last_draw_cycles;
        if (last_draw_cycles < min_draw_cycles) min_draw_cycles = last_draw_cycles;
        if (last_draw_cycles > max_draw_cycles) max_draw_cycles = last_draw_cycles;
        return;
    }
    case 0x1400:  /* WTCHDG */
        n_wdog++;
        if (cyc - last_kick_cyc > max_kick_gap_cyc) max_kick_gap_cyc = cyc - last_kick_cyc;
        last_kick_cyc = cyc;
        return;
    case 0x1600:  /* VSTOP: halts at once */
        n_vgstop++;
        if (vg_running && !vg_halted()) n_vstop_while_busy++;   /* the AVG was still
                                                                  * busy when the NMI's
                                                                  * 6-NMI VTIMER window
                                                                  * forced a halt */
        vg_running = 0;
        return;
    case 0x1840: n_sound_w++; return;  /* SOUND latch */
    default: break;
    }
    note_unmapped(first_unmapped_w, first_unmapped_w_pc, &n_unmapped_w, a);
}

/* NMOS base cycles per opcode (0 = undocumented; ref6502 exits on those). */
static const uint8_t base_cycles[256] = {
/*        0 1 2 3 4 5 6 7 8 9 A B C D E F */
/* 0 */   7,6,0,0,0,3,5,0,3,2,2,0,0,4,6,0,
/* 1 */   2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
/* 2 */   6,6,0,0,3,3,5,0,4,2,2,0,4,4,6,0,
/* 3 */   2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
/* 4 */   6,6,0,0,0,3,5,0,3,2,2,0,3,4,6,0,
/* 5 */   2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
/* 6 */   6,6,0,0,0,3,5,0,4,2,2,0,5,4,6,0,
/* 7 */   2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
/* 8 */   0,6,0,0,3,3,3,0,2,0,2,0,4,4,4,0,
/* 9 */   2,6,0,0,4,4,4,0,2,5,2,0,0,5,0,0,
/* A */   2,6,2,0,3,3,3,0,2,2,2,0,4,4,4,0,
/* B */   2,5,0,0,4,4,4,0,2,4,2,0,4,4,4,0,
/* C */   2,6,0,0,3,3,5,0,2,2,2,0,4,4,6,0,
/* D */   2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
/* E */   2,6,0,0,3,3,5,0,2,2,2,0,4,4,6,0,
/* F */   2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
};

static int is_branch(uint8_t op) { return (op & 0x1F) == 0x10; }

/* ------------------------------------------------------------------ */
/* AVG list walk (summary only: how much the display list draws).  Same
 * opcode table as Tempest's oracle (../../disasm/avg.py confirms the
 * encoding is identical): 0 VCTR(4) 1 HALT 2 SVEC(2) 3 STAT/SCAL(2)
 * 4 CNTR(2) 5 JSRL 6 RTSL 7 JMPL.                                     */
/* ------------------------------------------------------------------ */

static int shape_walk(uint16_t start_pc, unsigned *vectors, unsigned *bright, unsigned *ops)
{
    uint16_t pc = start_pc, stack[8];
    int sp = 0;
    *vectors = *bright = *ops = 0;
    while (*ops < 100000u) {
        if (pc < 0x2000 || pc >= 0x4000) return AVG_BAD;
        uint16_t w = (uint16_t)(peek(pc) | (peek((uint16_t)(pc + 1)) << 8));
        unsigned top = w >> 13;
        (*ops)++;
        switch (top) {
        case 0: { uint16_t w2 = (uint16_t)(peek((uint16_t)(pc + 2)) | (peek((uint16_t)(pc + 3)) << 8));
                  (*vectors)++; if (w2 >> 13) (*bright)++; pc = (uint16_t)(pc + 4); break; }
        case 1: return AVG_HALT;
        case 2: (*vectors)++; if ((w >> 5) & 7) (*bright)++; pc = (uint16_t)(pc + 2); break;
        case 5: if (sp >= 8) return AVG_BAD;
                stack[sp++] = (uint16_t)(pc + 2);
                pc = (uint16_t)(0x2000 + ((w & 0x1FFF) << 1)); break;
        case 6: if (sp == 0) return AVG_BAD;
                pc = stack[--sp]; break;
        case 7: pc = (uint16_t)(0x2000 + ((w & 0x1FFF) << 1));
                if (pc == start_pc && sp == 0 && *ops > 1) return AVG_LOOP;
                break;
        default: pc = (uint16_t)(pc + 2); break;
        }
    }
    return AVG_CAP;
}

static const char *avg_end_name(int e)
{
    return e == AVG_HALT ? "HALT" : e == AVG_LOOP ? "LOOP" : e == AVG_BAD ? "BAD" : "CAP";
}

/* ------------------------------------------------------------------ */
/* dumps                                                               */
/* ------------------------------------------------------------------ */

static const char *outdir = "tests\\ref";
static int dumps = 1;

static void dump_file(const char *kind, unsigned frame, const uint8_t *buf, size_t len)
{
    char path[600];
    FILE *f;
    snprintf(path, sizeof path, "%s/frame_%04u.%s", outdir, frame, kind);
    f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "REFRUN: cannot write %s\n", path); exit(4); }
    fwrite(buf, 1, len, f);
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* --mbtest: MBDIAG.MAC's own test table (battlezone-main/MBDIAG.MAC,     */
/* TESDAT) against mathbox.c - the same algorithm BZSTST's self test runs */
/* on real hardware, transcribed from the source (small, fixed test       */
/* vectors - not ROM image data, so not subject to gen_roms.py's "never   */
/* hand-transcribe" rule, which is about the ROM contents).               */
/* ------------------------------------------------------------------ */

typedef struct { uint8_t off, in; int chk_lo; uint8_t lo; int chk_hi; uint8_t hi; const char *note; } mbdiag_rec;

static const mbdiag_rec mbdiag_table[] = {
    { 0x00, 0x00, 1,0x00, 0,0x00, "AL=00" },
    { 0x01, 0x7F, 1,0x00, 1,0x7F, "AH=7F  A=7F00 (echo)" },
    { 0x02, 0x00, 1,0x00, 0,0x00, "BL=00" },
    { 0x03, 0x00, 1,0x00, 1,0x00, "BH=00  B=0000 (echo)" },
    { 0x04, 0x00, 1,0x00, 0,0x00, "EL=00" },
    { 0x05, 0x00, 1,0x00, 1,0x00, "EH=00  E=0000 (echo)" },
    { 0x06, 0x00, 1,0x00, 0,0x00, "FL=00" },
    { 0x07, 0x00, 1,0x00, 1,0x00, "FH=00  F=0000 (echo)" },
    { 0x08, 0x00, 1,0x00, 0,0x00, "XL=00" },
    { 0x09, 0x01, 1,0x00, 1,0x01, "XH=01  X=0100 (echo)" },
    { 0x0A, 0x80, 1,0x80, 0,0x00, "YL=80 (echo)" },
    { 0x0B, 0x00, 1,0x7F, 1,0x00, "YHSM Y=0080: (X-E)A-(Y-F)B >>16" },
    { 0x0C, 0x10, 1,0x10, 1,0x00, "N=10" },
    { 0x12, 0x00, 1,0x3F, 1,0x00, "SYM: (X-E)B+(Y-F)A >>16" },
    { 0x18, 0x00, 1,0x00, 1,0x80, "DYPL: Y' low16" },
    { 0x19, 0x00, 1,0x3F, 1,0x00, "DYPH: Y' high16" },
    { 0x17, 0x00, 1,0x7F, 1,0x00, "DXP:  X'" },
    { 0x13, 0x00, 1,0x00, 1,0x80, "SYXD: Y'/X' divide" },
    { 0x00, 0x00, 1,0x00, 0,0x00, "AL=00" },
    { 0x01, 0x5A, 1,0x00, 1,0x5A, "AH=5A  A=5A00" },
    { 0x02, 0x00, 1,0x00, 0,0x00, "BL=00" },
    { 0x03, 0xA6, 1,0x00, 1,0xA6, "BH=A6  B=A600" },
    { 0x04, 0xFF, 1,0xFF, 0,0x00, "EL=FF" },
    { 0x05, 0x7F, 1,0xFF, 1,0x7F, "EH=7F  E=7FFF" },
    { 0x06, 0xFF, 1,0xFF, 0,0x00, "FL=FF" },
    { 0x07, 0x7F, 1,0xFF, 1,0x7F, "FH=7F  F=7FFF" },
    { 0x08, 0x00, 1,0x00, 0,0x00, "XL=00" },
    { 0x09, 0x01, 1,0x00, 1,0x01, "XH=01  X=0100" },
    { 0x0A, 0x00, 1,0x00, 0,0x00, "YL=00" },
    { 0x0B, 0x01, 1,0xB4, 1,0xA6, "YHSM Y=0100: expect B4/A6" },
};

static int mbtest(void)
{
    int bad = 0;
    size_t n = sizeof mbdiag_table / sizeof mbdiag_table[0];
    mb_reset(&mb);
    printf("MBTEST  MBDIAG.MAC's TESDAT (battlezone-main/MBDIAG.MAC), %u records\n", (unsigned)n);
    for (size_t i = 0; i < n; i++) {
        const mbdiag_rec *r = &mbdiag_table[i];
        mb_write(&mb, r->off, r->in);
        uint8_t lo = mb_ylow(&mb), hi = mb_yhigh(&mb);
        int ok = (!r->chk_lo || lo == r->lo) && (!r->chk_hi || hi == r->hi);
        char expect[24];
        size_t o = 0;
        expect[0] = 0;
        if (r->chk_lo) o += (size_t)snprintf(expect + o, sizeof expect - o, " lo=$%02X", r->lo);
        if (r->chk_hi) o += (size_t)snprintf(expect + o, sizeof expect - o, " hi=$%02X", r->hi);
        printf("  [%2u] off=$%02X in=$%02X  got lo=$%02X hi=$%02X  expect%-13s  %-38s %s\n",
               (unsigned)i, r->off, r->in, lo, hi, expect, r->note, ok ? "ok" : "MISMATCH");
        if (!ok) bad++;
    }
    printf("MBTEST: %s (%d/%u records, steps %lu, runaway %lu)\n",
           bad ? "FAIL" : "PASS", (int)(n - (size_t)bad), (unsigned)n,
           (unsigned long)mb.steps, (unsigned long)mb.runaway);
    return bad ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

static void mark_pc(uint16_t pc) { pc_hit[pc >> 3] |= (uint8_t)(1u << (pc & 7)); }

/* --call-cycles FILE: for every JSR MAIN itself makes (SP at MAIN's level,
 * site in $5000-$53FF), the CPU cycles until it returns, NMIs included -
 * averaged per call site, attract [0] and game [1] apart.  The data behind
 * app_loop.c's pass-time model (hw_main_call). */
static const char *cc_file;
static int         cc_valid, cc_open;
static uint8_t     cc_main_s;
static uint16_t    cc_site;
static uint64_t    cc_start;
static unsigned long cc_n[2][0x400];
static uint64_t      cc_sum[2][0x400];
/* ... and where ROBOT falls: S_INTCT&15 at the pass start ($5004), and the
 * cycles (in NMI periods) and NMIs from there to the JSR ROBOT ($51B3). */
static uint64_t      cc_pass_cyc;
static uint8_t       cc_pass_int;
static int           cc_robot_seen;
static unsigned long cc_start_h[16], cc_off_h[32], cc_int_h[32];
/* ... and PNTPUT/DRAW (the per-object loop, $50B1/$50B9) by object byte. */
static uint8_t       cc_obj;
static unsigned long cc_obj_n[2][256];
static uint64_t      cc_obj_sum[2][256];

static void cc_write(void)
{
    FILE *f = fopen(cc_file, "w");
    if (!f) { perror(cc_file); return; }
    fprintf(f, "# site  attract_n attract_avg  game_n game_avg   (CPU cycles, NMIs included)\n");
    for (unsigned i = 0; i < 0x400; i++)
        if (cc_n[0][i] || cc_n[1][i])
            fprintf(f, "%04X %8lu %8.0f %8lu %8.0f\n", 0x5000 + i,
                    cc_n[0][i], cc_n[0][i] ? (double)cc_sum[0][i] / cc_n[0][i] : 0.0,
                    cc_n[1][i], cc_n[1][i] ? (double)cc_sum[1][i] / cc_n[1][i] : 0.0);
    fprintf(f, "# in game: S_INTCT&15 at pass start / NMI periods to JSR ROBOT / NMIs taken by then\n");
    for (unsigned i = 0; i < 32; i++)
        if ((i < 16 && cc_start_h[i]) || cc_off_h[i] || cc_int_h[i])
            fprintf(f, "# %2u: %6lu %6lu %6lu\n", i, i < 16 ? cc_start_h[i] : 0ul, cc_off_h[i], cc_int_h[i]);
    fprintf(f, "# object byte: PNTPUT n avg / DRAW n avg (all passes)\n");
    for (unsigned i = 0; i < 256; i++)
        if (cc_obj_n[0][i] || cc_obj_n[1][i])
            fprintf(f, "@ %02X %7lu %6.0f %7lu %6.0f\n", i,
                    cc_obj_n[0][i], cc_obj_n[0][i] ? (double)cc_obj_sum[0][i] / cc_obj_n[0][i] : 0.0,
                    cc_obj_n[1][i], cc_obj_n[1][i] ? (double)cc_obj_sum[1][i] / cc_obj_n[1][i] : 0.0);
    fclose(f);
}

int main(int argc, char **argv)
{
    unsigned frames_target = 600, capture_every = 32;
    uint16_t snap_pc = 0x5004;
    int do_mbtest = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames_target = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--outdir") && i + 1 < argc) outdir = argv[++i];
        else if (!strcmp(argv[i], "--capture-every") && i + 1 < argc) capture_every = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--snap-pc") && i + 1 < argc) snap_pc = (uint16_t)strtoul(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--no-dumps")) dumps = 0;
        else if (!strcmp(argv[i], "--call-cycles") && i + 1 < argc) cc_file = argv[++i];
        else if (!strcmp(argv[i], "--mbtest")) do_mbtest = 1;
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) script_path = argv[++i];
        else if (!strcmp(argv[i], "--watchdog-cycles") && i + 1 < argc) wd_cycles = strtoull(argv[++i], NULL, 0);
        else { fprintf(stderr, "usage: see the header of tests/refrun.c\n"); return 2; }
    }
    if (capture_every == 0) capture_every = 1;
    if (script_path && load_script(script_path)) return 2;

    mb_reset(&mb);
    if (do_mbtest) return mbtest();

    ad_pokey_init(&pokey, 1512000u, 44100u);  /* 1.512 MHz, HARDWARE_NOTES.md */
    ad_pokey_set_allpot(&pokey, in_allpot_sw);

    if (wd_cycles < 2u * NMI_CYCLES) { fprintf(stderr, "REFRUN: watchdog timeout below 2 NMI periods\n"); return 2; }

    FILE *sched = NULL, *idx = NULL;
    if (dumps) {
        char path[600];
        MKDIR(outdir);
        snprintf(path, sizeof path, "%s/frame_sched.txt", outdir);
        sched = fopen(path, "w");
        snprintf(path, sizeof path, "%s/ref_index.json", outdir);
        idx = fopen(path, "w");
        if (!sched || !idx) { fprintf(stderr, "REFRUN: cannot write into %s\n", outdir); return 4; }
        fprintf(sched, "# frame nmi_count cycle draw_cycles draw_ms  (nmi_count = NMIs serviced before\n"
                       "# this MAIN pass; draw_cycles/draw_ms = the LATEST GOADD's authentic state-PROM\n"
                       "# draw time as of this pass, avg_walk() (avg.h) - CPU cycles and milliseconds)\n");
        fprintf(idx, "{\n  \"rom\": \"battlezone (bzonea, $5000 CRC efbc3fa0)\",\n  \"cpu\": \"ref6502\",\n"
                     "  \"model\": {\"nmi_cycles\": %u, \"nmi_gate\": \"IN0 b4 set = self-test off\", "
                     "\"clock3k_half\": 256, \"vg_timing\": \"authentic state-PROM ticks, avg_walk() (avg.h) "
                     "- see tests/refrun.c\", \"in0_idle\": \"0x3F\", "
                     "\"option\": \"0x%02X\", \"opton2\": \"0x%02X\", \"allpot_idle\": \"0x00\", "
                     "\"random\": \"c012294 (ad_pokey), 1.512 MHz\", "
                     "\"mathbox\": \"microcode PROMs 036174..036180 (== Tempest's 136002-126..132)\", "
                     "\"snap_pc\": \"0x%04X\", \"script\": \"%s\", "
                     "\"watchdog_cycles\": %llu},\n  \"frames\": [\n",
                NMI_CYCLES, in_option, in_opton2, snap_pc,
                script_path ? script_path : "", (unsigned long long)wd_cycles);
    }

    cpu c;
    memset(&c, 0, sizeof c);
    c.read = mem_read;
    c.write = mem_write;
    apply_script(0);  /* PASS 0 lines: inputs held at power-on */
    cpu_reset(&c);

    uint64_t next_nmi = NMI_CYCLES;
    int nmi_pending = 0;
    unsigned frame = 0, captured = 0;
    unsigned long passes_nmi_prev = 0, max_nmi_per_pass = 0;
    uint64_t cycle_cap = (uint64_t)(frames_target + 200) * 20u * NMI_CYCLES;  /* generous */
    const char *stop = "frames reached";

    for (;;) {
        if (nmi_pending) {  /* real 6502 NMI: edge-triggered, unmaskable */
            cpu_nmi(&c);
            cyc += 7;
            nmi_pending = 0;
            n_nmi_taken++;
        }
        uint16_t pc = c.pc;
        if (pc == snap_pc) {
            frame++;
            int ev = apply_script(frame);
            unsigned long d = n_nmi_taken - passes_nmi_prev;
            if (frame > 1 && d > max_nmi_per_pass) max_nmi_per_pass = d;
            passes_nmi_prev = n_nmi_taken;
            if (sched) fprintf(sched, "%u %lu %llu %u %.4f\n", frame, n_nmi_taken, (unsigned long long)cyc,
                                last_draw_cycles, (double)last_draw_cycles / 1512000.0 * 1000.0);
            if (dumps && (frame <= 16 || frame % capture_every == 0)) {
                unsigned vec, bri, ops;
                int end = shape_walk(vg_start_addr, &vec, &bri, &ops);
                dump_file("ram", frame, ram, sizeof ram);
                dump_file("vram", frame, vram, sizeof vram);
                fprintf(idx, "%s    {\"frame\": %u, \"nmi_count\": %lu, \"cycle\": %llu, \"vggo\": %lu, "
                             "\"wdog\": %lu, \"avg_ops\": %u, \"vectors\": %u, \"bright\": %u, "
                             "\"list_end\": \"%s\", \"trap1\": %u, \"draw_cycles\": %u, \"draw_ms\": %.4f}",
                        captured ? ",\n" : "", frame, n_nmi_taken, (unsigned long long)cyc, n_vggo,
                        n_wdog, ops, vec, bri, avg_end_name(end), ram[0x370], last_draw_cycles,
                        (double)last_draw_cycles / 1512000.0 * 1000.0);
                captured++;
            }
            if (frame >= frames_target) break;
            if (ev == SC_END) { stop = "script end"; break; }
        }
        if (cc_file) {  /* --call-cycles: MAIN's top-level JSRs */
            if (pc == 0x5004) { cc_main_s = c.s; cc_valid = 1; cc_pass_cyc = cyc; cc_pass_int = ram[0x3A]; cc_robot_seen = 0; }
            if (pc == 0x51B3 && cc_valid && !cc_robot_seen && (ram[0xCE] & 0x80)) {  /* JSR ROBOT, in a game */
                cc_robot_seen = 1;
                cc_start_h[cc_pass_int & 15]++;
                unsigned nm = (unsigned)((cyc - cc_pass_cyc) / 6144u);
                cc_off_h[nm < 31 ? nm : 31]++;
                cc_int_h[(uint8_t)(ram[0x3A] - cc_pass_int) & 31]++;
            }
            if (cc_open && pc == (uint16_t)(cc_site + 3) && c.s == cc_main_s) {
                int k = ram[0xCE] & 0x80 ? 1 : 0;  /* ATRACT b7 set = a game */
                cc_n[k][cc_site - 0x5000]++;
                cc_sum[k][cc_site - 0x5000] += cyc - cc_start;
                if (cc_site == 0x50B1 || cc_site == 0x50B9) {
                    int j = cc_site == 0x50B9;
                    cc_obj_n[j][cc_obj]++;
                    cc_obj_sum[j][cc_obj] += cyc - cc_start;
                }
                cc_open = 0;
            }
            if (cc_valid && !cc_open && peek(pc) == 0x20 && c.s == cc_main_s && pc >= 0x5000 && pc < 0x5400) {
                cc_open = 1; cc_site = pc; cc_start = cyc;
                cc_obj = pc == 0x50B9 ? c.a : ram[0x270 + c.x];  /* DRAW's A / PNTPUT's PTBLO2,X */
            }
        }
        mark_pc(pc);
        uint8_t op = peek(pc);
        if (op == 0x00) n_brk++;
        cur_pc = pc;
        cpu_step(&c);
        cyc += base_cycles[op] + (is_branch(op) && c.pc != (uint16_t)(pc + 2) ? 1u : 0u);
        while (cyc >= next_nmi) {
            if (in_in0 & 0x10) {  /* self-test off: NMI enabled */
                if (nmi_pending) n_nmi_lost++;
                nmi_pending = 1;
                n_nmi_raised++;
            }
            next_nmi += NMI_CYCLES;
        }
        if (cyc > cycle_cap) { stop = "CYCLE CAP (stuck?)"; break; }
    }

    if (idx) { fprintf(idx, "\n  ]\n}\n"); fclose(idx); }
    if (sched) fclose(sched);
    if (cc_file) cc_write();
    if (dumps) {
        char path[600];
        snprintf(path, sizeof path, "%s/coverage_pcs.txt", outdir);
        FILE *cov = fopen(path, "w");
        if (cov) {
            for (unsigned a = 0x5000; a < 0x8000; a++)
                if (pc_hit[a >> 3] & (1u << (a & 7))) fprintf(cov, "%04X\n", a);
            fclose(cov);
        }
    }

    unsigned long distinct = 0;
    for (unsigned a = 0; a < 0x10000; a++) if (pc_hit[a >> 3] & (1u << (a & 7))) distinct++;
    unsigned vec, bri, ops;
    int end = shape_walk(vg_start_addr, &vec, &bri, &ops);
    unsigned long vram_nonzero = 0;
    for (size_t i = 0; i < sizeof vram; i++) vram_nonzero += vram[i] != 0;

    printf("REFRUN: stop = %s\n", stop);
    printf("  passes %u (target %u), cycles %llu (%.2f s), NMIs raised %lu taken %lu lost %lu, max NMIs/pass %lu\n",
           frame, frames_target, (unsigned long long)cyc, (double)cyc / 1512000.0,
           n_nmi_raised, n_nmi_taken, n_nmi_lost, max_nmi_per_pass);
    printf("  watchdog kicks %lu (max cycles between kicks %llu, timeout %llu), BRK executed %lu\n",
           n_wdog, (unsigned long long)max_kick_gap_cyc, (unsigned long long)wd_cycles, n_brk);
    printf("  GOADD (VG start) %lu, VSTOP %lu (list walks for HALT %lu), vector-RAM writes %lu (%lu non-zero bytes)\n",
           n_vggo, n_vgstop, n_vg_walks, n_vram_writes, vram_nonzero);
    printf("  display list from $%04X: %u AVG ops, %u vectors (%u beam-on), ends in %s\n",
           vg_start_addr, ops, vec, bri, avg_end_name(end));
    if (n_goadd_draws) {
        double avg_cyc = (double)sum_draw_cycles / (double)n_goadd_draws;
        printf("  AVG draw time (authentic state-PROM timing, avg_walk()/avg.h): %lu lists, "
               "avg %.0f cyc (%.3f ms), min %llu cyc (%.3f ms), max %llu cyc (%.3f ms)\n",
               n_goadd_draws, avg_cyc, avg_cyc / 1512000.0 * 1000.0,
               (unsigned long long)min_draw_cycles, (double)min_draw_cycles / 1512000.0 * 1000.0,
               (unsigned long long)max_draw_cycles, (double)max_draw_cycles / 1512000.0 * 1000.0);
        printf("  6-NMI VTIMER windows where VSTOP found the AVG still busy: %lu / %lu (%.1f%%)\n",
               n_vstop_while_busy, n_vgstop,
               n_vgstop ? 100.0 * (double)n_vstop_while_busy / (double)n_vgstop : 0.0);
    }
    printf("  RANDOM reads %lu, ALLPOT reads %lu, POKEY writes %lu, EMCNT writes %lu, SOUND writes %lu\n",
           n_random, n_allpot_reads, n_pokey_w, n_emcnt_w, n_sound_w);
    printf("  Mathbox starts %lu (%lu microsteps, %lu runaway, %lu xor-sign)\n",
           (unsigned long)mb.starts, (unsigned long)mb.steps, (unsigned long)mb.runaway, (unsigned long)mb.xor_sign);
    printf("  unmapped reads %lu (dummy-wait $6B79 excluded: %lu), writes %lu\n",
           n_unmapped_r, n_dummy_wait_r, n_unmapped_w);
    for (unsigned long i = 0; i < n_unmapped_r && i < 8; i++)
        printf("    unmapped read  $%04X from PC $%04X\n", first_unmapped_r[i], first_unmapped_r_pc[i]);
    for (unsigned long i = 0; i < n_unmapped_w && i < 8; i++)
        printf("    unmapped write $%04X from PC $%04X\n", first_unmapped_w[i], first_unmapped_w_pc[i]);
    printf("  RAM: ATRACT($%04X)=$%02X GOVER($%04X)=$%02X SYNC($%04X)=$%02X TRAP1($0370)=%u TRAP2($02ED)=$%02X%02X "
           "$$CRDT($000E)=$%02X SP $%02X PC $%04X\n",
           A_ATRACT, ram[A_ATRACT], A_GOVER, ram[A_GOVER], A_SYNC, ram[A_SYNC], ram[0x370],
           ram[0x2EE], ram[0x2ED], ram[0x00E], c.s, c.pc);
    printf("  distinct PCs %lu; captured %u frames into %s\n", distinct, captured, dumps ? outdir : "(no dumps)");

    int ok = frame >= frames_target && n_brk == 0 && n_vggo > 0 && n_vram_writes > 0 &&
             n_wdog > 0 && max_kick_gap_cyc <= wd_cycles &&
             (end == AVG_HALT || end == AVG_LOOP) && vec > 0;
    if (!strcmp(stop, "script end")) ok = n_brk == 0 && max_kick_gap_cyc <= wd_cycles;
    printf("REFRUN: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
