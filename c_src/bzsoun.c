/* bzsoun.c - BZSOUN.MAC ($7864-$7A18): the POKEY sound-effect sequencer.
 *
 * New translation (PLAN.md: "not shared with any ported game"). Written
 * with HLL65 structured macros in Atari's source (IFNE/BEGIN/CSEND/THEN/
 * MIEND) but the disassembly (../disasm/bzone_program_rom.asm) already
 * shows the real branches, translated straight to C control flow below.
 *
 * PNTRS ($7864, 32 bytes: 8 effects x 4 channel-pointer bytes), AUDCV
 * ($7884), IDLEV ($7885) and SOUND_7886 (the envelope-record data, from
 * $7886) are all ROM bytes baked into program ROM - read through cpu_rd(),
 * never retyped (TRANSLATING.md), even though AUDCV/IDLEV currently dump as
 * $00 (they are still ROM, not RAM: nothing in this ROM can write them).
 *
 * Cycle counting (TRANSLATING.md/hw.h CYC()): SNDON ($7985) and MODSND
 * ($79B9) are real ROM subroutines (JSR'd from bzone_nmi.c, which charges
 * their JSR cost) - each charges its own body through its own RTS.  SNDON's
 * entry TSX/LDA $0103,X peek of its own just-pushed A (L798B) has no C
 * equivalent (the parameter `a` already IS that value) but still spent
 * real cycles, so it is charged anyway per TRANSLATING.md ("everything the
 * ROM executes counts").  MODSND's loop has two physically different
 * "next channel" tails (L79E8-E9 for the MODSND_15/idle exit, which needs
 * an extra JMP PMRBIL on the last channel; L7A15-16 for the MODSND_30 exit,
 * which falls straight into the shared RTS) - charged with a `ch`/`x`-keyed
 * CYC() ternary at each, per TRANSLATING.md's "cond ? a : b" allowance; see
 * NOTES_nmi_sound_coin.md "Cycle counting". */
#include "state.h"
#include "hw.h"
#include "bzsoun.h"


#define A_PNTRS       0x7864  /* 8 effects x 4 channel-pointer bytes, offset from SOUND_7886 */
#define A_AUDCV       0x7884  /* audio control channel value (1 byte) */
#define A_IDLEV       0x7885  /* idle value (1 byte) */
#define A_SOUND_7886  0x7886  /* envelope-record base: 4-byte records, STVAL/FRCNT/CHANGE/NUMBER */

