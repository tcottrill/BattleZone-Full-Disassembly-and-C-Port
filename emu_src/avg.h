/* avg.h - Battlezone C port: the Analog Vector Generator display-list walker.
 *
 * Adapted from the Tempest port's C:\Source2026\Tempest Dissasembly\c_src\avg.c
 * /avg.h (colour AVG; itself adapted from the Space Duel walker,
 * harvest/avg.c).  Semantics here are Battlezone-specific and were checked
 * against three independent sources (see NOTES_avg.md for the full evidence
 * trail, including two points where they had to be reconciled):
 *   - ../disasm/avg.py, ../disasm/vrender.py  (the disassembly's own,
 *     byte-verified opcode decoder and display-list walker - this is the
 *     Gate V reference c_src/tests/avgshapes.c and tools/avg_gate_v.py check
 *     against)
 *   - battlezone-main/VGMC.MAC (Ed Logg's own vector macros: CNTR, HALT,
 *     JSRL, STAT, SCAL, RTSL, JMPL, VCTR - the primary source; its comments
 *     are quoted in NOTES_avg.md)
 *   - AAE's Battlezone AVG core, ../AAE_publish_new_vector_test/aae/aae/
 *     vidhrdwr/mame_late_avgdvg.cpp (bzone_latch1/bzone_strobe2/bzone_strobe3/
 *     avg_bzone), the same avgdvg family Tempest/Space Duel use
 *
 * WHAT IS DIFFERENT FROM TEMPEST'S avg.c (see NOTES_avg.md for evidence)
 *   - Monochrome: no colour STAT, no colour RAM.  VCTR/SVEC's 3-bit Z field
 *     is used DIRECTLY as the segment's beam intensity - there is no
 *     Tempest-style "Z==1 -> substitute the STAT intensity latch" rule.
 *   - STAT carries an extra clip-WINDOW mode (VGMC.MAC's HI/LOW, IN/OUT
 *     parameters): with HI/IN given, STAT arms one corner of a rectangular
 *     window at the current beam position instead of just setting the
 *     intensity latch.  The walker tracks the window (avg_state) and tags
 *     every segment with its clip state (avg_seg.clip) but does NOT apply
 *     the clip to segment geometry - see "CLIP WINDOW" below and
 *     NOTES_avg.md.
 *   - Hardware JSRL/RTSL stack depth is 4 slots (mod-16 counter, same
 *     avgdvg family as Tempest) even though VGMC.MAC's own RTSL comment
 *     says "five levels" - NOTES_avg.md documents the discrepancy.
 *   - No AVG_OPT_PREVIEW / vram-skip mode: the walker always reads through
 *     the caller's avg_mem exactly as given (Tempest needed a preview mode
 *     because its shape test zeroes vram; Battlezone's Gate V instead loads
 *     the *same* 64K ROM image ../disasm/avg.py does, byte for byte, so
 *     vram and vrom are already consistent with the reference and no
 *     special-casing is needed).
 *
 * MEMORY MAP (flat, the AVG's own address space = CPU $2000-$3FFF; confirmed
 * against ../bzone.cpp's AAE_DRIVER_VECTORRAM(0x2000, 0x1000) and
 * ../disasm/bzone_defines.asm's VECRAM/VECROM aliases - identical shape to
 * Tempest's map, just monochrome)
 *   $2000-$2FFF  4K vector RAM  (the live display list VGUT/BZONE build)
 *   $3000-$3FFF  4K vector ROM  (pictures/shapes/VGAN characters)
 *   JSRL/JMPL target = $2000 + 2 * (word & $1FFF) (VGMC.MAC: "THE LOWER
 *   THREE NYBBLES SPECIFY AN ADDRESS RELATIVE TO THE BEGINNING OF THE VECTOR
 *   GENERATOR ADDRESSABLE INSTRUCTION SPACE DIVIDED BY 2").
 *
 * OPCODES (word >> 13; the first word of each instruction, little-endian in
 * the ROM/RAM byte stream) - byte-identical to ../disasm/avg.py:
 *   0 VCTR  2 words: w1 = DY & $1FFF, w2 = Z<<13 | DX & $1FFF (13-bit two's
 *           complement deltas, confirmed by VGMC.MAC's VCTR macro: ".WORD
 *           DY&^H1FFF,<ZZ*^H2000>+<DX&^H1FFF>" - never sign-magnitude).
 *   1 HALT  $2000 (VGMC.MAC's HALT macro; not used by any ROM picture here -
 *           every AVG subroutine ends RTSL - kept for decode completeness).
 *   2 SVEC  short vector, 1 word: $4000 | Z<<5 | (DX/2)&$1F | ((DY/2)&$1F)<<8
 *           - VGMC's VCTR macro picks this automatically when |DX| and |DY|
 *           are both even and <= 30 (its "^H0FFE1" mask test).
 *   3 STAT  bit 12 ($1000) clear: intensity/window latch (see CLIP WINDOW).
 *       SCAL  bit 12 set: binary scale b = (w>>8)&7, linear l = w&$FF;
 *           factor = (255-l)/256 / 2^b (VGMC.MAC: "0 means full size, 1=1/2
 *           size, ..., 7=1/128 size"; linear "0=full size, 80=1/2, FF=1/256").
 *   4 CNTR  $8040 - beam to (0,0); the word's low byte only affects settling
 *           TIME on real hardware (avg_common_strobe3's OP2 branch is
 *           unconditional), never modelled here (see avg.h TIMING notes in
 *           Tempest's port; not carried to this milestone).
 *   5 JSRL  6 RTSL  7 JMPL - as VGMC.MAC/avg.py; JSRL/RTSL use the real
 *           4-slot hardware stack (see NOTES_avg.md on VGMC's "5 levels").
 *
 * INTENSITY
 *   VCTR/SVEC's Z field (0-7) is the segment's beam intensity DIRECTLY - no
 *   substitution.  Evidence: VGMC.MAC's VCTR macro comment ("ZZ SPECIFIES
 *   THE BEAM INTENSITY (0 THROUGH 7.)", no exception noted); the HATCH
 *   self-test picture's brightness-bar sequence ($3768-$3780, SVEC Z =
 *   7,6,5,4,3,2,1 in order - if Z==1 meant "use the STAT latch" the dimmest
 *   bar would not show level 1); and ../disasm/vrender.py's own
 *   byte-verified model, which uses Z with no substitution.  See
 *   NOTES_avg.md for why this reading is preferred over a literal quoting of
 *   AAE's bzone_strobe3, which reuses avg_common's "(int_latch>>1)==1"
 *   idiom in a way that a nibble-level trace shows can only be driven by a
 *   *stale* int_latch for SVEC (the state machine's latch3 - the only place
 *   int_latch is written - fires only on a VCTR's second word).
 *   STAT's own Z parameter sets an "intensity latch" (avg_state.intensity)
 *   that is tracked for completeness/future use but never applied to a
 *   segment.
 *
 * CLIP WINDOW (VGMC.MAC's STAT HI/LOW, IN/OUT parameters; AAE's
 * bzone_latch1/bzone_strobe2; see NOTES_avg.md for the full derivation)
 *   STAT Z alone (HI/IN both omitted) just loads the intensity latch (word
 *   bit $0400 - VGMC's macro calls this "...1", the "not a window" bit).
 *   STAT Z,HI,IN additionally arms one corner of a rectangular window AT THE
 *   CURRENT BEAM POSITION: HI=1 is the upper-right corner (xmax,ymax),
 *   HI=0 the lower-left (xmin,ymin) - VGMC.MAC's own comment, quoted
 *   verbatim in NOTES_avg.md.  IN=0 draws inside the window, IN=1 outside
 *   (also VGMC.MAC's comment; AAE stores this as vg->izblank and notes
 *   Battlezone never inverts it).  AAE's bzone_latch1 captures the corner
 *   one word-fetch after the arming STAT (a pipelining artifact of its
 *   serial nibble latches) - since neither the beam position nor the window
 *   registers can change between a STAT and the very next fetch without
 *   that next instruction being the one actually moving the beam (and by
 *   then it is too late to be "the STAT's" position), the observable corner
 *   value is always exactly the beam position AT the STAT - avg_walk()
 *   captures it immediately, which is behaviourally identical and avoids
 *   modelling the one-fetch pipeline delay.
 *   avg_walk() tracks the window rectangle and an "armed" flag in avg_state,
 *   and tags every avg_seg with AVG_CLIP_ARMED / AVG_CLIP_OUT so a future
 *   renderer can blank vectors outside (or inside) the window - it does NOT
 *   clip segment geometry itself: neither does ../disasm/vrender.py, so
 *   Gate V compares unclipped geometry.  The only ROM picture that arms a
 *   window is HATCH (the self-test grid); no attract/play picture in this
 *   ROM does, so the "blank the picture above/below the horizon window"
 *   mechanism the task brief mentions is exercised by BZONE.MAC's runtime
 *   display list (M3+), not by anything Gate V walks - open item, see
 *   NOTES_avg.md.
 *
 * COORDINATES
 *   Q15 fixed point (1.0 = 32768), unit = one LSB of a 13-bit VCTR delta at
 *   scale 1.0 (exactly Tempest's convention - see avg_scal_q15()).  Unlike
 *   Tempest (whose avg_state_init() starts scale at 0, an AAE/Tempest-
 *   specific quirk), avg_state_init() here starts scale at Q15 1.0
 *   (AVG_Q15_ONE) - matching ../disasm/vrender.py's own `scale = 1.0`
 *   picture-preview convention, which is what every ROM picture is walked
 *   from (most, e.g. HORIZN, contain no SCAL of their own and rely on the
 *   caller's scale - Gate V needs the same starting convention on both
 *   sides).  This is a rendering-preview convention, not a claim about the
 *   AVG's true power-on register value.
 *
 * TIMING (authentic MAME state-machine model, copied unchanged from Tempest's
 *   C:\Source2026\Tempest Dissasembly\c_src\avg.h/.c - see NOTES_avg.md
 *   "Timing" for the evidence this is the SAME model, not an approximation:
 *   Battlezone's AVG state PROM, ../roms/bzonea.zip member 036408-01.k7, is
 *   byte-identical (CRC32 5903AF03, 256 bytes) to Tempest's 136002-125, and
 *   AAE's avg_bzone (mame_late_avgdvg.cpp) reuses avg_strobe0/avg_strobe1/
 *   avg_common_strobe2/avg_common_strobe3 unchanged - its bzone_latch1/
 *   bzone_strobe2/bzone_strobe3 add clip-window and monochrome-intensity
 *   bookkeeping only, never touching a cycle count)
 *   The AVG runs off the 12.096 MHz master clock; the 256x4 state PROM is
 *   stepped every 8 master cycles (one 6502 cycle = one PROM tick).  Every
 *   opcode costs a fixed number of PROM ticks (state-machine traversal) plus,
 *   for VCTR/SVEC/CNTR, the beam's own settling/draw time from the timer
 *   shift register the normalizer (and SCAL's binary scale) fill:
 *     VCTR 8 ticks + (0x8000 - timer)              SVEC 6 ticks + (0x100 - (timer&0xFF))
 *     STAT/SCAL 7 ticks                             CNTR 5 ticks + (0x8000 - timer)
 *     JSRL 5 ticks   RTSL 4 ticks   JMPL 3 ticks    HALT 2 ticks
 *   plus one idle PROM tick (AVG_VGGO_LEADIN) after the GO strobe, before the
 *   first latch - NOT added inside avg_walk() (the caller adds it once, when
 *   starting a fresh pass from a GOADD strobe; a JSRL'd picture walk has no
 *   lead-in of its own).  avg_result.cycles is the running sum in master
 *   cycles; avg_cycles_ms() converts to milliseconds.
 *   Unlike Tempest (whose master lists end in an endless JMPL-to-self loop,
 *   AVG_STOP_LOOP), Battlezone's real display lists (built by VGUT/BZONE at
 *   $2000 each frame) end in HALT: the 6502 polls IN0 bit 6 (VG HALT) and
 *   restarts the AVG from NMI every VTIMER (6 ticks).  avg_result carries
 *   both `stop` (AVG_STOP_HALT is the expected case for a full-frame walk)
 *   and `cycles` (the draw time from GO to that HALT) so a future oracle/
 *   playable-build harness can decide exactly when HALT reads true, without
 *   any change to this file.
 *   Gate T (tools/avg_prom_sim.py + tests/avgtime.c) checks avg_result.cycles
 *   against an independent Python transcription of MAME's real state machine
 *   run on the actual 036408-01.k7 PROM bytes, cycle for cycle, over every
 *   ROM picture and a handful of synthetic lists (SCAL binary/linear, CNTR,
 *   JSRL nesting, a short vector) - see NOTES_avg.md.
 *
 * STACK
 *   4 physical slots addressed by a 4-bit up/down counter (stack[sp&3], sp
 *   = (sp +/- 1) & $F) - the same avgdvg-family hardware Tempest's avg.c
 *   documents; a 5th nested JSRL silently overwrites slot 0.  RTSL at depth
 *   0 ends the walk (AVG_STOP_RTSL) - how a subroutine/picture walk
 *   finishes.  See NOTES_avg.md for the discrepancy with VGMC.MAC's RTSL
 *   comment ("THE VG IS CAPABLE OF FIVE LEVELS OF SUBROUTINE CALLS").
 *
 * ACCESSOR
 *   avg_walk() takes the vector memory through avg_mem (two raw byte
 *   pointers, vram/vrom - no platform includes, no dependency on any
 *   generated state.h/vecrom.c).  tests/avgshapes.c builds an avg_mem
 *   straight from ../disasm/_survey/roms_extracted/bzone_cpu64k.bin (the
 *   same 64K image ../disasm/avg.py loads) so Gate V compares byte-for-byte
 *   identical memory on both sides.  Once the machine-state scaffold exists,
 *   another file (not this one) builds an avg_mem from g.vram + vecrom[].
 *
 * Plain C11, no platform dependencies.
 */
