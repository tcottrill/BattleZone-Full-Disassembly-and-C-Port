/* lockstep.c - call-by-call verification of the translated Battlezone
 * modules against the real ROM running on tests/ref6502 (the shared core;
 * never a new one).  Adapted from the Tempest port's
 * C:\Source2026\Tempest Dissasembly\c_src\tests\lockstep.c: same idea (ENTRY
 * snapshot / DURING I/O log / COMPLETION replay / COMPARE), trimmed to what
 * M1 needs (no pass probe, no IRQ-placement-in-translated-code, no ROM
 * thunks - VGUT calls nothing outside itself) and extended with an explicit
 * NMI-taint counter per PLAN.md M1's brief ("Battlezone's NMI is heavy...
 * at minimum count [interrupted calls] as tainted and do not compare, and
 * report the count").
 *
 * How a routine check works
 *  - ENTRY.  When the ROM CPU executes a JSR or JMP (opcodes $20/$4C - a
 *    conditional branch landing on the same address does NOT count; see
 *    ../../disasm/CALLING_NOTES.md's tail-branch hazard: VGADD's own RTS
 *    returns to VGJSRL/VGVCTR's ORIGINAL caller, not to the internal BNE
 *    that lands on it) whose target is a registered routine's address, the
 *    whole machine (RAM, vector RAM) and the entry registers/flags are
 *    snapshotted.  Checks NEST: a routine called (by JSR/JMP) while another
 *    check is open gets its own check too (e.g. VGHALT's "JSR VGCNTR", or
 *    VGSTAT's/VGSCAL's tail "JMP VGADD2" into a check already open for
 *    VGSTAT/VGSCAL) - both are compared independently from their own
 *    entries.  The ROM then runs on untouched.
 *  - DURING.  Every hardware read/write the ROM makes while any check is
 *    open is logged (address, value, read/write) for later replay - VGUT
 *    itself never touches hw.h (it only touches RAM/vector RAM/vector ROM
 *    through ordinary memory accesses), so this log is empty for every M1
 *    check; it exists now, generic over any registered routine, so M2's
 *    modules (which DO use hw.h) can register without changing this file -
 *    see "Extending the routine table" below.
 *  - An NMI taken while a check is open TAINTS it: Battlezone's NMI is a
 *    real, unmaskable, edge-triggered 6502 NMI (not gated by the I flag),
 *    and its own handler is heavy (coins, sound, the AVG buffer swap - all
 *    of which write RAM) - PLAN.md's minimum bar for M1 is exactly this:
 *    a tainted call is counted, not compared, and the count is reported.
 *    (The fuller "IRQ moved" comparison Tempest's M4 adds - re-running with
 *    the interrupt's own effect applied at the call's entry or after its
 *    return - is not implemented here; every VGUT call is only a handful of
 *    instructions, so in practice almost none straddle an NMI - see
 *    NOTES_m1.md for the measured count.)
 *  - COMPLETION.  At the instruction boundary where the ROM's SP rises back
 *    above the entry SP (an RTS unwound the frame), the snapshot is loaded
 *    into the C port's `g`, the registered adapter runs the real C
 *    function with the entry registers, and the hardware log (empty for
 *    VGUT) is checked fully consumed.
 *  - COMPARE.  C RAM == ROM RAM and C vector RAM == ROM vector RAM, except
 *    the stack bytes the ROM call itself pushed ($0100+min_s+1..
 *    $0100+entry_s - PHP/PLP inside VGHEX1 is exactly this: transient, and
 *    excluded), the I/O log fully consumed, and the documented C flag
 *    (VGHEXZ/VGHEX) equal.
 *
 * Extending the routine table (for the other translation agents' modules)
 *  Registering a new routine is ONE line in routines[] plus ONE small
 *  adapter function immediately above it:
 *    static void run_foo(void) { ret_c = foo(R.a, R.x); }   / / adapter
 *    { "FOO", 0x1234, run_foo, 0,0,0,0 },                    // table row
 *  The adapter reads the entry registers from the global `R` (a/x/y/p) and
 *  calls the real translated function; if the routine documents an exit
 *  register/flag a caller consumes, set the matching ret_a/ret_x/ret_y/
 *  ret_c/ret_z/ret_n/ret_v global (else leave it at -1, "not checked") - a
 *  `regs`-returning routine (bzone_math.h's cos/sin/sin1/cosxtr/sinxtr) sets
 *  whichever of these its own `regs` fields are documented as meaningful,
 *  same as any other adapter.  Nothing else in this file changes: open_
 *  check()/finish_check()/run_compare() are generic over the table.
 *
 * Hardware seam (hw.h) - implemented HERE (M2, once a module that calls
 *  hw.h existed): every hw_* function replays against the CURRENT check's
 *  own slice of the shared I/O log (lk_io() below, called from mem_read_io()/
 *  mem_write() - the ROM-side recording, unchanged since M1) via replay_
 *  read()/replay_write(), which walk io_rp forward through iolog[k->io_start
 *  .. io_n-at-completion] and fail the check if the C function's hardware
 *  access doesn't match the ROM's, in order (address, read/write, and value
 *  except for WTCHDG/VSTOP - true "any value" strobes, is_strobe()).  This
 *  is the same "DURING is logged, COMPLETION replays it" design the header
 *  comment above already described as generic; VGUT simply never exercised
 *  it (empty log) because it never touches hw.h.
 *
 * NMI as its own checked unit (M2's "NMI fully native" bar; PLAN.md, and
 *  Tempest's IRQ handling this port's brief said to reuse) - a routine-table
 *  entry ("NMI", $558F) opened NOT by the generic JSR/JMP-address detection
 *  (the 6502 NMI line is a hardware edge, not an opcode) but explicitly, in
 *  main()'s loop, right after cpu_nmi(&c) delivers it (so entry_s already
 *  reflects the 3 bytes NMI's own push puts on the stack - the same "entry_s
 *  = SP right now" convention every other check uses).  Closing (RTI, $40,
 *  SP back at entry_s), nesting (a JSR to another registered routine, e.g.
 *  MOOLAH, while NMI's own check is open, per the generic mechanism above),
 *  and tainting (another NMI arriving while checks - including a still-open
 *  NMI check, true reentrancy - are open taints all of them, unchanged logic)
 *  all fall out of the EXISTING generic machinery with no special-casing:
 *  the only new code is the one open_check() call at the NMI site and
 *  excluding $558F from the JSR/JMP address table (the ROM never JSRs there
 *  anyway - SIGNATURES.md).  This is Tempest's M1/M2-era bar (IRQ checked
 *  natively, taint-counted) - not its later M3+ "IRQ placement"/"IRQ moved"
 *  machinery, out of scope until a module needs it (NOTES_m1.md, NOTES_
 *  integration.md "Prep: NMI-as-its-own-check design").
 *
 * ROM thunks (for callees in modules not yet handed over: robchk/explde/
 *  tr7chk, "game") - lk_rom_call() runs the ROM's OWN bytes at the callee's
 *  address on a second, throwaway cpu instance over the shared `g` (RAM/
 *  vector RAM via cpu_rd/cpu_wr, hardware via the SAME replay_read/
 *  replay_write every hw_* above uses - so a thunk's hardware accesses are
 *  verified against the log exactly like a real translated callee's would
 *  be), until a fake return address pops it to PC=$FFFF; every stack byte it
 *  touched is then restored (its own JSR/RTS bookkeeping is invisible to the
 *  caller's own RAM comparison).  Never registered in routines[] (there is
 *  no real C translation yet to compare against - the thunk trivially
 *  "passes" by construction, so counting it as checked/passed would be
 *  misleading; Tempest's thunk_addr[] is likewise disjoint from its own
 *  checked-routine table).  Deleted (thunk function + its bzone_game.h
 *  include) the moment "game" hands over and bzone_game.c is linked in.
 *
 * --list-untranslated: a coverage report cross-referencing every ROM
 *  routine in ../disasm/build/program_refs.json against this file's own
 *  routines[] table by address (not name - avoids any mangling ambiguity),
 *  printing checked/passed/failed/tainted for the registered ones and
 *  "NO" for the rest.  Diagnostic only; does not affect the run.
 *
 * Command line: tests\lockstep.exe [--frames N] [--script FILE]
 *               [--watchdog-cycles N (1000000)] [--list-untranslated]
 *               [--pass-probe]
 * Same script syntax as tests\refrun.c's header.  Exit 0 = 0 failed calls.
 */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>

#include "ref6502.h"
#include "mathbox.h"
#include "c012294.h"
#include "progrom.h"
#include "vecrom.h"
#include "avg.h"
#include "state.h"
#include "hw.h"
#include "vgut.h"
#include "bzsoun.h"
#include "coin65.h"
#include "bzone_nmi.h"
#include "bzone_math.h"
#include "bzone_game.h"
#include "bzone_disp.h"

/* ------------------------------------------------------------------ */
/* the ROM-side oracle machine: same hardware model as tests/refrun.c   */
/* (NMI, GOADD/VSTOP/AVG timing via avg.c, POKEY, Mathbox, DIP defaults, */
/* the $6B79 dummy-read classification) - see its header for the       */
/* evidence trail; not re-derived here.                                */
/* ------------------------------------------------------------------ */
#define NMI_CYCLES 6144u

