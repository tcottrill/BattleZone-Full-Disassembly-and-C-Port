/* bzone_game.h - BZONE game logic (owner: agent "game", bzone_game.c).
 *
 * Routines translated here (BZONE.MAC, addresses from ../disasm/bzone_program_rom.asm):
 * MAIN ($5000-$51D8), EXPLDE ($5B89-$5C5B), QWIKCK/SHRTCK ($5F4E/$5F5B),
 * COLCHK ($6045), SAUCHK ($60C2), NEWLIF/EXINIT ($615F/$6199),
 * MOTION ($61D0), REACT ($63CB), ROBOT ($6418) and family, FIREIT ($6595),
 * BUZBOM ($6624), SAUCMV ($677C), OBJOBJ ($68D6), TR7CHK ($69B5),
 * ROBCHK ($69BE), TANKCK ($69E8), R2D3CK ($6A22), ROB1.
 *
 * Inputs a routine reads become parameters and the results its callers use
 * become the return value.  Everything else lives in state_defs.h RAM
 * cells, as in the ROM.
 */
#ifndef BZONE_GAME_H
#define BZONE_GAME_H

#include <stdint.h>

/* An object-object collision test's result (OBJOBJ). */
typedef struct {
    int touching;  /* the tank is in contact with an obstacle or a tank */
    uint8_t kind;  /* 0 = obstacle, $FF = tank, otherwise explosion flags */
} object_contact;

/* ---- MAIN ($5000-$51D8) --------------------------------------------------
 * main_init(): BZSTST's one-time setup this module owns (none yet - BZSTST
 * itself is not in this milestone's scope; kept for symmetry with the
 * Tempest template's mainln()/mainln_pass() split).
 * main_pass(): one trip around MAIN, from the SYNC frame-wait (through the
 * seam's hw_wait_frame(), see NOTES_game.md) to the tail JMP MAIN. */
void main_init(void);
void main_pass(void);

/* bz_main()/b_end(): the same MAIN routine, exposed under the names the
 * display module's already-written tail calls use (WNSHLD/CKSCOR/HISCRE/
 * DISTBL reach MAIN or B_END via a real ROM `JMP`, never `JSR` - see
 * SIGNATURES.md's display section and NOTES_game.md "bz_main/b_end split").
 * bz_main(): MAIN ($5000) from the SYNC wait through B_END or a display
 * tail call.  b_end(): B_END ($5159), MAIN's mid-routine label - the tank
 * stats/game-over/finish/collision tail every path (including WNSHLD's and
 * CKSCOR's) eventually reaches.  Neither takes register inputs.
 *
 * Integration fix (2026-09-18, NOTES_integration.md "CRITICAL fix... /
 * open item 1 resolved"): these two, plus WNSHLD/CKSCOR/HISCRE/DISTBL
 * (bzone_disp.c), used to tail-call each other as REAL, unbounded C calls
 * at every "JMP MAIN"/"JMP B_END" site - faithful to the ROM's own endless
 * JMP-chained loop, but it meant calling any ONE of these six for real
 * (e.g. to verify it under lockstep, or from a driver) recursed through the
 * rest of the game's execution, transitively, forever - a real stack-
 * overflow crash once WNSHLD's own check was actually replayed with a
 * live game (see NOTES_integration.md's account: reliably crashed
 * `coin_start` the moment the player's tank took its first hit).
 *
 * Fixed with a trampoline: every one of these six routines' translated
 * body lives in a `<name>_body()` function that RETURNS a `pass_signal`
 * instead of tail-calling the next one - "JMP B_END" becomes `return
 * PASS_SIG_BEND;`, "JMP MAIN" becomes `return PASS_SIG_YIELD;` (this
 * pass's work is over - the ROM would be back at MAIN's own SYNC wait),
 * and so on.  `main_pass()` (below) is the ONE trampoline loop that acts
 * on the returned signal and keeps going, bounded, never recursing - the
 * harness/driver calls `main_pass()` once per real pass, matching
 * Tempest's `mainln()`/`mainln_pass()` split this port's own PLAN.md
 * always intended (the "TODO(seam) hw_wait_frame()" comment above
 * predates this fix and already assumed call-and-return semantics).
 * `bz_main()`/`b_end()`/`wnshld()`/`ckscor()`/`hiscre()`/`distbl()` stay
 * as ordinary, individually callable functions (matching every existing
 * caller, including tests\lockstep.c's own adapters) - each just runs its
 * OWN `_body()` once and discards the signal, so calling one directly
 * (standalone, e.g. for a routine check) executes exactly that routine's
 * own translated code and stops exactly where the ROM would jump
 * elsewhere - never recursing, regardless of caller. */
typedef enum {
    PASS_SIG_MAIN,    /* dispatch bz_main_body() (from the top, incl. its own wait) */
    PASS_SIG_BEND,    /* "JMP B_END" */
    PASS_SIG_WNSHLD,  /* "JMP WNSHLD" */
    PASS_SIG_CKSCOR,  /* "JMP CKSCOR" */
    PASS_SIG_HISCRE,  /* "JMP HISCRE" */
    PASS_SIG_DISTBL,  /* "JMP DISTBL" */
    PASS_SIG_YIELD    /* "JMP MAIN" - this pass's work is done */
} pass_signal;