/* SNDON ($7985): see bzsoun.h. */
void sndon(uint8_t a, uint8_t x)
{
    uint8_t idx;
    unsigned y;

    /* L798A-99B: TSX/LDA $0103,X peeks SNDON's own just-pushed entry A
     * (PHA/TXA/PHA/TYA/PHA puts it 3 bytes above the post-push SP; the
     * arithmetic works out to exactly the entry A regardless of caller -
     * see NOTES_nmi_sound_coin.md). Translated directly as `a`. */
    CYC(15);       /* L7985-L798A PHA/TXA/PHA/TYA/PHA/TSX: register save */
    CYC(4);        /* L798B LDA $0103,X (entry-A peek, unused by this C) */
    if (a != 0) {  /* L798E BEQ: bit-scan for a's single set bit (0-7) (not taken) */
        CYC(2);
        uint8_t v = a;
        uint8_t xx = 8;
        unsigned carry;
        CYC(2);  /* L7990 LDX #$08 */
        do {
            carry = (v & 0x80) ? 1u : 0u;  /* L7992 ASL: carry = old bit7 */
            v = (uint8_t)(v << 1);
            xx--;                /* L7993 DEX */
            CYC(carry ? 6 : 7);  /* L7992 ASL / L7993 DEX / L7994 BCC (taken while carry clear) */
        } while (!carry);  /* L7994 BCC: loop while carry clear */
        idx = xx;          /* L7996 TXA */
    } else {
        CYC(3);   /* L798E BEQ taken */
        idx = x;  /* L7996 TXA (fallback: entry X, unused by this ROM) */
    }
    CYC(2);  /* L7996 TXA (shared idx assignment) */

    CYC(8);                             /* L7997 ASL / L7998 ASL / L7999 ADC #$03 / L799B TAY */
    y = (unsigned)idx * 4u + 3u;        /* L7997-99B: ASL/ASL/ADC #$03/TAY */
    CYC(4);                             /* L799C CPY #PTLEN+7 / L799E BCS (not taken) */
    if (y < (unsigned)(K_PTLEN + 7)) {  /* L799C/9E CPY #PTLEN+7/BCS: bounds check (not taken) */
        CYC(2);  /* L79A0 LDX #$03 */
        int ch;
        for (ch = 3; ch >= 0; ch--) {  /* L79A0 LDX #$03 ... L79B1 BPL */
            CYC(4);                                       /* L79A2 LDA PNTRS,Y */
            uint8_t p = cpu_rd((uint16_t)(A_PNTRS + y));  /* L79A2 LDA PNTRS,Y */
            if (p != 0) {                                 /* L79A5 BEQ: 0 = don't touch this channel (not taken) */
                CYC(2);
                POINT_AT(ch) = p;   /* L79A7 */
                CYC(4);             /* L79A7 STA POINT,X */
                FRAMES_AT(ch) = 1;  /* L79A9/AB: dummy start */
                CYC(6);             /* L79A9 LDA #$01 / L79AB STA FRAMES,X */
                COUNT_AT(ch) = 1;   /* L79AD */
                CYC(4);             /* L79AD STA COUNT,X */
            } else {
                CYC(3);  /* L79A5 BEQ taken */
            }
            y--;                  /* L79AF DEY */
            CYC(ch > 0 ? 7 : 6);  /* L79AF DEY / L79B0 DEX / L79B1 BPL (taken while ch>0) */
        }
    } else {
        CYC(3);  /* L799E BCS taken */
    }
    /* L79B3-B8: PLA/TAY/PLA/TAX/PLA/RTS - register restore, nothing to model. */
    CYC(22);  /* L79B3-B8: PLA/TAY/PLA/TAX/PLA/RTS */
}