#ifndef AVG_H
#define AVG_H

#include <stddef.h>
#include <stdint.h>

#define AVG_VRAM_BASE     0x2000u
#define AVG_VROM_BASE     0x3000u
#define AVG_SPACE_END     0x4000u
#define AVG_STACK_SLOTS   4
#define AVG_FRAME_BUDGET  20000u    /* default instruction budget per walk   */

#define AVG_Q15_ONE       32768
#define AVG_Q15_TO_F(v)   ((double)(v) / 32768.0)

#define AVG_MASTER_HZ     12096000.0 /* AVG state machine clock (TIMING)      */
#define AVG_CYC_PER_CPU   8u         /* master cycles per 6502 cycle / PROM tick */
#define AVG_VGGO_LEADIN   8u         /* one idle PROM tick after VGGO; the
                                      * caller adds this, avg_walk() does not */
#define avg_cycles_ms(c)  ((double)(c) * (1000.0 / AVG_MASTER_HZ))

/* Why a walk ended. */
typedef enum {
    AVG_STOP_HALT = 0,    /* HALT instruction                                */
    AVG_STOP_LOOP,        /* JMPL at depth 0 back to the start address       */
    AVG_STOP_RTSL,         /* RTSL at depth 0 (end of a subroutine walk)      */
    AVG_STOP_BUDGET,       /* instruction budget exhausted                    */
    AVG_STOP_BADADDR       /* fetch outside $2000-$3FFF                       */
} avg_stop;