pass_signal bz_main_body(void);  /* MAIN's own translated body (bzone_game.c) */
pass_signal b_end_body(void);    /* B_END's own translated body (bzone_game.c) */

void bz_main(void);
void b_end(void);

/* ---- EXPLDE ($5B89) - update every active explosion-piece slot's X/Y/Z
 * position and orientation, then append the survivors to PTBLX2/PTBLY2/
 * PTBLO2 via ROTPNT.  ENTRY/EXIT y = the running PTBLX2/PTBLY2/PTBLO2
 * write index (ROTATE's own; ROTATE's only call site always passes 0 -
 * LDY #$00 at ROTATE's entry, untouched all the way to JSR EXPLDE).  X is
 * NOT a real input: EXPLDE sets X=$0A itself at entry (LDX #$0A) before
 * ever reading it, so any incoming X is discarded.  Internally loops X =
 * $0A,$08,...,$00 (6 slots, offsets 0/2/4/6/8/10) - see NOTES_game.md for
 * why offsets 12/14 (the other 2 of the nominal 8 EXNOBJ slots) are
 * deliberately excluded (R2D3CK reuses EXPOSZ+$C/+$D as the buzz-bomb's Z
 * depth, not as an explosion piece). */
uint8_t explde(uint8_t y);

/* ---- QWIKCK/SHRTCK ($5F4E/$5F5B) - shell/tank collision check.
 * qwikck(): player's shell vs. robot only (shooter 0, target 2).
 * shrtck(x,y): either shell-vs-tank pair; x/y select whose shell/target.
 * Both return 1 on a collision (flags/counters set), else 0. */
int qwikck(void);
int shrtck(uint8_t x, uint8_t y);

/* ---- COLCHK ($6045) - per-frame object/tank collision bookkeeping. */
void colchk(void);

/* ---- SAUCHK ($60C2) - saucer collision check (up to, not including,
 * ABSTMP - the maths agent's).  ENTRY x = whose shell (0/2, as SHRTCK),
 * y = whose SNDWRD gets the LOX bit on a saucer hit (the target tank's
 * index - only meaningful together with x, both real inputs: SAUCHK reads
 * FIRECT/EXPCNT,X and SNDWRD,Y).  No return (falls through to COLCHK's
 * shared per-shell loop tail either way - see NOTES_game.md). */
void sauchk(uint8_t x, uint8_t y);

/* ---- NEWLIF ($615F) - award a bonus life / super-bonus if the score just
 * crossed the right threshold.  No register inputs (its header's "USES (A)"
 * just means it clobbers the accumulator, not that A is read - the first
 * instruction is `LDA OPTION`); reads `TEMP1` as a hidden RAM input, which
 * the caller must set to the score *before* the current increment
 * (SHRTCK_18/SAUCHK already do this - `TEMP1 = HITS[,X]` right before the
 * decimal HITS add, not a dead store as session 2 first guessed - see
 * NOTES_game.md). No return. */
void newlif(void);

/* ---- EXINIT ($6199) - explosion-piece table init, robot-death only.
 * ENTRY x: ($6199's own header) 2 = player exploded (routine does nothing:
 * `TXA/BNE EXINIT_20` skips straight to RTS - the player's death is the
 * windshield-crack sequence instead, see SHRTCK), 0 = robot exploded (inits
 * EXPOSX/EXPOSY/EXPOSO/EXPOSZ/ZVELOC for the 6 explosion-piece slots from
 * the robot's own TPOSX+2/TPOSY+2 position).  x is unchanged on return in
 * both cases (SHRTCK_19/SAUCHK read FIRECT/EXPCNT,X right after calling
 * this - EXINIT's own `LDX #$00` tail only runs on the x==0 path, where it
 * is a no-op). */
void exinit(uint8_t x);

/* ---- MOTION ($61D0) - apply the player's tread-lever input (attract-mode
 * auto-drive, or the real ALLPOT tread switches via the internal, `static`
 * `m_top()`/MTAB `switch` - see bzone_game.c).  No inputs/outputs; always
 * operates on the player tank (x=0) - every M_* leg reached only from
 * MOTION's own dispatch is `static` in bzone_game.c (never `JSR`/`JMP`'d
 * from outside this cluster - confirmed by grep), so only the generic,
 * X-indexed motion primitives below (also called directly by ROBOT/BUZBOM
 * with x=2 - confirmed by grep) are exposed here. */
void motion(void);

/* ---- M_SET/M_FOR/M_FOR1/M_REV/ITANGL/DTANGL/SAVEIT/BAKOUT ($6310-$63CA) -
 * generic tank/robot motion primitives, ENTRY x = 0 (player tank) or 2
 * (robot) throughout (SAVEIT's own header: "ON ENTRY X=0 MEANS TANK, X=2
 * MEANS ROBOT").  m_set() computes the per-tick X1/Y1 motion vector from
 * TANGLE,x (SIN/COS, maths-owned); m_for()/m_for1()/m_rev() apply it to
 * TPOSX/TPOSY (m_for() calls m_set() first, m_for1() assumes X1/Y1 already
 * set - both are independently called from outside MOTION); itangl()/
 * dtangl() pivot LANGLE/TANGLE by one step; saveit()/bakout() save/restore
 * TPOSX/TPOSY around a collision retry (OTPOSX/OTPOSY). No return values. */