/* MODSND ($79B9): see bzsoun.h. */
void modsnd(void)
{
    int x;

    CYC(2);                     /* L79B9 LDX #$03 */
    for (x = 3; x >= 0; x--) {  /* MODSND: LDX #$03 ... DEX/BPL MODSND_10 */
        CYC(4);                   /* L79BB LDA FRAMES,X */
        if (FRAMES_AT(x) == 0) {  /* L79BD BEQ MODSND_15 (taken) */
            CYC(3);
            goto modsnd_15;
        }
        CYC(2);  /* L79BD BEQ not taken */

        FRAMES_AT(x) = (uint8_t)(FRAMES_AT(x) - 1);  /* L79BF DEC FRAMES,X */
        CYC(6);                                      /* L79BF DEC FRAMES,X */
        if (FRAMES_AT(x) != 0) {                     /* L79C1 BNE MODSND_30 (taken) */
            CYC(3);
            goto modsnd_30;
        }
        CYC(2);  /* L79C1 BNE not taken */

        {
            uint8_t y = POINT_AT(x);  /* L79C3 LDY POINT,X */
            CYC(4);                   /* L79C3 LDY POINT,X */
            if (y == 0) {             /* L79C5 BEQ MODSND_30 (taken): channel not active */
                CYC(3);
                goto modsnd_30;
            }
            CYC(2);  /* L79C5 BEQ not taken */

            COUNT_AT(x) = (uint8_t)(COUNT_AT(x) - 1);  /* L79C7 DEC COUNT,X */
            CYC(6);                                    /* L79C7 DEC COUNT,X */
            if (COUNT_AT(x) != 0) {                    /* L79C9 BNE MODSND_20 (taken) */
                CYC(3);
                /* MODSND_20 ($79FD): step within the current record. */
                FRAMES_AT(x) = cpu_rd((uint16_t)(A_SOUND_7886 + K_FRCNT - 4 + y));  /* L79FD/L7A00 */
                CYC(8);  /* L79FD LDA / L7A00 STA FRAMES,X */
                CURRENT_AT(x) = (uint8_t)(CURRENT_AT(x) +
                    cpu_rd((uint16_t)(A_SOUND_7886 + K_CHANGE - 4 + y)));  /* L7A02-08 */
                CYC(14);  /* L7A02 LDA / L7A04 CLC / L7A05 ADC / L7A08 STA */
                goto modsnd_30;
            }
            CYC(2);  /* L79C9 BNE not taken */

            /* COUNT just hit 0: load the next 4-byte record. */
            CURRENT_AT(x) = cpu_rd((uint16_t)(A_SOUND_7886 + K_STVAL + y));  /* L79CB/CE */
            CYC(8);                                                          /* L79CB LDA / L79CE STA CURRENT,X */
            FRAMES_AT(x) = cpu_rd((uint16_t)(A_SOUND_7886 + K_FRCNT + y));   /* L79D0/D3 */
            CYC(8);                                                          /* L79D0 LDA / L79D3 STA FRAMES,X */
            if (FRAMES_AT(x) != 0) {                                         /* L79D5 BNE MODSND_17 (taken) */
                CYC(3);
                /* MODSND_17 ($79EE) */
                COUNT_AT(x) = cpu_rd((uint16_t)(A_SOUND_7886 + K_NUMBER + y));  /* L79EE/F1 */
                CYC(8);                                                         /* L79EE LDA / L79F1 STA COUNT,X */
                POINT_AT(x) = (uint8_t)(POINT_AT(x) + 4);                       /* L79F3-F8 */
                CYC(12);                                                        /* L79F3 LDA / L79F5 CLC / L79F6 ADC / L79F8 STA */
                CYC(3);                                                         /* L79FA JMP MODSND_30 */
                goto modsnd_30;
            }
            CYC(2);           /* L79D5 BNE not taken */
            POINT_AT(x) = 0;  /* L79D7: idle this channel */
            CYC(4);           /* L79D7 STA POINT,X */
            CYC(2);           /* L79D9 TXA */
            if (x != 0) {     /* L79D9/DA TXA/BNE MODSND_30 (taken) */
                CYC(3);
                goto modsnd_30;
            }
            CYC(2);  /* L79DA BNE not taken */
            /* x == 0 ("channel 1"): fall into MODSND_15 - idling channel 0
             * also forces the idle-output tail (no AUDCTL write this pass,
             * see below) for it, ending all sound. */
        }

    modsnd_15:  /* MODSND_15 ($79DC) */
        CURRENT_AT(x) = cpu_rd(A_IDLEV);            /* L79DC/DF LDA IDLEV/STA CURRENT,X */
        CYC(8);                                     /* L79DC LDA IDLEV / L79DF STA CURRENT,X */
        hw_pokey_write((uint8_t)x, CURRENT_AT(x));  /* L79E1 STA AUDF1,X */
        CYC(5);                                     /* L79E1 STA AUDF1,X */
        POINT_AT(x) = 0;                            /* L79E4/E6 */
        CYC(6);                                     /* L79E4 LDA #$00 / L79E6 STA POINT,X */
        CYC(x > 0 ? 5 : 7);                         /* L79E8 DEX / L79E9 BPL MODSND_10 [+ L79EB JMP PMRBIL on the last channel] */
        continue;                                             /* DEX/BPL MODSND_10 (loop) - note: NO AUDCTL write
                                                                 * this iteration, unlike MODSND_30 below. */

    modsnd_30:  /* MODSND_30 ($7A0A) */
        hw_pokey_write((uint8_t)x, CURRENT_AT(x));  /* L7A0A/0C LDA CURRENT,X/STA AUDF1,X */
        CYC(9);                                     /* L7A0A LDA CURRENT,X / L7A0C STA AUDF1,X */
        hw_pokey_write(0x08, cpu_rd(A_AUDCV));      /* L7A0F/12 LDA AUDCV/STA AUDCTL */
        CYC(8);                                     /* L7A0F LDA AUDCV / L7A12 STA AUDCTL */
        CYC(x > 0 ? 5 : 4);                         /* L7A15 DEX / L7A16 BPL MODSND_10 */
    }
    /* PMRBIL ($7A18): RTS. */
    CYC(6);  /* L7A18 RTS */
}
