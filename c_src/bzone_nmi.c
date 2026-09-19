/* bzone_nmi.c - the Battlezone NMI handler (BZONE.MAC $558F-$56C2).
 *
 * Atari's header claims "SUBROUTINES: MOOLAH,MODSND,SNDON,RDBCHK" - RDBCHK
 * does not exist anywhere in this ROM; the only similarly-named routine NMI
 * actually calls is ROBCHK ($69BE), almost certainly what the header means
 * (../disasm/CALLING_NOTES.md section 7). ROBCHK belongs to the "game"
 * agent (bzone_game.c) - called here by signature only.
 *
 * Translated as `void nmi(void)` per the module brief: PHA/PLA register
 * saves are modelled as nothing (elided below with a comment at each spot),
 * but every RAM and hardware access happens in the ROM's own order. Two
 * exceptions where the stack genuinely carries DATA (not a register save)
 * are kept as real local variables: the sound-word accumulator saved at
 * NMI_65's PHA/consumed by NMI_55 or NMI_68's PLA, and the one call site
 * (NMI_60, before the slam-alarm SNDON call) that saves/restores it around
 * a nested call.
 *
 * The stack-pointer-blown re-entrancy guard (TSX/BMI NMI_2, spinning
 * forever if the 6502 hardware stack is corrupted) has no equivalent in
 * this C port - there is no 256-byte hardware stack for A/X/Y pushes to
 * blow. Elided; nmi() always takes the path real hardware takes when the
 * guard doesn't trip, which is every execution this ROM is ever observed
 * to take (PLAN.md; CALLING_NOTES.md section 7 hazards table).
 *
 * Cycle counting (TRANSLATING.md/hw.h CYC()): every PHA/PLA/TSX/BMI the
 * header above calls "elided" still spent real 6502 cycles on the board,
 * so each elided span is charged with its own CYC() even though nothing
 * else in that statement has a C-visible effect. The NMI's own epilogue
 * (PLA/TAY/PLA/TAX/PLA/RTI, NMI_20 $56BD-$56C2, 22 cycles) is shared by
 * BOTH the early "VTIMER not due" return and the normal fall-off return at
 * the end of the function - charged at each. See NOTES_nmi_sound_coin.md
 * "Cycle counting" for the judgement calls (the ternary CYC() on the
 * sound-priority cascade, the reused NMI_54/NMI_56 tails).
 */
#include "state.h"
#include "hw.h"
#include "coin65.h"
#include "bzsoun.h"
#include "bzone_game.h"   /* ROBCHK ($69BE) - now that the "game" agent's header exists
                            * (TRANSLATING.md's seam rule: swap the temporary local
                            * prototype for the real header once it appears). */
#include "bzone_nmi.h"