void m_set(uint8_t x);
void m_for(uint8_t x);
void m_for1(uint8_t x);
void m_rev(uint8_t x);
void itangl(uint8_t x);
void dtangl(uint8_t x);
void saveit(uint8_t x);
void bakout(uint8_t x);

/* ---- REACT ($63CB) - set the robot's ACTION (reaction delay) and SKILL
 * (aim accuracy) from the current score gap (HITS vs. HITS+2) or a fixed
 * default, depending on ATRACT.  ENTRY a: only read on the path that skips
 * straight to REACT_10 (`BMI REACT_10`) - REACT is entered via `JMP REACT`
 * (tail call) from R_RAND/R_ATCK (ROBOT family, not yet translated), never
 * `JSR`, so `a` there is whatever those routines held at their own
 * `JMP REACT` - **the Atari header says "REGISTERS: NONE", which appears
 * to be stale/incomplete documentation given the code's own use of entry-A
 * on that path; re-verify once R_RAND/R_ATCK are translated** (see
 * NOTES_game.md). No return value (`OUTPUT PARAMS: ACTION, SKILL`, both
 * RAM). */
void react(uint8_t a);

/* ---- ROBOT ($6418) and family - enemy tank AI (owner: agent "robot",
 * bzone_robot.c; see NOTES_robot.md).  ROBOT itself takes no register
 * inputs, x=2 (the robot) hardwired throughout.  R_37S_/R_40S_/R_50S_/
 * R_EVAD/R_RAND/R_ATCK are reached only from inside ROBOT's own control
 * flow (grepped the whole disassembly - no external JSR/JMP), same as
 * MOTION's internal M_* legs, but each has its own listing header/`[note]`
 * block (routine_docs.py treats them as distinct routines) and FIREIT is
 * JSR'd 8 times from within ROBOT alone, so all seven get real (non-
 * `static`) functions here rather than being inlined as `goto` labels
 * inside robot() - see NOTES_robot.md for the full reasoning.  None take
 * register inputs; R_RAND/R_ATCK tail-call react() with a fixed A (0x00/
 * 0x80 respectively, confirmed by hand-tracing - closes NOTES_game.md's
 * "REGISTERS: NONE... re-verify once R_RAND/R_ATCK are translated" flag). */
void robot(void);
void r_37s_(void);
void r_40s_(void);
void r_50s_(void);
void r_evad(void);
void r_rand(void);
void r_atck(void);

/* ---- FIREIT ($6595) - range/angle-gated shell firing.  **Signature
 * corrected** from the pre-existing draft `void fireit(uint8_t x)`: every
 * cell FIREIT touches is written with a hardwired +2/+3 literal offset in
 * the ROM (never `,X`), and it is JSR'd/JMP'd only from within ROBOT (x=2
 * always) - it is robot-only, not a generic tank/robot primitive like
 * SAVEIT/ITANGL/etc. despite the family resemblance. No inputs/outputs. */
void fireit(void);

/* ---- BUZBOM ($6624) - buzz-bomb (R2D3) AI/motion. */
void buzbom(void);

/* ---- SAUCMV ($677C) - flying-saucer AI (up to, not including, TRACK -
 * the maths agent's). */
void saucmv(void);

/* ---- OBJOBJ ($68D6) - object-object collision detect.  tank = 0 player
 * tank, 2 robot tank. */
object_contact objobj(uint8_t tank);

/* ---- TR7CHK ($69B5) - TR7 tank variant branch.  Returns 1 for the TR7
 * variant (MAIN's tail at B_END_19 branches on it). */
int tr7chk(void);

/* ---- ROBCHK ($69BE) - robot reset check; shared with the NMI handler
 * (nmi-sound-coin's callers must include this header). */
void robchk(void);

/* ---- TANKCK ($69E8) - reset/respawn the demo/enemy tank. */
void tankck(void);

/* ---- R2D3CK ($6A22) - spawn/reset the buzz-bomb opponent. */
void r2d3ck(void);

/* ---- ROB1 - robot AI helper family, up to (not including) DRADAR
 * ($6AE9, the display agent's).  No prototype here: grepping the whole
 * disassembly shows ROB1 is never JSR'd/JMP'd from outside the ROBCHK/
 * TANKCK/R2D3CK/ROB1 cluster (reached only via TANKCK's own tail JMP/BEQ
 * or R2D3CK's fall-through) - `static` in bzone_enemy.c, same convention
 * as bzone_game.c's own internal-only fall-through targets (`c21s_`,
 * `m_top`, etc. - no header entry).  This replaces the earlier draft
 * `void rob1(void);` prototype, which was also the wrong shape (ROB1
 * genuinely reads an input `a` - see NOTES_enemy.md). */

#endif  /* BZONE_GAME_H */