/* Diagnostic flags (avg_result.flags). */
#define AVG_FLAG_STACK_OVER  0x01u  /* nesting exceeded the 4 hardware slots  */

/* Per-segment clip-window flags (avg_seg.clip; see "CLIP WINDOW" above). */
#define AVG_CLIP_ARMED  0x01u  /* a window rectangle has been fully defined
                                 * (both a HI=1 and a HI=0 STAT seen) by the
                                 * time this segment was drawn               */
#define AVG_CLIP_OUT    0x02u  /* the most recent window-arming STAT had
                                 * IN/OUT=1 (draw outside the window); only
                                 * meaningful when AVG_CLIP_ARMED is set     */

/* One beam movement (every VCTR / SVEC, dark moves included). */
typedef struct {
    int32_t  x0, y0, x1, y1;   /* Q15 AVG coordinates                        */
    uint8_t  intensity;        /* 0..7, VCTR/SVEC Z direct (0 = dark move)   */
    uint8_t  svec;              /* 1 = SVEC, 0 = VCTR                        */
    uint8_t  clip;              /* AVG_CLIP_* at the time this was drawn     */
    uint16_t pc;                /* CPU address of the instruction            */
} avg_seg;

/* The memory the AVG sees - the accessor.  Both pointers 4096 bytes; the
 * caller owns the storage (a raw ROM image slice, or later g.vram/vecrom[]). */