static uint8_t  R_ram[0x0400];   /* the ROM's own RAM  ($0000-$03FF) */
static uint8_t  R_vram[0x1000];  /* the ROM's own vector RAM ($2000-$2FFF) */

static uint64_t cyc;
static uint64_t vg_busy_until;
static mathbox  mb;
static ad_pokey pokey;
static uint64_t pokey_cyc;

static unsigned long n_nmi_raised, n_nmi_taken, n_nmi_lost, n_nmi_during_check;
static unsigned long n_wdog, n_brk;
static uint64_t wd_cycles = 1000000u;
static uint64_t last_kick_cyc, max_kick_gap_cyc;

static uint8_t peek(uint16_t addr)
{
    uint16_t a = (uint16_t)(addr & 0x7FFF);
    if (a < 0x0400) return R_ram[a];
    if (a >= 0x2000 && a < 0x3000) return R_vram[a - 0x2000];
    if (a >= 0x3000 && a < 0x4000) return vecrom[a - 0x3000];
    if (a >= 0x5000 && a < 0x8000) return progrom[a - 0x5000];
    return 0;
}

static uint16_t cur_pc;

static int      vg_running;
static uint16_t vg_start_addr = 0x2002;
static int      vg_ends_in_halt;

static int vg_halted(void)
{
    if (!vg_running) return 1;
    if (!vg_ends_in_halt) return 0;
    return cyc >= vg_busy_until;
}

/* ---- scripted inputs (same syntax as tests/refrun.c) ---- */
static uint8_t in_in0 = 0x3F;
static uint8_t in_option = 0x15;
static uint8_t in_opton2 = 0x02;
static uint8_t in_allpot_sw = 0x00;