/* NMI ($558F): see bzone_nmi.h. */
void nmi(void)
{
    uint8_t snd;  /* the SOUND ($1840) latch value being built up */
    uint8_t saved_snd;       /* NMI_65's PHA (L5622), consumed by exactly one
                              * of NMI_55's PLA (L5691) or NMI_68's PLA (L5631) */

    /* L558F-5594: PHA/TXA/TSX/BMI NMI_2, then PHA - entry register save +
     * re-entrancy guard, elided (see file header). */
    CYC(12);
    S_INTCT = (uint8_t)(S_INTCT + 1);  /* L5595 INC S_INTCT */
    CYC(5);
    CYC(7);                         /* L5597 LDA S_INTCT / L5599 AND #$0F / L559B BNE (not taken) */
    if ((S_INTCT & 0x0F) == 0)   {  /* L5597-559B AND #$0F/BNE NMI_10 */
        CYC(5);                      /* L559D INC SYNC */
        SYNC = (uint8_t)(SYNC + 1);  /* L559D INC SYNC: one game frame / 16 NMIs */
        /* L559F: BMI NMI_2 - same guard, elided.  Only on this path: the
         * BNE at L559B jumps to NMI_10 ($55A1), past it. */
        CYC(2);
    } else {
        CYC(1);  /* L559B BNE taken */
    }

    /* L55A1-55A2: TYA/PHA - register save, elided. */
    CYC(5);
    CYC(2);                 /* L55A3 CLD */
    g.dflag = 0;            /* L55A3 CLD: protect MOOLAH's binary-mode math */
    CYC(4);                 /* L55A4 STA WTCHDG */
    hw_wtchdg();            /* L55A4 STA WTCHDG */
    CYC(7);                 /* L55A7 LDA OPTON2 / L55AA STA S_CMODE */
    S_CMODE = hw_opton2();  /* L55A7/AA LDA OPTON2/STA S_CMODE */
    CYC(6);                 /* L55AC JSR MOOLAH */
    moolah();               /* L55AC JSR MOOLAH */

    /* L55AF-55BC: pack the 3 CCTIM timers' sign bits (bit2=CCTIM[0], bit1=
     * CCTIM[1], bit0=CCTIM[2] of the result) into one EMCNT byte, via a
     * ROL/carry chain across two loads with a PHA/PLA in between. Simulated
     * literally, instruction by instruction - the incoming carry (from
     * MOOLAH's undocumented exit flags) provably can't reach the final
     * byte, since the very first ROL's own result is thrown away by the
     * very next LDA. */
    {
        uint8_t a, saved;
        unsigned carry, next;

        CYC(30);                          /* L55AF-55BC: LDA/ROL/LDA/ROL/ROL/PHA/LDA/ROL/PLA/ROL/STA */
        a = S_CCTIM;                      /* L55AF LDA S_CCTIM */
        carry = (unsigned)(a >> 7) & 1u;  /* L55B1 ROL (rotated A discarded) */

        a = S_CCTIM_AT(1);  /* L55B2 LDA S_CCTIM+1 */
        next = (unsigned)(a >> 7) & 1u;
        a = (uint8_t)((a << 1) | carry);  /* L55B4 ROL */
        carry = next;
        next = (unsigned)(a >> 7) & 1u;
        a = (uint8_t)((a << 1) | carry);  /* L55B5 ROL (2nd, same accumulator) */
        carry = next;
        saved = a;  /* L55B6 PHA */

        a = S_CCTIM_AT(2);  /* L55B7 LDA S_CCTIM+2 */
        next = (unsigned)(a >> 7) & 1u;
        a = (uint8_t)((a << 1) | carry);  /* L55B9 ROL */
        carry = next;

        a = saved;                        /* L55BA PLA */
        a = (uint8_t)((a << 1) | carry);  /* L55BB ROL */
        hw_emcnt(a);                      /* L55BC STA EMCNT */
    }

    CYC(6);    /* L55BF JSR MODSND */
    modsnd();  /* L55BF JSR MODSND */

    /* L55C2-55E1: pick one new sound by priority - super-bonus lockout (do
     * nothing) > saucer-disintegration-collision (SCOLFG) > object
     * collision (OBJCOL) > saucer present (SAUCER bit7). Only fires if
     * channel 0 is currently idle (POINT == 0).
     *
     * The outer test is a single C `&&` over TWO ROM branches (BMI SUPER,
     * then BNE POINT) with different taken/not-taken costs, and the
     * bit-selection is a 3-way if/else-if/else cascade (SCOLFG/OBJCOL/
     * SAUCER) each ending in its own branch back to the shared JSR SNDON -
     * rather than splitting these into nested ifs (which TRANSLATING.md's
     * "only add CYC lines" asks not to do), the whole L55C2-55DF span is
     * charged with one path-dependent CYC() ternary, keyed off the same
     * flags the `if`s below test (see NOTES_nmi_sound_coin.md). */
    CYC(!(SUPER & 0x80)
            ? (POINT == 0
                   ? (SCOLFG != 0 ? 21
                      : OBJCOL != 0 ? 27
                      : (SAUCER & 0x80) ? 30
                      : 29)
                   : 12)
            : 7);  /* L55C2-55DF: sound-priority pick, path-dependent */
    if (!(SUPER & 0x80) && POINT == 0) {  /* L55C2-55C9 */
        int fire = 1;
        uint8_t bit = 0;
        if (SCOLFG != 0)                bit = K_DISINT;  /* L55CB-55CF */
        else if (OBJCOL != 0)           bit = K_BLKSND;  /* L55D3-55D7 */
        else if (SAUCER & 0x80)         bit = K_SAUSND;  /* L55DB-55DF */
        else                              fire = 0;
        if (fire) {
            CYC(6);            /* L55E1 JSR SNDON */
            sndon(bit, 0xFF);  /* L55E1 JSR SNDON (X is MODSND's leftover $FF; a != 0 here so x is unused) */
        }
    }

    /* L55E4-5600: fold in the explosion and shell-explosion sounds, then
     * OR SNDWRD's own bits. */
    {
        uint8_t y = 0;
        CYC(3);             /* L55E4 LDA EXPCNT */
        if (EXPCNT != 0) {  /* L55E6 BEQ NMI_17 (not taken) */
            CYC(2);
            CYC(5);                          /* L55E8 DEC EXPCNT */
            EXPCNT = (uint8_t)(EXPCNT - 1);  /* L55E8 DEC EXPCNT */
            if (EXPCNT != 0) {               /* L55EA BNE NMI_16 (taken) */
                CYC(3);
                y = 1;   /* NMI_16: turn on explosion sound */
                CYC(2);  /* L55F0 LDA #$01 */
                /* else y stays 0: shut off explosion sound */
            } else {
                CYC(2);  /* L55EA BNE not taken */
                CYC(2);  /* L55EC LDA #$00 */
                CYC(3);  /* L55EE BEQ NMI_17 (taken) */
            }
        } else {
            CYC(3);  /* L55E6 BEQ taken */
        }
        CYC(2);             /* NMI_17 L55F2 TAY */
        CYC(3);             /* L55F3 LDA SXPCNT */
        if (SXPCNT != 0) {  /* L55F5 BEQ NMI_18 (not taken) */
            CYC(2);
            CYC(5);                          /* L55F7 DEC SXPCNT */
            SXPCNT = (uint8_t)(SXPCNT - 1);  /* L55F7 DEC SXPCNT */
            if (SXPCNT != 0) {               /* L55F9 BEQ NMI_18 (not taken) */
                CYC(2);
                CYC(6);                   /* L55FB TYA / L55FC ORA #$04 / L55FE TAY */
                y = (uint8_t)(y | 0x04);  /* L55FC ORA #$04: turn on shell */
            } else {
                CYC(3);  /* L55F9 BEQ taken */
            }
        } else {
            CYC(3);  /* L55F5 BEQ taken */
        }
        CYC(5);                       /* NMI_18 L55FF TYA / L5600 ORA SNDWRD */
        snd = (uint8_t)(y | SNDWRD);  /* L55FF/5600 TYA/ORA SNDWRD */
    }

    CYC(3);               /* L5602 BIT ATRACT */
    if (ATRACT & 0x80) {  /* L5604 BPL NMI_19 (not taken) */
        CYC(2);
        snd = (uint8_t)(snd | 0xA0);  /* L5606: game - enable sounds+rumble */
        CYC(2);                       /* L5606 ORA #$A0 */
        CYC(3);                       /* L5608 JMP NMI_56 */
        goto nmi_56;                  /* L5608 JMP NMI_56 */
    }
    CYC(3);  /* L5604 BPL taken */

    /* NMI_19 ($560B): attract mode. */
    CYC(3);              /* L560B BIT GOVER */
    if (GOVER & 0x80) {  /* L560D BMI NMI_54 (taken) */
        CYC(3);
        snd = (uint8_t)(snd | 0x60);  /* NMI_54 L568D: rumble off only */
        CYC(2);                       /* L568D ORA #$60 */
        CYC(3);                       /* L568F BNE NMI_56 (always taken) */
        goto nmi_56;                  /* L568F BNE NMI_56 (always) */
    }
    CYC(2);   /* L560D BMI not taken */
    CYC(2);   /* L560F LDA #$00 */
    snd = 0;  /* L560F: start with nothing */

    /* NMI_60 ($5611): slam alarm. */
    CYC(3);              /* L5611 LDX S_LMTIM */
    if (S_LMTIM != 0) {  /* L5613 BEQ NMI_65 (not taken) */
        CYC(2);
        CYC(2);                       /* L5615 ORA #$20 */
        snd = (uint8_t)(snd | 0x20);  /* L5615 ORA #$20: sound the alarm */
        CYC(3);                       /* L5617 LDX POINT */
        if (POINT == 0) {             /* L5619 BNE NMI_65 (not taken) */
            CYC(2);
            uint8_t save = snd;  /* L561B PHA */
            CYC(3);
            CYC(2);              /* L561C LDA #DISINT */
            CYC(6);              /* L561E JSR SNDON */
            sndon(K_DISINT, 0);  /* L561C/1E LDA #DISINT/JSR SNDON (X is POINT=0 here) */
            CYC(4);              /* L5621 PLA */
            snd = save;          /* L5621 PLA */
        } else {
            CYC(3);  /* L5619 BNE taken */
        }
    } else {
        CYC(3);  /* L5613 BEQ taken */
    }

    /* NMI_65 ($5622). */
    CYC(3);                       /* L5622 PHA */
    saved_snd = snd;              /* L5622 PHA */
    CYC(7);                       /* L5623 LDA S_CMODE / L5625 AND #$03 / L5627 BEQ (not taken) */
    if ((S_CMODE & 0x03) != 0) {  /* L5623-5627 AND #$03/BEQ NMI_68: not free play */
        CYC(3);               /* L5629 LDA S_S_CRDT */
        if (S_S_CRDT == 0) {  /* L562B BEQ NMI_55 (taken) */
            CYC(3);
            CYC(6);                             /* L5691 PLA / L5692 ORA #$40 */
            snd = (uint8_t)(saved_snd | 0x40);  /* NMI_55 L5691/92: PLA/ORA #$40 */
            goto nmi_56;
        }
        CYC(2);  /* L562B BEQ not taken */
    } else {
        CYC(1);  /* L5627 BEQ taken */
    }
    /* NMI_68 ($562D): flash the start button from FRAME's bit 1. */
    {
        CYC(7);  /* L562D LDA FRAME / L562F LSR / L5630 LSR */
        unsigned flash_bit = (unsigned)(FRAME >> 1) & 1u;                    /* L562D-5630: LSR,LSR carry-out = bit1 of FRAME
                                                                               * (the LSR'd A itself is discarded by the PLA below) */
        CYC(4);           /* L5631 PLA */
        snd = saved_snd;  /* L5631 PLA */
        if (flash_bit) {  /* L5632 BCC NMI_70 (not taken) */
            CYC(2);
            CYC(2);                       /* L5634 ORA #$40 */
            snd = (uint8_t)(snd | 0x40);  /* L5634 ORA #$40 */
        } else {
            CYC(3);  /* L5632 BCC taken */
        }
    }

    /* NMI_70 ($5636). */
    CYC(4);                     /* L5636 STA POTGO */
    hw_pokey_write(0x0B, snd);  /* L5636 STA POTGO */
    CYC(4);                     /* L5639 STA SOUND */
    hw_sound(snd);              /* L5639 STA SOUND: turn off sounds */
    CYC(6);                     /* L563C LDA ALLPOT / L563F AND #START */
    if (!(hw_pokey_read(0x08) & K_START))     {                                /* L5641 BEQ NMI_40 (taken):
                                                                               * FIX (2nd review): this used to be `if (START) { ... }`
                                                                               * with the game-start block inside braces and NOTHING
                                                                               * after it but the fall-through into nmi_56's hw_sound() -
                                                                               * so on every NMI where START is NOT held (i.e. nearly
                                                                               * every NMI), execution fell out of the `if` straight into
                                                                               * `nmi_56: hw_sound(snd);`, an extra hardware write the ROM
                                                                               * never does here (BEQ NMI_40 jumps straight past NMI_56,
                                                                               * skipping L5694 entirely). Flattened to an early
                                                                               * `goto nmi_40` so the common (START-not-held) path takes
                                                                               * exactly one hw_sound() call (L5639's), matching the ROM. */
        CYC(3);
        goto nmi_40;
    }
    CYC(2);  /* L5641 BEQ not taken */

    /* Game-start initialisation ($5643-568B). */
    CYC(6);         /* L5643 LDA #$FF / L5645 STA NOR2D3 */
    NOR2D3 = 0xFF;  /* L5643/45 */
    CYC(3);         /* L5648 STA ATRACT */
    ATRACT = 0xFF;  /* L5648 */
    {
        unsigned c = 1;
        CYC(12);                                        /* L564A LDA S_S_CRDT / L564C SEC / L564D SBC TRAP1 / L5650 STA */
        S_S_CRDT = sbc6502(S_S_CRDT, TRAP1, &c, NULL);  /* L564A-5650 SEC/SBC TRAP1 */
    }
    /* Integration fix (2026-09-18, NOTES_integration.md): L5652 LDA #$00 /
     * L5654 STA SKCTL was missing entirely - the comment on the next line
     * ("L5652/57") skipped straight from the LDA to S_BCCNT's own store,
     * dropping the hardware write to SKCTL (POKEY reg $0F) in between.
     * Found by lockstep on the coin_start scenario (the only scenario that
     * ever reaches this game-start path for real): NMI check #545 failed
     * with an I/O mismatch at exactly this point (C's next hw_sound(0) call
     * matched against the ROM's still-unconsumed SKCTL=0 write, address
     * $1840 vs $182F) - a real missing instruction, not a value bug. */
    CYC(6);                      /* L5652 LDA #$00 / L5654 STA SKCTL */
    hw_pokey_write(0x0F, 0x00);  /* L5652/5654 LDA #$00 / STA SKCTL */
    CYC(3);                      /* L5657 STA S_BCCNT */
    S_BCCNT = 0;                 /* L5657 (A=0 from here on) */
    CYC(4);                      /* L5659 STA LOGO */
    LOGO = 0;                    /* L5659 */
    CYC(4);                      /* L565C STA SOUND */
    hw_sound(0);                 /* L565C STA SOUND: turn off licons */
    CYC(3);                      /* L565F STA HITS */
    HITS = 0;                    /* L565F */
    CYC(3);                      /* L5661 STA HITS+1 */
    HITS_AT(1) = 0;              /* L5661 */
    CYC(3);                      /* L5663 STA HITS+3 */
    HITS_AT(3) = 0;              /* L5663 (ROM's own +3-before-+2 order) */
    CYC(3);                      /* L5665 STA HITS+2 */
    HITS_AT(2) = 0;              /* L5665 */
    CYC(3);                      /* L5667 STA ACTION */
    ACTION = 0;                  /* L5667 */
    CYC(3);                      /* L5669 STA FIRECT+2 */
    FIRECT_AT(2) = 0;            /* L5669 */
    CYC(3);                      /* L566B STA GOVER */
    GOVER = 0;                   /* L566B */
    CYC(3);                      /* L566D STA TIMOUT */
    TIMOUT = 0;                  /* L566D */
    CYC(3);                      /* L566F STA SCOLFG */
    SCOLFG = 0;                  /* L566F */
    CYC(3);                      /* L5671 STA SAUCER */
    SAUCER = 0;                  /* L5671 */
    CYC(3);                      /* L5673 STA TANGLE */
    TANGLE = 0;                  /* L5673 */
    CYC(3);                      /* L5675 STA TPOSX+1 */
    TPOSX_AT(1) = 0;             /* L5675 */
    CYC(3);                      /* L5677 STA TPOSY+1 */
    TPOSY_AT(1) = 0;             /* L5677 */
    CYC(6);                      /* L5679 LDA #$07 / L567B STA SKCTL */
    hw_pokey_write(0x0F, 0x07);  /* L5679/567B STA SKCTL: re-init POKEY */
    CYC(6);                      /* L567E JSR ROBCHK */
    robchk();                    /* L567E JSR ROBCHK */
    {
        CYC(13);                                               /* L5681 CLC / L5682 LDA OPTION / L5685 AND #$03 / L5687 ADC #$02 / L5689 STA LIVES */
        uint8_t lives = (uint8_t)((hw_option() & 0x03) + 2u);  /* L5681-5687 CLC/LDA OPTION/AND #3/ADC #2 */
        LIVES = lives;                                         /* L5689 */
        CYC((lives & 0x80) ? 2 : 3);                           /* L568B BPL NMI_40 */
        if (!(lives & 0x80))                                   /* L568B BPL NMI_40 */
            goto nmi_40;
        /* Unreachable in practice: (OPTION&3)+2 is always in [2,5], never
         * negative - kept for fidelity (NMI_54's fallthrough). */
        CYC(5);  /* L568D ORA #$60 / L568F BNE NMI_56 (always taken) */
        snd = (uint8_t)(lives | 0x60);
        /* falls into NMI_56 below (shares NMI_54's ORA #$60/BNE NMI_56 tail -
         * see NMI_54 above, which this "unreachable" branch reproduces). */
    }

    /* NMI_56 ($5694): reached only via the ATRACT/GOVER-bit7 early `goto
     * nmi_56`s above, or by falling out of the LIVES-negative branch just
     * above - NOT from the ordinary "START not held" path, which took the
     * early `goto nmi_40` and never reaches this write (see the FIX comment
     * above). */
    /* NMI_40 ($5697): buffer swap / re-arm the AVG every 6 NMIs (~24 ms). */
nmi_56:
    CYC(4);         /* NMI_56 L5694 STA SOUND */
    hw_sound(snd);  /* NMI_56 L5694 STA SOUND */
nmi_40:
    CYC(5);                          /* L5697 DEC VTIMER */
    VTIMER = (uint8_t)(VTIMER - 1);  /* L5697 DEC VTIMER */
    if (VTIMER != 0) {               /* L5699 BNE NMI_20 (taken) */
        CYC(3);
        CYC(22);  /* NMI_20 L56BD-56C2: PLA/TAY/PLA/TAX/PLA/RTI */
        return;   /* NMI_20: register restore (elided) + RTI */
    }
    CYC(2);  /* L5699 BNE not taken */

    CYC(4);             /* L569B STA VSTOP */
    hw_vstop();         /* L569B STA VSTOP */
    CYC(3);             /* L569E LDA BUFRDY */
    if (BUFRDY == 0) {  /* L56A0 BNE NMI_30 (not taken) */
        CYC(2);
        CYC(6);                                             /* L56A2 LDA VECRAM+1 / L56A5 EOR #$04 */
        uint8_t v1 = (uint8_t)(VRAM(A_VECRAM + 1) ^ 0x04);  /* L56A2/A5 LDA VECRAM+1/EOR #$04 */
        CYC(4);                                             /* L56A7 STA VECRAM+1 */
        VRAM(A_VECRAM + 1) = v1;                            /* L56A7 STA VECRAM+1 */
        CYC(2);                                             /* L56AA AND #$04 */
        if (v1 & 0x04) {                                    /* L56AC BNE NMI_25 (taken) */
            CYC(3);
            CYC(5);                                /* NMI_25 L56B2 LDA #VECRAM/$100 / NMI_26 L56B4 STA BUFRDY */
            BUFRDY = (uint8_t)(A_VECRAM / 0x100);  /* NMI_25 L56B2: lower buffer */
        } else {
            CYC(2);                                      /* L56AC BNE not taken */
            CYC(8);                                      /* L56AE LDA #upper / L56B0 BNE NMI_26 (always taken) / NMI_26 L56B4 STA BUFRDY */
            BUFRDY = (uint8_t)((A_VECRAM / 0x100) + 8);  /* L56AE: upper buffer */
        }
    } else {
        CYC(3);  /* L56A0 BNE taken */
    }
    CYC(4);            /* NMI_30 L56B6 STA GOADD */
    hw_goadd(BUFRDY);  /* NMI_30 L56B6 STA GOADD */
    CYC(5);            /* L56B9 LDA #$06 / L56BB STA VTIMER */
    VTIMER = 6;        /* L56B9/BB */

    /* L56BD-56C2: PLA/TAY/PLA/TAX/PLA/RTI - register restore, elided. */
    CYC(22);  /* NMI_20 L56BD-56C2: PLA/TAY/PLA/TAX/PLA/RTI (shared with the early return above) */
}