typedef struct {
    const uint8_t *vram;       /* CPU $2000-$2FFF                            */
    const uint8_t *vrom;       /* CPU $3000-$3FFF                            */
} avg_mem;

/* Beam / latch / clip-window state; carried across walks if the caller wants
 * to (e.g. a JSRL'd picture that depends on the caller's scale). */
typedef struct {
    int64_t  x, y;              /* Q15 beam position                         */
    int32_t  scale_q15;         /* current scale factor, Q15                 */
    int      bin_scale;         /* SCAL binary field b (0..7)                */
    int      intensity;         /* STAT intensity latch 0..15 (tracked only -
                                  * see "INTENSITY" above; never applied to a
                                  * segment)                                 */
    uint16_t stack[AVG_STACK_SLOTS];
    uint8_t  sp;                 /* 4-bit hardware counter                    */
    int      depth;              /* logical nesting depth (for diagnostics)   */

    /* Clip window (see "CLIP WINDOW" above). */
    int32_t  clip_xmin, clip_ymin, clip_xmax, clip_ymax;  /* Q15 corners      */
    int      clip_have_lo;       /* a HI=0 STAT has armed (xmin,ymin)         */
    int      clip_have_hi;       /* a HI=1 STAT has armed (xmax,ymax)         */
    int      clip_out;           /* most recent window STAT's IN/OUT bit      */
} avg_state;