#define MAX_SCRIPT 4096
enum { SC_BYTE, SC_BIT_LOW, SC_BIT_HIGH, SC_END };
static struct { unsigned pass; int kind; uint8_t *port; uint8_t mask; uint8_t val; } script[MAX_SCRIPT];
static int script_n, script_pos;

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
    if (!f) { fprintf(stderr, "LOCKSTEP: cannot open script %s\n", path); return 1; }
    while (fgets(line, sizeof line, f)) {
        char *h = strchr(line, '#');
        if (h) *h = 0;
        int nf = sscanf(line, "%u %31s %i", &pass, port, &val);
        if (nf < 2) continue;
        int k = -1;
        for (size_t i = 0; i < sizeof script_ports / sizeof script_ports[0]; i++)
            if (!strcmp(port, script_ports[i].name)) { k = (int)i; break; }
        int is_event = k >= 0 && script_ports[k].kind == SC_END;
        if (k < 0 || script_n >= MAX_SCRIPT || (is_event ? nf != 2 : nf != 3) || pass < last_pass) {
            fprintf(stderr, "LOCKSTEP: bad script line: %s", line);
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
    if (vg_halted())     v |= 0x40;
    if ((cyc >> 8) & 1u) v |= 0x80;
    return v;
}

static void pokey_sync(void)
{
    if (cyc > pokey_cyc) { ad_pokey_advance(&pokey, (uint32_t)(cyc - pokey_cyc)); pokey_cyc = cyc; }
}

/* forward decl: the I/O log (defined in the "verification" section below) */
static void lk_io(uint32_t addr, uint8_t val, int write);

static unsigned long n_unmapped_r, n_unmapped_w, n_dummy_wait_r;

static uint8_t mem_read_io(uint16_t a)
{
    /* PLAN.md M1 item 4: the harmless dummy read at $6B79 (DRADAR's CMP
     * ($00,X), a deliberate Mathbox-wait "waste time" instruction -
     * CALLING_NOTES.md section "WAIT") is classified here, not counted as an
     * unmapped access or logged - it has no effect and no translated routine
     * will ever need to replay it.  Checked BEFORE the mapped-I/O decode: the
     * pointer comes from zero page at whatever X TRACK left, so it can land
     * on IN0/Mathbox/POKEY too; logging it there put one extra ROM-only read
     * ahead of LDA MOUTLO and desynced every such DRADAR check's replay
     * ("C read $1810, ROM read $0800 (I/O #13)", NOTES_integration.md
     * "Fixes made" #12).  None of those reads has a side effect. */
    if (cur_pc == 0x6B79) { n_dummy_wait_r++; return 0x00; }
    if (a >= 0x1820 && a < 0x1830) {
        pokey_sync();
        uint8_t v = ad_pokey_read(&pokey, (uint8_t)(a & 0x0F));
        lk_io(a, v, 0);
        return v;
    }
    switch (a) {
    case 0x0800: { uint8_t v = in0_byte(); lk_io(a, v, 0); return v; }
    case 0x0A00: lk_io(a, in_option, 0); return in_option;
    case 0x0C00: lk_io(a, in_opton2, 0); return in_opton2;
    case 0x1800: { uint8_t v = mb_status(&mb); lk_io(a, v, 0); return v; }
    case 0x1810: { uint8_t v = mb_ylow(&mb);   lk_io(a, v, 0); return v; }
    case 0x1818: { uint8_t v = mb_yhigh(&mb);  lk_io(a, v, 0); return v; }
    default: break;
    }
    n_unmapped_r++;
    return 0x00;
}

static uint8_t mem_read(uint16_t a)
{
    if (a < 0x0400 || (a >= 0x2000 && a < 0x4000) || a >= 0x5000) return peek(a);
    return mem_read_io(a);
}

static void mem_write(uint16_t a, uint8_t v)
{
    if (a < 0x0400) { R_ram[a] = v; return; }
    if (a >= 0x2000 && a < 0x3000) { R_vram[a - 0x2000] = v; return; }
    if (a >= 0x1820 && a < 0x1830) { pokey_sync(); ad_pokey_write(&pokey, (uint8_t)(a & 0x0F), v); lk_io(a, v, 1); return; }
    if (a >= 0x1860 && a < 0x1880) { mb_write(&mb, (uint8_t)(a - 0x1860), v); lk_io(a, v, 1); return; }
    switch (a) {
    case 0x1000: lk_io(a, v, 1); return;
    case 0x1200: {  /* GOADD */
        avg_mem am; avg_result ar;
        vg_running = 1;
        vg_start_addr = (uint16_t)(((uint16_t)v << 8) + 2);
        am.vram = R_vram; am.vrom = vecrom;
        ar = avg_walk(&am, vg_start_addr, NULL, NULL);
        vg_ends_in_halt = (ar.stop == AVG_STOP_HALT);
        /* ar.cycles is in AVG master-clock cycles (avg.h TIMING); convert to
         * CPU cycles - ceil(avg_cycles/8) - same conversion tests/refrun.c
         * applies (see its GOADD case for the found-by-M1 bug this mirrors:
         * without it, VG HALT reads busy ~8x too long). */
        vg_busy_until = cyc + (ar.cycles + AVG_CYC_PER_CPU - 1u) / AVG_CYC_PER_CPU;
        lk_io(a, v, 1);
        return;
    }
    case 0x1400:
        n_wdog++;
        if (cyc - last_kick_cyc > max_kick_gap_cyc) max_kick_gap_cyc = cyc - last_kick_cyc;
        last_kick_cyc = cyc;
        lk_io(a, v, 1);
        return;
    case 0x1600: vg_running = 0; lk_io(a, v, 1); return;  /* VSTOP */
    case 0x1840: lk_io(a, v, 1); return;                  /* SOUND */
    default: break;
    }
    n_unmapped_w++;
}

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
/* verification: the I/O log, routine table, checks, run_compare       */
/* ------------------------------------------------------------------ */
/* Generous: an NMI that interrupts an open check taints it (not compared)
 * but its own hardware I/O (POKEY, EMCNT, GOADD, WTCHDG, SOUND, Mathbox -
 * several events per NMI) is still logged while the check stays open, all
 * the way to the NMI's RTI, since the log does not itself know it will
 * never be replayed for a tainted check.  Over 5000 frames Battlezone's own
 * NMI rate (~246/frame pass; PLAN.md/NOTES_m0.md) gives at most a few
 * hundred thousand such events - nowhere near this bound. */
#define IOLOG_MAX (1u << 20)
typedef struct { uint32_t addr; uint8_t val, write; } io_ev;
static io_ev    iolog[IOLOG_MAX];
static unsigned io_n;
static unsigned long io_overflow;

/* entry registers / documented exit registers for the adapters (see the
 * "Extending the routine table" header comment) */
typedef struct { uint8_t a, x, y, p; } entry_regs;
static entry_regs R;
static int ret_c, ret_a, ret_x, ret_y;  /* -1 = not a documented output */
static int ret_z, ret_n, ret_v;          /* likewise, for a `regs`-returning
                                           * routine's z/n/v fields (regs.h) -
                                           * no current adapter sets these
                                           * (only .c is documented so far),
                                           * kept ready per the task brief's
                                           * "adapter-table shape for regs" */

static int      depth;
#define MAX_DEPTH 8
static unsigned long depth_overflow;

/* lk_io(): called from mem_read_io()/mem_write() above while at least one
 * check is open.  See the header comment - VGUT never calls this (it has
 * no hw.h calls), so this only exercises the "0 of the ROM's 0 I/O
 * operations" path for M1; a future module that DOES call hw.h needs no
 * change here. */
static void lk_io(uint32_t addr, uint8_t val, int write)
{
    if (depth == 0) return;
    if (io_n >= IOLOG_MAX) { io_overflow++; return; }
    iolog[io_n].addr = addr; iolog[io_n].val = val; iolog[io_n].write = (uint8_t)write;
    io_n++;
}

/* ------------------------------------------------------------------ */
/* I/O replay (COMPLETION) - see the header comment "Hardware seam".   */
/* io_rp/io_rp_end bound the CURRENTLY-replaying check's own slice of   */
/* iolog[]; set by run_compare() below just before calling r->run().   */
/* ------------------------------------------------------------------ */
static unsigned io_rp, io_rp_end;
static int      io_err;
static char     io_msg[160];

static int is_strobe(uint32_t a) { return a == 0x1400u || a == 0x1600u; }  /* WTCHDG, VSTOP: any value */
static const char *ev_verb(const io_ev *e) { return e->write ? "wrote" : "read"; }

/* Runaway guard: once a replay has desynced (io_err), a C loop that polls
 * hardware until some condition holds (e.g. WNSHLD_15's PRAND/OBJOBJ retry)
 * would otherwise spin forever on the 0s returned below.  After
 * RUNAWAY_IO more hw.h calls past the first error, abandon the C call -
 * run_compare() reports it as the FAIL it already is.  Found by the
 * WNSHLD mutation check (NOTES_integration.md "Fixes made" #15). */
#define RUNAWAY_IO 100000u
static jmp_buf  runaway_jb;
static int      runaway_armed;
static unsigned runaway_n;
static void runaway_tick(void)
{
    if (io_err && runaway_armed && ++runaway_n > RUNAWAY_IO) longjmp(runaway_jb, 1);
}

static uint8_t replay_read(uint32_t a)
{
    runaway_tick();
    if (io_rp >= io_rp_end || iolog[io_rp].write || iolog[io_rp].addr != a) {
        if (!io_err) {
            io_err = 1;
            if (io_rp >= io_rp_end) snprintf(io_msg, sizeof io_msg, "C read $%04X but the ROM did no more I/O", a);
            else snprintf(io_msg, sizeof io_msg, "C read $%04X, ROM %s $%04X (I/O #%u)", a,
                          ev_verb(&iolog[io_rp]), iolog[io_rp].addr, io_rp);
        }
        return 0;
    }
    return iolog[io_rp++].val;
}

static void replay_write(uint32_t a, uint8_t v)
{
    runaway_tick();
    if (io_rp >= io_rp_end || !iolog[io_rp].write || iolog[io_rp].addr != a ||
        (!is_strobe(a) && iolog[io_rp].val != v)) {
        if (!io_err) {
            io_err = 1;
            if (io_rp >= io_rp_end) snprintf(io_msg, sizeof io_msg, "C wrote $%02X to $%04X but the ROM did no more I/O", v, a);
            else snprintf(io_msg, sizeof io_msg, "C wrote $%02X to $%04X, ROM %s $%02X at $%04X (I/O #%u)", v, a,
                          ev_verb(&iolog[io_rp]), iolog[io_rp].val, iolog[io_rp].addr, io_rp);
        }
        return;
    }
    io_rp++;
}

/* ---- hw.h: the hardware seam's verification-time implementation (its own
 * header comment names tests/lockstep.c as one of the two homes) - every
 * call replays against the check currently being completed. */
uint8_t hw_in0(void)    { return replay_read(0x0800); }
uint8_t hw_option(void) { return replay_read(0x0A00); }
uint8_t hw_opton2(void) { return replay_read(0x0C00); }
uint8_t hw_pokey_read(uint8_t reg)               { return replay_read(0x1820u + (reg & 0x0Fu)); }
void    hw_pokey_write(uint8_t reg, uint8_t val) { replay_write(0x1820u + (reg & 0x0Fu), val); }
uint8_t hw_mb_status(void) { return replay_read(0x1800); }
uint8_t hw_mb_ylow(void)   { return replay_read(0x1810); }
uint8_t hw_mb_yhigh(void)  { return replay_read(0x1818); }
void    hw_mb_write(uint8_t offset, uint8_t val) { replay_write(0x1860u + (offset & 0x1Fu), val); }
void    hw_emcnt(uint8_t v)    { replay_write(0x1000, v); }
void    hw_goadd(uint8_t page) { replay_write(0x1200, page); }
void    hw_wtchdg(void)        { replay_write(0x1400, 0); }
void    hw_vstop(void)         { replay_write(0x1600, 0); }
void    hw_sound(uint8_t v)    { replay_write(0x1840, v); }

/* ---- hw_wait_frame(): see hw.h and the "Pass-level probe" section below.
 *
 * Integration bug found and fixed while building the pass probe (2026-09-
 * 18, NOTES_integration.md): MAIN's real "LSR SYNC / BCC MAIN" spin only
 * terminates because NMI keeps incrementing SYNC WHILE the 6502 is spinning
 * (real, interleaved, real-time hardware) - a single, uninterrupted C call
 * cannot reproduce that race at all (an early attempt that shifted the
 * snapshotted SYNC byte hung forever when that byte was exactly 0; a second
 * attempt that copied R_ram[A_SYNC] directly was wrong for a different
 * reason - by the time the probe actually runs, R_ram has moved on to a
 * LATER pass, so it reads the wrong byte).  SYNC has no other observable
 * effect anywhere the pass probe checks (bzone_game.c's own header comment:
 * "nothing else in this module ever reads SYNC") - so this function is a
 * pure no-op except for the pass-probe's own bookkeeping, and SYNC's
 * address is excluded from the pass probe's RAM comparison (run_
 * pass_boundary(), same idea as a routine check's stack-byte exclusion). */
static int     pass_active;
static int     pass_hits;
static jmp_buf pass_jmp;

void hw_wait_frame(void)
{
    if (pass_active && ++pass_hits >= 2) longjmp(pass_jmp, 1);
}

/* hw_wait_bufrdy(): MAIN_5's BUFRDY spin (hw.h) - no RAM effect of its own,
 * so nothing to do here. */
void hw_wait_bufrdy(void) {}

/* hw_cycles() (hw.h CYC): sums the C call's own cycle charges; run_compare()
 * compares the sum with the ROM's cycles for the same call. */
static uint64_t c_cyc;
static unsigned long cyc_failures_printed;
static int cycles_strict;  /* --cycles-strict: a cycle mismatch fails the run */
void hw_cycles(unsigned n) { c_cyc += n; }

/* ------------------------------------------------------------------ */
/* ROM thunks - see the header comment "ROM thunks".                   */
/* ------------------------------------------------------------------ */
static uint8_t thunk_s;   /* SP the currently-replaying check had at its own
                            * entry (run_compare() sets this = k->entry_s
                            * before calling r->run()) - every thunk called
                            * from within that call uses it as its own base. */

/* Integration bug found while folding in the maths module's ROTATE (which
 * calls the tr7chk() thunk below): the first cut of this range check only
 * excluded RAM/vector-RAM/vector-ROM from the hardware range, forgetting
 * program ROM ($5000-$7FFF) - so a thunk's OWN OPCODE FETCHES inside that
 * range (tc.read is also used for instruction fetch, not just data reads)
 * were routed into replay_read() as if they were hardware accesses,
 * consuming real logged I/O events and desyncing everything after (visible
 * as "ROM thunk $69B5 did not return" and a bogus "C read $69B5" in the
 * failure report - the thunk was trying to fetch its own first opcode
 * through the I/O replay).  Fixed by matching the ROM-side oracle's own
 * mem_read_io()/mem_write() address list exactly, not a broad range. */
static int is_hw_addr(uint16_t a)
{
    if (a == 0x0800u || a == 0x0A00u || a == 0x0C00u) return 1;  /* IN0/OPTION/OPTON2 */
    if (a == 0x1000u || a == 0x1200u || a == 0x1400u || a == 0x1600u || a == 0x1840u) return 1;  /* EMCNT/GOADD/WTCHDG/VSTOP/SOUND */
    if (a == 0x1800u || a == 0x1810u || a == 0x1818u) return 1;  /* Mathbox status/result */
    if (a >= 0x1820u && a < 0x1830u) return 1;                   /* POKEY */
    if (a >= 0x1860u && a < 0x1880u) return 1;                   /* Mathbox write */
    return 0;
}

static uint8_t thunk_read(uint16_t a)  { return is_hw_addr(a) ? replay_read(a) : cpu_rd(a); }
static void    thunk_write(uint16_t a, uint8_t v) { if (is_hw_addr(a)) replay_write(a, v); else cpu_wr(a, v); }

typedef struct { uint8_t a, x, y, c; } thunk_regs;

/* Runs the ROM's own bytes at `addr` over the shared `g`, entry A/X/Y (and
 * g.dflag) in; returns exit A/X/Y/carry, g.dflag updated. */
static thunk_regs lk_rom_call(uint16_t addr, uint8_t a, uint8_t x, uint8_t y)
{
    cpu tc;
    uint8_t stack[0x100], low;
    unsigned long n;
    thunk_regs r;

    memcpy(stack, g.ram + 0x100, sizeof stack);
    memset(&tc, 0, sizeof tc);
    tc.read = thunk_read;
    tc.write = thunk_write;
    tc.p = (uint8_t)(0x24 | (g.dflag ? CPU_D : 0));
    tc.a = a; tc.x = x; tc.y = y;
    tc.s = thunk_s;
    g.ram[0x100 | tc.s] = 0xFF; tc.s--;      /* fake return address $FFFE:
                                               * an RTS lands on PC=$FFFF */
    g.ram[0x100 | tc.s] = 0xFE; tc.s--;
    low = tc.s;
    tc.pc = addr;
    for (n = 0; tc.pc != 0xFFFF; n++) {
        if (n > 5000000ul) {
            fprintf(stderr, "LOCKSTEP: ROM thunk $%04X did not return\n", addr);
            break;
        }
        cpu_step(&tc);
        if (tc.s < low) low = tc.s;
    }
    for (unsigned s = (unsigned)low + 1u; s <= thunk_s; s++)
        g.ram[0x100 + s] = stack[s];  /* undo the thunk's own stack use */
    g.dflag = (uint8_t)((tc.p & CPU_D) != 0);
    r.a = tc.a; r.x = tc.x; r.y = tc.y; r.c = (uint8_t)(tc.p & CPU_C);
    return r;
}

/* ---- thunk stand-ins: NONE currently needed. "game" (MAIN/EXPLDE/etc),
 * "robot" (bzone_robot.c: ROBOT family, FIREIT) and "enemy" (bzone_enemy.c:
 * BUZBOM, SAUCMV, OBJOBJ, ROBCHK, TANKCK, R2D3CK, static ROB1) have all
 * handed over 2026-09-18 - every routine of BZONE.MAC/BZSOUN/COIN65/VGUT
 * is real and linked now (see build_all.bat's MODULES). Only BZSTST
 * remains (planned last, PLAN.md port order #7) - this block is the slot
 * for its callees if any are needed before it hands over; empty for now. */

/* ------------------------------------------------------------------ */
/* Pass-level probe (Gates 1/N's basis, coordinator's brief) - runs      */
/* bz_main() over one whole pass from a snapshot and compares it to the  */
/* ROM's own real trace of that SAME pass, the way a routine check       */
/* compares one call - see the long comment below for why bz_main()/     */
/* b_end() (which recurse into each other forever, "JMP MAIN" mapped to  */
/* a real C call - NOTES_game.md's own "flagged for integration" caveat) */
/* are NOT registered as an ordinary routine check, and why this         */
/* separate mechanism is safe where that would not be.                   */
/*                                                                        */
/* bz_main()/b_end() are never registered in routines[] and $5000/$5159  */
/* are excluded from addr_routine[] (see main()'s setup) - so the ROM's   */
/* OWN JMP MAIN / JMP B_END never opens a nested check for them. A        */
/* multi-thousand-frame lockstep run calling the REAL bz_main() through   */
/* the ordinary JSR/JMP-address mechanism would recurse once per frame     */
/* forever (C gives no TCO guarantee) - unbounded stack growth over 5000   */
/* frames.  The pass probe instead calls bz_main() itself, but bounds the   */
/* call to exactly ONE pass using hw_wait_frame()'s own instrumentation:     */
/* bz_main()'s FIRST statement is hw_wait_frame() (pass N's own wait,        */
/* already "done" - let it return normally) - the SECOND call happens only   */
/* after this pass's full body runs and b_end() tail-recurses into the       */
/* NEXT bz_main(), i.e. exactly at the boundary Tempest's own pass probe      */
/* uses ("from each pass start to the next").  longjmp() unwinds back to      */
/* run_pass_probe() right there, discarding the (small, bounded-by-one-pass)   */
/* C call depth this built up - no accumulation across probe calls, since       */
/* each one starts fresh from setjmp(). */
static uint8_t  pass_snap_ram[0x400], pass_snap_vram[0x1000];
static unsigned pass_io_start;
static int      pass_have_snap;
static unsigned long pass_checked, pass_passed, pass_failed;
static unsigned long pass_failures_printed;

/* Off by default - see NOTES_integration.md "Pass-level probe: known-
 * broken".  Calling the REAL bz_main() once per pass, on top of the ROM's
 * own execution and every ordinary routine check that pass also opens,
 * roughly doubles (or worse, once nested checks are counted) the work
 * per frame - fine for the occasional targeted run while debugging the
 * probe itself, but it turns an ordinary `--frames 5000` regression run
 * (seconds) into a many-minutes one, which is not acceptable for the
 * routine-level checks that ARE trustworthy and ARE this milestone's
 * actual deliverable.  Pass `--pass-probe` to opt in. */
static int pass_probe_enabled;

/* Called once per real "MAIN pass boundary" visit - PC == $5004 (the JSR
 * VOLCNO right after the SYNC spin's BCC falls through, NOT $5000: $5000
 * itself is the LSR/BCC spin's OWN address, revisited many times per real
 * pass while the ROM waits - $5004 is reached exactly once per pass, the
 * SAME point tests/refrun.c and this file's own frame counter already use
 * - see main()'s loop). If a previous visit's snapshot exists, this pass
 * just finished on the ROM side (from pass_io_start to the current io_n) -
 * run the probe for it, then take the new snapshot for the pass that is
 * about to start. */
static void pass_boundary(const cpu *c)
{
    if (!pass_probe_enabled) return;
    if (pass_have_snap) {
        pass_checked++;
        memcpy(g.ram, pass_snap_ram, sizeof g.ram);
        memcpy(g.vram, pass_snap_vram, sizeof g.vram);
        g.stray_writes = 0;
        io_rp = pass_io_start; io_rp_end = io_n;
        io_err = 0; io_msg[0] = 0;
        thunk_s = c->s;  /* any thunk this pass reaches bases its stack here */
        pass_active = 1; pass_hits = 0;
        /* main_pass(), not bz_main(): since the pass_signal trampoline,
         * bz_main() runs MAIN's own segment only (stopping at its first
         * JMP B_END/WNSHLD/...); main_pass() follows the chain to the
         * pass's own "JMP MAIN".  The longjmp stays as a backstop. */
        if (setjmp(pass_jmp) == 0) main_pass();
        pass_active = 0;

        if (!io_err && io_rp != io_rp_end) {
            io_err = 1;
            snprintf(io_msg, sizeof io_msg, "the pass's %u I/O event(s) were not replayed", io_rp_end - io_rp);
        }
        {
            unsigned nbad = 0, a;
            char diffs[400]; size_t dl = 0;
            diffs[0] = 0;
            for (a = 0; a < 0x400; a++) {
                if (a == A_SYNC) continue;  /* hw_wait_frame() no-op - see its own comment */
                if (g.ram[a] != R_ram[a]) {
                    if (nbad < 6) dl += (size_t)snprintf(diffs + dl, sizeof diffs - dl, " $%04X C=%02X ROM=%02X", a, g.ram[a], R_ram[a]);
                    nbad++;
                }
            }
            for (a = 0; a < 0x1000; a++)
                if (g.vram[a] != R_vram[a]) {
                    if (nbad < 6) dl += (size_t)snprintf(diffs + dl, sizeof diffs - dl, " $%04X C=%02X ROM=%02X", a + 0x2000, g.vram[a], R_vram[a]);
                    nbad++;
                }
            if (nbad || io_err || g.stray_writes) {
                pass_failed++;
                if (pass_failures_printed++ < 20)
                    printf("PASS PROBE FAIL #%lu: %u byte(s)%s%s%s\n", pass_checked, nbad, diffs,
                           io_err ? "; I/O: " : "", io_err ? io_msg : "");
            } else {
                pass_passed++;
            }
        }
    }

    memcpy(pass_snap_ram, R_ram, sizeof pass_snap_ram);
    memcpy(pass_snap_vram, R_vram, sizeof pass_snap_vram);
    if (depth == 0) io_n = 0;  /* the probe's own recycle point (see the depth-0 recycle in main()) */
    pass_io_start = io_n;
    pass_have_snap = 1;
}

typedef struct {
    const char *name; uint16_t addr; void (*run)(void);
    unsigned long checked, passed, failed, tainted;
    unsigned long cyc_ok, cyc_bad;  /* CYC() total vs the ROM's cycles, untainted calls */
} routine;

/* ---- adapters: one small function per registered routine, reading the
 * entry registers from R and calling the real translated function (see
 * "Extending the routine table" above for how a new module registers) ---- */
static void run_vgrtsl(void) { vgrtsl(); }
static void run_vghalt(void) { vghalt(); }
static void run_vghexz(void) { ret_c = vghexz(R.a, R.p & 1); }
static void run_vghex(void)  { ret_c = vghex(R.a); }
static void run_vgjmpl(void) { vgjmpl(R.a, R.x); }
static void run_vgjsrl(void) { vgjsrl(R.a, R.x); }
static void run_vgsta1(void) { vgsta1(R.a); }
static void run_vgstat(void) { vgstat(R.a, R.y); }
static void run_vgcntr(void) { vgcntr(); }
static void run_vgadd2(void) { vgadd2(R.a, R.x); }
static void run_vgadd(void)  { vgadd(R.y); }
static void run_vgsca1(void) { vgsca1(R.a); }
static void run_vgscal(void) { vgscal(R.a, R.y); }
static void run_vgdot(void)  { vgdot(R.a); }
static void run_vgvtr(void)  { vgvtr(R.a, R.x, R.y); }
static void run_vgvtr1(void) { vgvtr1(R.a, R.x); }
static void run_vgvtr2(void) { vgvtr2(); }
static void run_vgvctr(void) { vgvctr(R.x); }

/* ---- nmi-sound-coin (bzsoun.c/coin65.c/bzone_nmi.c) --------------------- */
static void run_sndon(void)  { sndon(R.a, R.x); }
static void run_modsnd(void) { modsnd(); }
static void run_moolah(void) { moolah(); }
static void run_nmi(void)    { nmi(); }

/* ---- maths (bzone_math.c) ----------------------------------------------- */
static void run_wait(void)    { ret_a = wait(); }
static void run_rotate(void)  { rotate(); }
static void run_rotpnt(void)  { ret_y = rotpnt(R.y); }
static void run_pntput(void)  { pntput(R.x); }
static void run_kludge(void)  { kludge(); }
static void run_bound(void)   { bound(); }
static void run_cos(void)     { regs r = cos_(R.a);   ret_a = r.a; ret_x = r.x; ret_c = r.c; }
static void run_sin(void)     { regs r = sin_(R.a);   ret_a = r.a; ret_x = r.x; ret_c = r.c; }
static void run_sin1(void)    { regs r = sin1(R.a);   ret_a = r.a; ret_x = r.x; ret_c = r.c; }
static void run_cosxtr(void)  { regs r = cosxtr(R.a, R.x); ret_a = r.a; ret_x = r.x; ret_c = r.c; }
static void run_sinxtr(void)  { regs r = sinxtr(R.a, R.x); ret_a = r.a; ret_x = r.x; ret_c = r.c; }
static void run_abstmp(void)  { abstmp(); }
static void run_track(void)   { ret_a = track(); }
static void run_t_0(void)     { ret_a = t_0(R.a); }
static void run_t_1(void)     { ret_a = t_1(R.a); }
static void run_t_2(void)     { ret_a = t_2(R.a); }
static void run_t_3(void)     { ret_a = t_3(R.a); }

/* ---- game (bzone_game.c, first half - MAIN/EXPLDE/QWIKCK.../TR7CHK) ----
 * bz_main/b_end deliberately NOT here - see "Pass-level probe" above. */
static void run_explde(void)  { ret_y = explde(R.y); }
static void run_qwikck(void)  { ret_c = qwikck(); }
static void run_shrtck(void)  { ret_c = shrtck(R.x, R.y); }
static void run_colchk(void)  { colchk(); }
static void run_sauchk(void)  { sauchk(R.x, R.y); }
static void run_newlif(void)  { newlif(); }
static void run_exinit(void)  { exinit(R.x); }
static void run_motion(void)  { motion(); }
static void run_m_set(void)   { m_set(R.x); }
static void run_m_for(void)   { m_for(R.x); }
static void run_m_for1(void)  { m_for1(R.x); }
static void run_m_rev(void)   { m_rev(R.x); }
static void run_itangl(void)  { itangl(R.x); }
static void run_dtangl(void)  { dtangl(R.x); }
static void run_saveit(void)  { saveit(R.x); }
static void run_bakout(void)  { bakout(R.x); }
static void run_react(void)   { react(R.a); }
static void run_tr7chk(void)  { ret_c = tr7chk(); }

/* ---- display (bzone_disp.c) - all void, no documented register output */
static void run_bigwnd(void)  { bigwnd(); }
static void run_wnshld(void)  { wnshld(); }
static void run_battle(void)  { battle(); }
static void run_batint(void)  { batint(); }
static void run_ckscor(void)  { ckscor(); }
static void run_hiscre(void)  { hiscre(); }
static void run_letchk(void)  { letchk(); }
static void run_distbl(void)  { distbl(); }
static void run_dspalp(void)  { dspalp(R.a); }
static void run_finish(void)  { finish(); }
static void run_mounts(void)  { mounts(); }
static void run_volcno(void)  { volcno(); }
static void run_draw(void)    { draw(R.a); }
static void run_dovect(void)  { dovect(R.a); }
static void run_d_next(void)  { d_next(); }
static void run_dradar(void)  { dradar(); }
static void run_shupdt(void)  { shupdt(); }
static void run_msgs(void)    { msgs(R.x); }
static void run_info(void)    { info(); }
static void run_digt2s(void)  { digt2s(R.a); }
static void run_digtys(void)  { digtys(R.a, R.y); }
static void run_digits(void)  { digits(R.a, R.y, R.p & CPU_C); }

/* ---- robot (bzone_robot.c) ---- */
static void run_robot(void)   { robot(); }
static void run_r_37s_(void)  { r_37s_(); }
static void run_r_40s_(void)  { r_40s_(); }
static void run_r_50s_(void)  { r_50s_(); }
static void run_r_evad(void)  { r_evad(); }
static void run_r_rand(void)  { r_rand(); }
static void run_r_atck(void)  { r_atck(); }
static void run_fireit(void)  { fireit(); }

/* ---- enemy (bzone_enemy.c) ---- */
static void run_buzbom(void)  { buzbom(); }
static void run_saucmv(void)  { saucmv(); }
static void run_objobj(void)  { regs r = objobj(R.x); ret_a = r.a; ret_c = r.c; }
static void run_robchk(void)  { robchk(); }
static void run_tankck(void)  { tankck(); }
static void run_r2d3ck(void)  { r2d3ck(); }

static routine routines[] = {
    { "VGRTSL", 0x7A19, run_vgrtsl, 0,0,0,0 },
    { "VGHALT", 0x7A1D, run_vghalt, 0,0,0,0 },
    { "VGHEXZ", 0x7A29, run_vghexz, 0,0,0,0 },
    { "VGHEX",  0x7A2F, run_vghex,  0,0,0,0 },
    { "VGJMPL", 0x7A49, run_vgjmpl, 0,0,0,0 },
    { "VGJSRL", 0x7A5A, run_vgjsrl, 0,0,0,0 },
    { "VGSTA1", 0x7A61, run_vgsta1, 0,0,0,0 },
    { "VGSTAT", 0x7A63, run_vgstat, 0,0,0,0 },
    { "VGCNTR", 0x7A6A, run_vgcntr, 0,0,0,0 },
    { "VGADD2", 0x7A6E, run_vgadd2, 0,0,0,0 },
    { "VGADD",  0x7A76, run_vgadd,  0,0,0,0 },
    { "VGSCA1", 0x7A81, run_vgsca1, 0,0,0,0 },
    { "VGSCAL", 0x7A83, run_vgscal, 0,0,0,0 },
    { "VGDOT",  0x7A8A, run_vgdot,  0,0,0,0 },
    { "VGVTR",  0x7A8E, run_vgvtr,  0,0,0,0 },
    { "VGVTR1", 0x7A90, run_vgvtr1, 0,0,0,0 },
    { "VGVTR2", 0x7AAB, run_vgvtr2, 0,0,0,0 },
    { "VGVCTR", 0x7AAD, run_vgvctr, 0,0,0,0 },
    /* nmi-sound-coin: NMI is opened explicitly in main()'s loop (see the
     * header comment), never via the JSR/JMP address table below - its
     * address is still listed here so the per-routine report/--list-
     * untranslated can find it by address like any other row. */
    { "SNDON",  0x7985, run_sndon,  0,0,0,0 },
    { "MODSND", 0x79B9, run_modsnd, 0,0,0,0 },
    { "MOOLAH", 0x56C3, run_moolah, 0,0,0,0 },
    { "NMI",    0x558F, run_nmi,    0,0,0,0 },
    /* maths */
    { "WAIT",   0x5B80, run_wait,   0,0,0,0 },
    { "ROTATE", 0x5946, run_rotate, 0,0,0,0 },
    { "ROTPNT", 0x5B1F, run_rotpnt, 0,0,0,0 },
    { "PNTPUT", 0x5D4A, run_pntput, 0,0,0,0 },
    { "KLUDGE", 0x5E05, run_kludge, 0,0,0,0 },
    { "BOUND",  0x5E44, run_bound,  0,0,0,0 },
    { "COS",    0x5E4B, run_cos,    0,0,0,0 },
    { "SIN",    0x5E4E, run_sin,    0,0,0,0 },
    { "SIN1",   0x5E63, run_sin1,   0,0,0,0 },
    { "COSXTR", 0x5EF9, run_cosxtr, 0,0,0,0 },
    { "SINXTR", 0x5F17, run_sinxtr, 0,0,0,0 },
    { "ABSTMP", 0x6149, run_abstmp, 0,0,0,0 },
    { "TRACK",  0x6810, run_track,  0,0,0,0 },
    { "T_0",    0x68D5, run_t_0,    0,0,0,0 },
    { "T_1",    0x68C6, run_t_1,    0,0,0,0 },
    { "T_2",    0x68CB, run_t_2,    0,0,0,0 },
    { "T_3",    0x68D0, run_t_3,    0,0,0,0 },
    /* game (first half) */
    { "EXPLDE", 0x5B89, run_explde, 0,0,0,0 },
    { "QWIKCK", 0x5F4E, run_qwikck, 0,0,0,0 },
    { "SHRTCK", 0x5F5B, run_shrtck, 0,0,0,0 },
    { "COLCHK", 0x6045, run_colchk, 0,0,0,0 },
    { "SAUCHK", 0x60C2, run_sauchk, 0,0,0,0 },
    { "NEWLIF", 0x615F, run_newlif, 0,0,0,0 },
    { "EXINIT", 0x6199, run_exinit, 0,0,0,0 },
    { "MOTION", 0x61D0, run_motion, 0,0,0,0 },
    { "M_SET",  0x6310, run_m_set,  0,0,0,0 },
    { "M_FOR",  0x6351, run_m_for,  0,0,0,0 },
    { "M_FOR1", 0x6354, run_m_for1, 0,0,0,0 },
    { "M_REV",  0x636F, run_m_rev,  0,0,0,0 },
    { "ITANGL", 0x638D, run_itangl, 0,0,0,0 },
    { "DTANGL", 0x639B, run_dtangl, 0,0,0,0 },
    { "SAVEIT", 0x63A9, run_saveit, 0,0,0,0 },
    { "BAKOUT", 0x63BA, run_bakout, 0,0,0,0 },
    { "REACT",  0x63CB, run_react,  0,0,0,0 },
    { "TR7CHK", 0x69B5, run_tr7chk, 0,0,0,0 },
    /* display */
    { "BIGWND", 0x51D9, run_bigwnd, 0,0,0,0 },
    { "WNSHLD", 0x51E9, run_wnshld, 0,0,0,0 },
    { "BATTLE", 0x5268, run_battle, 0,0,0,0 },
    { "BATINT", 0x531F, run_batint, 0,0,0,0 },
    { "CKSCOR", 0x5344, run_ckscor, 0,0,0,0 },
    { "HISCRE", 0x53C5, run_hiscre, 0,0,0,0 },
    { "LETCHK", 0x5465, run_letchk, 0,0,0,0 },
    { "DISTBL", 0x549B, run_distbl, 0,0,0,0 },
    { "DSPALP", 0x556A, run_dspalp, 0,0,0,0 },
    { "FINISH", 0x5574, run_finish, 0,0,0,0 },
    { "MOUNTS", 0x57C8, run_mounts, 0,0,0,0 },
    { "VOLCNO", 0x58BA, run_volcno, 0,0,0,0 },
    { "DRAW",   0x5C5C, run_draw,   0,0,0,0 },
    { "DOVECT", 0x5D11, run_dovect, 0,0,0,0 },
    { "D_NEXT", 0x5D41, run_d_next, 0,0,0,0 },
    { "DRADAR", 0x6AE9, run_dradar, 0,0,0,0 },
    { "SHUPDT", 0x6C66, run_shupdt, 0,0,0,0 },
    { "MSGS",   0x6C98, run_msgs,   0,0,0,0 },
    { "INFO",   0x6D2E, run_info,   0,0,0,0 },
    { "DIGT2S", 0x7B9E, run_digt2s, 0,0,0,0 },
    { "DIGTYS", 0x7BA0, run_digtys, 0,0,0,0 },
    { "DIGITS", 0x7BA1, run_digits, 0,0,0,0 },
    /* robot */
    { "ROBOT",  0x6418, run_robot,  0,0,0,0 },
    { "R_37S_", 0x64EE, run_r_37s_, 0,0,0,0 },
    { "R_40S_", 0x6534, run_r_40s_, 0,0,0,0 },
    { "R_50S_", 0x6538, run_r_50s_, 0,0,0,0 },
    { "R_EVAD", 0x6553, run_r_evad, 0,0,0,0 },
    { "R_RAND", 0x6571, run_r_rand, 0,0,0,0 },
    { "R_ATCK", 0x6589, run_r_atck, 0,0,0,0 },
    { "FIREIT", 0x6595, run_fireit, 0,0,0,0 },
    /* enemy */
    { "BUZBOM", 0x6624, run_buzbom, 0,0,0,0 },
    { "SAUCMV", 0x677C, run_saucmv, 0,0,0,0 },
    { "OBJOBJ", 0x68D6, run_objobj, 0,0,0,0 },
    { "ROBCHK", 0x69BE, run_robchk, 0,0,0,0 },
    { "TANKCK", 0x69E8, run_tankck, 0,0,0,0 },
    { "R2D3CK", 0x6A22, run_r2d3ck, 0,0,0,0 },
};
#define N_ROUTINES (sizeof routines / sizeof routines[0])
#define NMI_ROUTINE_NAME "NMI"  /* used by main() to find NMI's table index */

/* address -> routines[] index, -1 if none (built once in main()) */
static int addr_routine[0x10000];

typedef struct {
    routine *r;
    int      tainted;
    uint8_t  entry_s, min_s, a, x, y, p;
    uint16_t caller;
    unsigned io_start;
    uint64_t cyc0;  /* ROM cycle count at entry (after the JSR) */
    uint8_t  ram[0x400], vram[0x1000];
} check;

static check stk[MAX_DEPTH];

static unsigned long failures_printed;
static unsigned long mutation_check_only;  /* --mutation-check NAME: only report this routine (quieter demo runs) */
static const char    *mutation_check_name;

/* Run the registered C function of check k from its snapshot and compare
 * with the ROM's own final state.  Returns 1 if anything differs. */
static int run_compare(check *k, const cpu *c)
{
    routine *r = k->r;
    unsigned lo, hi, nbad = 0;
    char diffs[400];
    size_t dl = 0;

    memcpy(g.ram, k->ram, sizeof g.ram);
    memcpy(g.vram, k->vram, sizeof g.vram);
    g.stray_writes = 0;
    R.a = k->a; R.x = k->x; R.y = k->y; R.p = k->p;
    ret_c = ret_a = ret_x = ret_y = ret_z = ret_n = ret_v = -1;
    io_rp = k->io_start; io_rp_end = io_n;
    io_err = 0; io_msg[0] = 0;
    thunk_s = k->entry_s;  /* base SP for any ROM thunk this call reaches */

    c_cyc = 0;
    runaway_n = 0;
    runaway_armed = 1;
    if (setjmp(runaway_jb) == 0) {
        r->run();
    } else {
        size_t ml = strlen(io_msg);
        snprintf(io_msg + ml, sizeof io_msg - ml, " (C call abandoned: still doing I/O %u events after the desync)", RUNAWAY_IO);
    }
    runaway_armed = 0;

    if (!io_err && io_rp != io_rp_end) {
        io_err = 1;
        snprintf(io_msg, sizeof io_msg, "the check's %u I/O event(s) were not replayed", io_rp_end - io_rp);
    }

    diffs[0] = 0;
    lo = 0x100u + k->min_s + 1u;
    hi = 0x100u + k->entry_s;
    for (unsigned a = 0; a < 0x400; a++) {
        if (a >= lo && a <= hi) continue;  /* stack bytes this call itself pushed (PHP/PLP etc.) */
        if (g.ram[a] != R_ram[a]) {
            if (nbad < 6) dl += (size_t)snprintf(diffs + dl, sizeof diffs - dl, " $%04X C=%02X ROM=%02X", a, g.ram[a], R_ram[a]);
            nbad++;
        }
    }
    for (unsigned a = 0; a < 0x1000; a++)
        if (g.vram[a] != R_vram[a]) {
            if (nbad < 6) dl += (size_t)snprintf(diffs + dl, sizeof diffs - dl, " $%04X C=%02X ROM=%02X", a + 0x2000, g.vram[a], R_vram[a]);
            nbad++;
        }
    int flag_bad = (ret_c >= 0 && ret_c != ((c->p & CPU_C) != 0)) ||
                   (ret_z >= 0 && ret_z != ((c->p & CPU_Z) != 0)) ||
                   (ret_n >= 0 && ret_n != ((c->p & CPU_N) != 0)) ||
                   (ret_v >= 0 && ret_v != ((c->p & CPU_V) != 0));
    char regs_msg[80] = "";
    if ((ret_a >= 0 && ret_a != c->a) || (ret_x >= 0 && ret_x != c->x) || (ret_y >= 0 && ret_y != c->y))
        snprintf(regs_msg, sizeof regs_msg, "; exit registers C A=%d X=%d Y=%d, ROM A=%d X=%d Y=%d (-1 = unchecked)",
                 ret_a, ret_x, ret_y, c->a, c->x, c->y);
    int bad = nbad || io_err || flag_bad || g.stray_writes || regs_msg[0];
    uint64_t rom_cyc = cyc - k->cyc0;
    /* SAUCHK ends with JMP C_60S_ back into COLCHK's loop, not an RTS, so its
     * check only closes at COLCHK's own RTS and the ROM count includes the
     * rest of COLCHK - it can never match by construction.  COLCHK's own
     * (exact) count covers it; not compared. */
    if (!strcmp(r->name, "SAUCHK")) { /* not a cycle unit */ }
    else if (c_cyc == rom_cyc) r->cyc_ok++;
    else {
        r->cyc_bad++;
        if ((!mutation_check_name || !strcmp(r->name, mutation_check_name)) && cyc_failures_printed++ < 40)
            printf("LOCKSTEP CYCLES %s #%lu (from $%04X, A=%02X X=%02X Y=%02X P=%02X): C %llu, ROM %llu (%+lld)\n",
                   r->name, r->checked, k->caller, k->a, k->x, k->y, k->p,
                   (unsigned long long)c_cyc, (unsigned long long)rom_cyc, (long long)c_cyc - (long long)rom_cyc);
    }
    if (bad && (!mutation_check_name || !strcmp(r->name, mutation_check_name)) && failures_printed++ < 40) {
        printf("LOCKSTEP FAIL %s #%lu (from $%04X, A=%02X X=%02X Y=%02X P=%02X): %u byte(s)%s%s%s%s%s\n",
               r->name, r->checked, k->caller, k->a, k->x, k->y, k->p, nbad, diffs,
               io_err ? "; I/O: " : "", io_err ? io_msg : "",
               flag_bad ? "; flag(s) differ" : "", regs_msg);
    }
    return bad;
}

/* Named finish_check(), not finish() - bzone_disp.h's real FINISH ($5574)
 * routine is also called `finish()` now that the display module is
 * linked in; kept distinct to avoid the name collision. */
static void finish_check(check *k, const cpu *c)
{
    routine *r = k->r;
    r->checked++;
    if (k->tainted) { r->tainted++; return; }
    if (run_compare(k, c)) r->failed++;
    else r->passed++;
}

static void open_check(routine *r, const cpu *c, uint16_t caller)
{
    check *k;
    if (depth >= MAX_DEPTH) { depth_overflow++; return; }
    k = &stk[depth++];
    k->r = r; k->tainted = 0;
    k->entry_s = k->min_s = c->s;
    k->a = c->a; k->x = c->x; k->y = c->y; k->p = c->p;
    k->caller = caller;
    k->io_start = io_n;
    k->cyc0 = cyc;
    memcpy(k->ram, R_ram, sizeof k->ram);
    memcpy(k->vram, R_vram, sizeof k->vram);
}

/* The four display routines that end in a JMP (never an RTS) and whose C
 * _body() returns a pass_signal at that JMP (bzone_game.h trampoline). */
static int is_tail_chained(uint16_t a)
{
    return a == 0x51E9 /* WNSHLD */ || a == 0x5344 /* CKSCOR */ ||
           a == 0x53C5 /* HISCRE */ || a == 0x549B /* DISTBL */;
}

/* ------------------------------------------------------------------ */
/* --list-untranslated: cross-reference ../disasm/build/program_refs.json's */
/* "routines" object (name -> {"addr": "$XXXX", ...}) against routines[]     */
/* by address.  A small hand-rolled scanner tailored to this file's own,     */
/* regular, generator-produced layout (2-space-indented "NAME": { lines,      */
/* 3-space-indented "addr": "$HEX" lines) - not a general JSON parser.        */
/* ------------------------------------------------------------------ */
typedef struct { char name[32]; uint16_t addr; } refentry;
static refentry refs[256];
static int      refs_n;

static int load_program_refs(const char *path)
{
    FILE *f = fopen(path, "rb");
    char line[256];
    int in_routines = 0;
    char pending[32] = "";

    if (!f) return 1;
    while (fgets(line, sizeof line, f)) {
        if (!in_routines) {
            if (strstr(line, "\"routines\"")) in_routines = 1;
            continue;
        }
        if (line[0] == ' ' && line[1] == ' ' && line[2] == '"') {
            char *start = line + 3;
            char *end = strchr(start, '"');
            if (end && strchr(end, '{')) {
                size_t len = (size_t)(end - start);
                if (len >= sizeof pending) len = sizeof pending - 1;
                memcpy(pending, start, len);
                pending[len] = 0;
            }
        } else if (strstr(line, "\"addr\"")) {
            char *dollar = strchr(line, '$');
            if (dollar && pending[0] && refs_n < (int)(sizeof refs / sizeof refs[0])) {
                strncpy(refs[refs_n].name, pending, sizeof refs[refs_n].name - 1);
                refs[refs_n].name[sizeof refs[refs_n].name - 1] = 0;
                refs[refs_n].addr = (uint16_t)strtoul(dollar + 1, NULL, 16);
                refs_n++;
                pending[0] = 0;
            }
        }
    }
    fclose(f);
    return 0;
}

static void print_untranslated_report(void)
{
    int i, n_reg = 0;
    if (load_program_refs("../disasm/build/program_refs.json")) {
        fprintf(stderr, "LOCKSTEP: --list-untranslated: cannot open ../disasm/build/program_refs.json\n");
        return;
    }
    printf("\nROM ROUTINE COVERAGE (%d routines, ../disasm/build/program_refs.json):\n", refs_n);
    printf("  %-8s %-6s %-3s %8s %8s %8s %8s\n", "name", "addr", "reg", "checked", "passed", "failed", "tainted");
    for (i = 0; i < refs_n; i++) {
        int ri = addr_routine[refs[i].addr];
        if (ri < 0) {
            /* Fall back to a name match: NMI is deliberately absent from
             * addr_routine[] (opened explicitly, not via JSR/JMP - see the
             * header comment), but it IS registered in routines[]. */
            unsigned j;
            for (j = 0; j < N_ROUTINES; j++)
                if (!strcmp(routines[j].name, refs[i].name)) { ri = (int)j; break; }
        }
        if (ri >= 0) {
            n_reg++;
            printf("  %-8s $%04X yes %8lu %8lu %8lu %8lu\n", refs[i].name, refs[i].addr,
                   routines[ri].checked, routines[ri].passed, routines[ri].failed, routines[ri].tainted);
        } else {
            printf("  %-8s $%04X NO  %8s %8s %8s %8s\n", refs[i].name, refs[i].addr, "-", "-", "-", "-");
        }
    }
    printf("  %d/%d ROM routines registered in lockstep's adapter table\n", n_reg, refs_n);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    unsigned frames_target = 5000;
    const char *script_path = NULL;
    int list_untranslated = 0;
    int idx_nmi = -1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames_target = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) script_path = argv[++i];
        else if (!strcmp(argv[i], "--watchdog-cycles") && i + 1 < argc) wd_cycles = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--mutation-check") && i + 1 < argc) mutation_check_name = argv[++i];
        else if (!strcmp(argv[i], "--list-untranslated")) list_untranslated = 1;
        else if (!strcmp(argv[i], "--pass-probe")) pass_probe_enabled = 1;
        else if (!strcmp(argv[i], "--cycles-strict")) cycles_strict = 1;
        else { fprintf(stderr, "usage: see the header of tests/lockstep.c\n"); return 2; }
    }
    if (script_path && load_script(script_path)) return 2;

    for (unsigned a = 0; a < 0x10000; a++) addr_routine[a] = -1;
    for (unsigned i = 0; i < N_ROUTINES; i++) {
        if (!strcmp(routines[i].name, NMI_ROUTINE_NAME)) { idx_nmi = (int)i; continue; }  /* opened explicitly below, never via JSR/JMP */
        /* Integration fix (2026-09-18, NOTES_integration.md): D_NEXT ($5D41)
         * is DRAW's own loop back-edge, entered by "JMP D_NEXT" from inside
         * draw()'s own dispatch - draw() already calls d_next() correctly
         * as a plain nested C call within its OWN check, covering the same
         * effect.  If the ROM's "JMP D_NEXT" ALSO auto-opens a check here
         * (same address, JMP doesn't push - entry_s equals draw()'s own),
         * that check closes at the SAME RTS draw()'s own check does, but
         * d_next()'s adapter only performs the TBLPTR increment - it never
         * runs the REST of draw()'s remaining loop iterations, so the
         * window's real, larger effect always shows up as a mismatch
         * (found by lockstep: every D_NEXT check failed, sometimes 30+
         * bytes).  Excluded from addr_routine[] for the same reason NMI is:
         * still listed in routines[]/--list-untranslated by address, just
         * never auto-opened. */
        if (!strcmp(routines[i].name, "D_NEXT")) continue;
        /* CRITICAL fix (2026-09-18, NOTES_integration.md - "Crash: WNSHLD/
         * CKSCOR/HISCRE/DISTBL replay recurses forever"): each of these
         * four routines' own C body ends with a REAL, unconditional call
         * to b_end() or bz_main() (matching the ROM's own "JMP B_END"/"JMP
         * MAIN" tail) - and b_end()/bz_main() call each other forever,
         * by design, matching the ROM's own endless frame loop (NOTES_
         * game.md's own "flagged for integration" caveat about this
         * recursion). bz_main()/b_end() themselves are already excluded
         * from addr_routine[] so the ROM's OWN "JMP MAIN"/"JMP B_END"
         * never auto-opens a check for them - but that does NOT stop a
         * check that opens for WNSHLD/CKSCOR/HISCRE/DISTBL from crashing
         * once ITS OWN replay actually runs: run_compare() calls the REAL
         * wnshld()/ckscor()/hiscre()/distbl(), and THEIR OWN C body then
         * makes a perfectly ordinary, unbounded C call into b_end()/
         * bz_main() - which recurses forever (the whole rest of the
         * game's execution, transitively) and stack-overflows.  Found by
         * running `coin_start.txt` long enough for the player's tank to
         * actually take a hit (CRACK becomes nonzero for the first time
         * around frame 454): `lockstep --script coin_start.txt --frames
         * 500`+ crashed hard (no output at all - a stack overflow gives
         * the process no chance to flush stdio) while `--frames 453`
         * (one frame earlier, CRACK still 0, WNSHLD never opened) ran
         * clean.  A temporary `fprintf`-per-open-check diagnostic (not
         * kept) showed the last thing opened before the crash was WNSHLD
         * ($50C6, MAIN_30's "JMP WNSHLD" once CRACK != 0) - attract mode
         * never sets CRACK, which is exactly why 5000 attract frames
         * never triggered this.  They were excluded from addr_routine[]
         * until "game"/"display" moved to the pass_signal trampoline
         * (bzone_game.h): wnshld()/ckscor()/hiscre()/distbl() now run
         * their own _body() once and stop where the ROM JMPs elsewhere,
         * so they are auto-opened again.  The window ends at that JMP -
         * into MAIN/B_END (the close just below the ENTRY test in the main
         * loop) or into another of the four (is_tail_chained(), same
         * place). */
        addr_routine[routines[i].addr] = (int)i;
    }

    mb_reset(&mb);
    ad_pokey_init(&pokey, 1512000u, 44100u);
    ad_pokey_set_allpot(&pokey, in_allpot_sw);

    cpu c;
    memset(&c, 0, sizeof c);
    c.read = mem_read;
    c.write = mem_write;
    apply_script(0);
    cpu_reset(&c);

    uint64_t next_nmi = NMI_CYCLES;
    int nmi_pending = 0;
    unsigned frame = 0;
    uint64_t cycle_cap = (uint64_t)(frames_target + 200) * 20u * NMI_CYCLES;
    const char *stop = "frames reached";

    for (;;) {
        if (nmi_pending) {
            uint16_t interrupted_pc = c.pc;
            if (depth > 0) { n_nmi_during_check++; for (int i = 0; i < depth; i++) stk[i].tainted = 1; }
            cpu_nmi(&c);
            cyc += 7;
            nmi_pending = 0;
            n_nmi_taken++;
            /* NMI as its own checked unit (header comment): opened here,
             * explicitly, right after the push+vector-fetch above so
             * entry_s already reflects the 3 pushed bytes - not via the
             * generic JSR/JMP address table (NMI is a hardware edge, not
             * an opcode).  Closes/nests/taints exactly like any other
             * check from this point on (RTI raises SP above entry_s, same
             * as any RTS). */
            if (idx_nmi >= 0) open_check(&routines[idx_nmi], &c, interrupted_pc);
        }
        uint16_t pc = c.pc;
        if (pc == 0x5004) {
            pass_boundary(&c);  /* one MAIN pass boundary - see "Pass-level probe" */
            frame++;
            int ev = apply_script(frame);
            if (frame >= frames_target) break;
            if (ev == SC_END) { stop = "script end"; break; }
        }
        cur_pc = pc;
        uint8_t op = peek(pc);
        if (op == 0x00) n_brk++;
        cpu_step(&c);
        cyc += base_cycles[op] + (is_branch(op) && c.pc != (uint16_t)(pc + 2) ? 1u : 0u);

        /* ENTRY: a JSR ($20) or JMP ($4C) landing on a registered address -
         * never a branch (see the header comment on the tail-branch hazard).
         * bz_main()/b_end() are never registered (see "Pass-level probe"),
         * so a JMP to $5000/$5159 never opens a nested check for them. */
        if ((op == 0x20 || op == 0x4C) && addr_routine[c.pc] >= 0) {
            /* A tail JMP from one trampoline routine into another (the
             * ROM's only case: WNSHLD's L521F "JMP CKSCOR") ends the first
             * one's window: wnshld_body() stops there with PASS_SIG_CKSCOR,
             * so the ROM-side effect must stop there too. */
            if (op == 0x4C && depth > 0 && is_tail_chained(stk[depth - 1].r->addr) &&
                is_tail_chained(c.pc) && c.s == stk[depth - 1].entry_s) {
                depth--;
                finish_check(&stk[depth], &c);
            }
            open_check(&routines[addr_routine[c.pc]], &c, pc);
        } else if (op == 0x4C && (c.pc == 0x5000 || c.pc == 0x5159) && depth > 0) {
            /* Display module's WNSHLD/CKSCOR/HISCRE (and DISTBL) tail-JMP
             * into MAIN/B_END instead of RTS - "game" owns those two names
             * (bz_main/b_end) now, but they are deliberately NOT routine-
             * checked (see "Pass-level probe": bz_main/b_end recurse into
             * each other forever, matching the ROM's own endless JMP-chain
             * loop, so calling them through the ordinary per-call mechanism
             * over a multi-thousand-frame run would grow the C stack
             * without bound).  Per the coordinator's brief: close the
             * currently open check right here - the ROM has left ITS
             * routine's own range via a JMP, not an RTS, so this is where
             * its checked effect ends. */
            depth--;
            finish_check(&stk[depth], &c);
        }

        /* min_s: the lowest SP reached during every still-open check's call
         * (excludes the stack bytes that call itself pushed from COMPARE) */
        for (int i = 0; i < depth; i++) if (c.s < stk[i].min_s) stk[i].min_s = c.s;

        /* COMPLETION: SP rose back above a still-open check's entry SP */
        while (depth > 0 && c.s > stk[depth - 1].entry_s) {
            depth--;
            finish_check(&stk[depth], &c);
        }

        /* Integration fix (2026-09-18, NOTES_integration.md): iolog[]/io_n
         * used to grow for the whole run (M1: always empty, since VGUT never
         * touches hw.h, so this never mattered).  Once a hw.h-using module's
         * checks (nmi-sound-coin, maths' Mathbox writes) started logging for
         * real, io_n blew through IOLOG_MAX within a few hundred frames
         * (Mathbox-heavy routines like PNTPUT/KLUDGE log ~10 events per call,
         * thousands of calls per attract run) - once full, every later I/O
         * event was silently dropped (io_overflow++), corrupting every
         * check's replay window from that point on and causing widespread
         * false failures.  Fix: whenever the check stack fully unwinds
         * (depth 0), every check that ever referenced an iolog[] index has
         * already been replayed and compared in finish_check() above (checks
         * close LIFO, and a check's own replay fully consumes its slice
         * before returning) - so it is always safe to recycle the log here. */
        /* ...except under --pass-probe, whose slice (pass_io_start..io_n)
         * spans the whole pass: pass_boundary() recycles it instead. */
        if (depth == 0 && !pass_probe_enabled) io_n = 0;

        while (cyc >= next_nmi) {
            if (in_in0 & 0x10) {
                if (nmi_pending) n_nmi_lost++;
                nmi_pending = 1;
                n_nmi_raised++;
            }
            next_nmi += NMI_CYCLES;
        }
        if (cyc > cycle_cap) { stop = "CYCLE CAP (stuck?)"; break; }
    }

    unsigned long total_checked = 0, total_passed = 0, total_failed = 0, total_tainted = 0;
    unsigned long total_cyc_ok = 0, total_cyc_bad = 0;
    printf("LOCKSTEP: stop = %s, frames %u, cycles %llu\n", stop, frame, (unsigned long long)cyc);
    printf("  NMIs raised %lu taken %lu lost %lu, NMIs while a check was open (tainting) %lu\n",
           n_nmi_raised, n_nmi_taken, n_nmi_lost, n_nmi_during_check);
    printf("  watchdog kicks %lu (max gap %llu cyc), BRK %lu, unmapped reads %lu (dummy-wait $6B79 excluded: %lu) writes %lu\n",
           n_wdog, (unsigned long long)max_kick_gap_cyc, n_brk, n_unmapped_r, n_dummy_wait_r, n_unmapped_w);
    printf("  io log overflow %lu, depth overflow %lu\n", io_overflow, depth_overflow);
    printf("  per-routine (name: checked passed failed tainted | cycles exact differ):\n");
    for (unsigned i = 0; i < N_ROUTINES; i++) {
        routine *r = &routines[i];
        printf("    %-7s %8lu %8lu %8lu %8lu | %8lu %8lu%s\n", r->name, r->checked, r->passed, r->failed, r->tainted,
               r->cyc_ok, r->cyc_bad, r->checked == 0 ? "   (never called by this scenario)" : "");
        total_cyc_ok += r->cyc_ok; total_cyc_bad += r->cyc_bad;
        total_checked += r->checked; total_passed += r->passed; total_failed += r->failed; total_tainted += r->tainted;
    }
    printf("  TOTAL   %8lu %8lu %8lu %8lu | %8lu %8lu\n", total_checked, total_passed, total_failed, total_tainted,
           total_cyc_ok, total_cyc_bad);
    if (pass_probe_enabled)
        printf("  PASS PROBE (bz_main, one whole pass vs the ROM's own trace of it): %lu compared %lu passed %lu failed\n",
               pass_checked, pass_passed, pass_failed);
    else
        printf("  PASS PROBE: disabled (pass --pass-probe to enable; see NOTES_integration.md - known-broken, and roughly doubles run time)\n");
    printf("LOCKSTEP: %s (%lu compared, %lu failed, %lu tainted; pass probe %lu/%lu failed)\n",
           (total_failed == 0 && pass_failed == 0 && (!cycles_strict || total_cyc_bad == 0)) ? "PASS" : "FAIL",
           total_checked, total_failed, total_tainted, pass_failed, pass_checked);
    printf("LOCKSTEP CYCLES: %lu exact, %lu differ%s\n", total_cyc_ok, total_cyc_bad,
           cycles_strict ? "" : " (not failing the run: pass --cycles-strict)");
    if (list_untranslated) print_untranslated_report();
    return (total_failed == 0 && pass_failed == 0) ? 0 : 1;
}