typedef void (*avg_seg_fn)(void *ctx, const avg_seg *s);

typedef struct {
    avg_seg_fn seg;             /* per-segment callback, may be NULL          */
    void      *ctx;
    avg_seg   *segs;             /* optional array sink, may be NULL          */
    size_t     seg_cap;
    uint32_t   max_ops;          /* 0 = AVG_FRAME_BUDGET                      */
} avg_cfg;

typedef struct {
    avg_stop stop;
    unsigned flags;              /* AVG_FLAG_*                                */
    uint16_t stop_pc;             /* address of the instruction that ended it  */
    uint32_t ops;                 /* instructions executed (incl. the last one)*/
    uint32_t cycles;               /* draw time in AVG master cycles (TIMING),
                                     * without the VGGO lead-in tick             */
    uint32_t nseg;                 /* VCTR + SVEC executed                      */
    uint32_t nseg_stored;          /* segments written to cfg->segs             */
    uint32_t nlit;                  /* segments with intensity > 0               */
    int      max_depth;             /* deepest logical nesting reached           */
} avg_result;

/* Power-on-like state: centre, scale Q15 1.0 (see COORDINATES above),
 * intensity 0, empty stack, no clip window armed. */
void avg_state_init(avg_state *st);

/* Walk from CPU address `start` ($2000-$3FFE).  st may be NULL (fresh
 * avg_state_init state).  cfg may be NULL (budget only, no output). */
avg_result avg_walk(const avg_mem *m, uint16_t start, avg_state *st,
                    const avg_cfg *cfg);

/* SCAL b,l scale factor as Q15: (255 - l) << (7 - (b & 7)).  Mathematically
 * (255-l)/256 / 2^b - byte-identical to ../disasm/avg.py's scal_factor()
 * (and to Tempest's avg_scal_q15()). */
int32_t avg_scal_q15(int bin, int lin);

const char *avg_stop_name(avg_stop s);

#endif /* AVG_H */
