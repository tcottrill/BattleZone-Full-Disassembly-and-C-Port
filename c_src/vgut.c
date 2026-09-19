/* vgut.c - VGUT ($7A19-$7AC9): vector generator utility subroutines,
 * VGUT.MAC (battlezone-main/VGUT.MAC).
 *
 * PLAN.md M1: VGUT.MAC is textually IDENTICAL (0 differing code lines) to
 * Gravitar's and Red Baron's VGUT.MAC.  This file starts from
 * C:\Source2026\Gravitar Dissasembly\c_src\vgutil.c and the Tempest port's
 * C:\Source2026\Tempest Dissasembly\c_src\alvgut.c (the same module,
 * Tempest's colour variant - reference only, its ALVGUT link addresses and
 * zero-page cells differ), re-checked statement by statement against
 * Battlezone's own listing (../disasm/bzone_program_rom.asm, $7A19-$7AC9) -
 * link addresses and zero-page cell addresses ($01 VGBRIT, $02/$03 VGLIST,
 * $04-$07 XCOMP) are Battlezone's, not Gravitar's/Tempest's.
 *
 * Every routine appends REAL AVG bytes to the open display buffer through
 * the zero-page pointer VGLIST, exactly as the ROM does (PLAN.md's "the
 * words the ROM wrote are the words we write").  A byte that would land
 * outside RAM/vector RAM is dropped by cpu_wr() (state.c increments
 * g.stray_writes instead) - the same behaviour the hardware's own decode
 * gives a write into $2800-$2FFF unused mirror space or elsewhere; this
 * ROM's own list pointer never actually strays (VGLIST always points into
 * $2000-$2FFF while a display is being built).
 *
 * Fall-through chain (../disasm/CALLING_NOTES.md section 1): VGRTSL ->
 * VGHAL1, VGHALT -> VGHAL1, VGHEXZ -> VGHEX -> VGHEX1, VGJMPL -> VGJMP1,
 * VGJSRL -> VGJMP1, VGSTA1 -> VGSTAT, VGCNTR -> VGADD2 -> VGADD, VGSCA1 ->
 * VGSCAL, VGDOT -> VGVTR -> VGVTR1 -> VGVTR2 -> VGVCTR -> VGWAI1 -> (tail
 * BNE) VGADD.  Modelled as shared static internal functions (vghal1,
 * vghex1, vgjmp1, vgwai1) called by every entry point that falls into them,
 * never by dropping a store.  The two tail-branch hazards CALLING_NOTES.md
 * flags:
 *   - VGJMP1's "INY / BNE VGADD" and VGWAI1's "STA (VGLIST),Y / BNE VGADD"
 *     are UNCONDITIONAL (their operands can never be zero: VGJMP1 always
 *     just INY'd from Y=0 to Y=1; VGWAI1's OR'd/EOR'd byte never forces the
 *     branch not to fire by construction of the caller) - VGADD's own RTS
 *     is what returns to VGJSRL/VGVCTR's ORIGINAL caller, not to
 *     VGJMP1/VGWAI1.  Modelled as an ordinary tail call (vgadd()), which
 *     gives the same observable result (same stores, same VGLIST update,
 *     same return to the C caller) without needing a fake RTS.
 *   - VGWAI1's own "BNE" tests Y (a fixed counter left over from the
 *     append), never the just-stored intensity byte - no special handling
 *     needed since the branch is unconditional either way.
 */
#include "state.h"
#include "vgut.h"
#include "hw.h"  /* CYC(): cycle charges, see TRANSLATING.md "Cycle counting" */

/* STA (VGLIST),Y - one byte into the open buffer.  A write outside RAM /
 * vector RAM is dropped by cpu_wr() (state.c: g.stray_writes++). */
static void vg_put(uint8_t y, uint8_t v)
{
    cpu_wr((uint16_t)(VGLIST_ADDR + y), v);
}

/* VGADD ($7A76): VGLIST += Y + 1.  ENTRY (Y).
 *
 * LK_MUTATE_VGADD (NOTES_m1.md mutation test): a compile-time switch that
 * deliberately advances VGLIST one byte too far (+2 instead of +1), proving
 * the lockstep verifier catches a wrong store.  Off (undefined) by default -
 * never defined by build_all.bat. */
void vgadd(uint8_t y)
{
#ifdef LK_MUTATE_VGADD
    unsigned t = (unsigned)y + 2u + VGLIST;  /* MUTATED: should be +1 */
#else
    unsigned t = (unsigned)y + 1u + VGLIST;  /* L7A76 TYA / L7A77 SEC / L7A78 ADC VGLIST */
#endif
    CYC(12);              /* L7A76-L7A7C */
    VGLIST = (uint8_t)t;  /* L7A7A STA VGLIST */
    if (t > 0xFFu) {      /* L7A7C BCC VGADD_10 */
        CYC(5);
        g.ram[A_VGLIST + 1]++;  /* L7A7E INC VGLIST+1 */
    } else CYC(1);  /* BCC taken */
    CYC(6);         /* L7A80 RTS */
}

/* VGWAI1 ($7AC7, internal): INY / STA (VGLIST),Y / BNE VGADD (always taken -
 * see the header comment on the tail-branch hazard). */
static void vgwai1(uint8_t a, uint8_t y)
{
    CYC(11);       /* L7AC7 INY / STA / BNE (taken) */
    y++;           /* L7AC7 INY */
    vg_put(y, a);  /* L7AC8 STA (VGLIST),Y */
    vgadd(y);      /* L7ACA BNE VGADD */
}

/* VGHAL1 ($7A22, internal): store A at offset 0, then again at offset 1
 * (through VGWAI1) - the RTSL/HALT word is the same byte in both halves. */
static void vghal1(uint8_t a)
{
    CYC(11);       /* L7A22-L7A26 */
    vg_put(0, a);  /* L7A22 LDY #$00 / L7A24 STA (VGLIST),Y */
    vgwai1(a, 0);  /* L7A26 JMP VGWAI1 */
}

/* VGRTSL ($7A19): append RTSL ($C0,$C0).  Dead code in this ROM
 * (CALLING_NOTES.md: not called anywhere) - translated and tested anyway. */
void vgrtsl(void)
{
    CYC(5);        /* L7A19 LDA / L7A1B BNE (taken) */
    vghal1(0xC0);  /* L7A19 LDA #$C0 / L7A1B BNE VGHAL1 */
}

/* VGHALT ($7A1D): centre the beam, then append HALT ($20,$20). */
void vghalt(void)
{
    CYC(6);        /* L7A1D JSR */
    vgcntr();      /* L7A1D JSR VGCNTR */
    CYC(2);        /* L7A20 LDA #$20 */
    vghal1(0x20);  /* L7A20 LDA #$20 (falls into VGHAL1) */
}

/* VGHEX1 ($7A34, internal): JSRL to character glyph A (an index into the
 * vector-ROM table VGMSGA - 0 is the blank glyph, n+1 the digit n); returns
 * the C it was given (the caller's zero-suppression state passes through
 * untouched - PHP/PLP around the whole body). */
static int vghex1(uint8_t a, int c)
{
    uint8_t x = (uint8_t)(a << 1);                    /* L7A34 PHP / L7A35 ASL */
    CYC(19);                                          /* L7A34-L7A3C */
    vg_put(0, cpu_rd((uint16_t)(A_VGMSGA + x)));      /* L7A36 LDY#0 / L7A38 TAX / L7A39 LDA VGMSGA,X / L7A3C STA */
    CYC(12);                                          /* L7A3E-L7A42 */
    vg_put(1, cpu_rd((uint16_t)(A_VGMSGA + x + 1)));  /* L7A3E LDA VGMSGA+1,X / L7A41 INY / L7A42 STA */
    CYC(6);                                           /* L7A44 JSR */
    vgadd(1);                                         /* L7A44 JSR VGADD */
    CYC(10);                                          /* L7A47 PLP / L7A48 RTS */
    return c;                                         /* L7A47 PLP / L7A48 RTS */
}

/* VGHEX ($7A2F): display the low nibble of A.  EXIT C clear. */
int vghex(uint8_t a)
{
    CYC(6);                         /* L7A2F-L7A32 */
    a = (uint8_t)((a & 0x0F) + 1);  /* L7A2F AND #$0F / L7A31 CLC / L7A32 ADC #$01 (clears C) */
    return vghex1(a, 0);
}

/* VGHEXZ ($7A29): digit with zero suppression.  ENTRY C set = suppress a
 * zero.  EXIT C cleared if a non-zero digit was actually displayed (a
 * suppressed zero shows the blank glyph and returns C still set). */
int vghexz(uint8_t a, int c)
{
    if (!c) { CYC(3); return vghex(a); }          /* L7A29 BCC VGHEX (taken) */
    CYC(4);                                       /* L7A29 BCC / L7A2B AND */
    a = (uint8_t)(a & 0x0F);                      /* L7A2B AND #$0F */
    if (a == 0) { CYC(3); return vghex1(0, 1); }  /* L7A2D BEQ VGHEX1 (taken) - leave C set */
    CYC(2);                                       /* L7A2D BEQ */
    return vghex(a);                              /* falls into VGHEX (its AND is idempotent) */
}

/* VGJMP1 ($7A4E, internal): store the assembled high byte (opcode+address
 * bits) at offset 1 and the low byte, rotated right through the carry the
 * caller's LSR left, at offset 0. */
static void vgjmp1(uint8_t a, uint8_t x, int carry)
{
    CYC(25);                                                 /* L7A4E-L7A58, BNE taken */
    vg_put(1, a);                                            /* L7A4E LDY #$01 / L7A50 STA (VGLIST),Y */
    vg_put(0, (uint8_t)((x >> 1) | (carry ? 0x80 : 0x00)));  /* L7A52 DEY / L7A53 TXA / L7A54 ROR / L7A55 STA */
    vgadd(1);                                                /* L7A57 INY / L7A58 BNE VGADD */
}

/* VGJMPL ($7A49): append a JMPL word aimed at CPU address A:X (MSB:LSB).
 * Dead code in this ROM (CALLING_NOTES.md: not called anywhere) - every
 * JMPL this game's display lists use is baked into ROM pictures. */
void vgjmpl(uint8_t a_msb, uint8_t x_lsb)
{
    int carry = a_msb & 1;                                /* L7A49 LSR - carry out */
    CYC(6);                                               /* L7A49-L7A4C */
    uint8_t t = (uint8_t)(((a_msb >> 1) & 0x0F) | 0xE0);  /* L7A4A AND #$0F / L7A4C ORA #$E0 */
    vgjmp1(t, x_lsb, carry);
}

/* VGJSRL ($7A5A): append a JSRL word aimed at CPU address A:X (MSB:LSB). */
void vgjsrl(uint8_t a_msb, uint8_t x_lsb)
{
    int carry = a_msb & 1;                                /* L7A5A LSR - carry out */
    CYC(9);                                               /* L7A5A-L7A5F, BNE taken */
    uint8_t t = (uint8_t)(((a_msb >> 1) & 0x0F) | 0xA0);  /* L7A5B AND #$0F / L7A5D ORA #$A0 */
    vgjmp1(t, x_lsb, carry);                              /* L7A5F BNE VGJMP1 */
}

/* VGSTA1 ($7A61): VGSTAT with Y = VGBRIT.  Dead code in this ROM
 * (CALLING_NOTES.md: every real caller already has the brightness in Y and
 * calls VGSTAT directly). */
void vgsta1(uint8_t a)
{
    CYC(3);             /* L7A61 LDY VGBRIT */
    vgstat(a, VGBRIT);  /* L7A61 LDY VGBRIT */
}

/* VGSTAT ($7A63): append a STAT word.  A = the enable/hi-low/in-out flag
 * bits, Y = the brightness/low byte. */
void vgstat(uint8_t a, uint8_t y)
{
    CYC(9);                          /* L7A63-L7A67 */
    vgadd2(y, (uint8_t)(a | 0x60));  /* L7A63 ORA #$60 / L7A65 TAX / L7A66 TYA / L7A67 JMP VGADD2 */
}

/* VGCNTR ($7A6A): append CNTR ($40,$80). */
void vgcntr(void)
{
    CYC(4);              /* L7A6A-L7A6C */
    vgadd2(0x40, 0x80);  /* L7A6A LDA #$40 / L7A6C LDX #$80 (falls into VGADD2) */
}

/* VGADD2 ($7A6E): append bytes A then X. */
void vgadd2(uint8_t a, uint8_t x)
{
    CYC(18);       /* L7A6E-L7A74 */
    vg_put(0, a);  /* L7A6E LDY #$00 / L7A70 STA (VGLIST),Y */
    vg_put(1, x);  /* L7A72 INY / L7A73 TXA / L7A74 STA (VGLIST),Y */
    vgadd(1);      /* falls into VGADD, Y=1 */
}

/* VGSCA1 ($7A81): VGSCAL with linear scale 0 (full size). */
void vgsca1(uint8_t a)
{
    CYC(2);        /* L7A81 LDY #$00 */
    vgscal(a, 0);  /* L7A81 LDY #$00 */
}

/* VGSCAL ($7A83): append a SCAL word.  A = binary scale (ORed with the
 * opcode), Y = linear scale. */
void vgscal(uint8_t a, uint8_t y)
{
    CYC(9);                          /* L7A83-L7A87 */
    vgadd2(y, (uint8_t)(a | 0x70));  /* L7A83 ORA #$70 / L7A85 TAX / L7A86 TYA / L7A87 JMP VGADD2 */
}

/* VGDOT ($7A8A): a zero-length vector (a dot) at the current beam position.
 * A = the intensity, which becomes VGBRIT. */
void vgdot(uint8_t a)
{
    CYC(6);                /* L7A8A-L7A8D */
    vgvtr(0x00, 0x00, a);  /* L7A8A TAY / L7A8B LDA #$00 / L7A8D TAX (falls into VGVTR) */
}

/* VGVTR ($7A8E): "short form" VGVCTR call.  A = delta X/4, X = delta Y/4
 * (both signed, -$80..+$7F), Y = the new brightness (VGBRIT). */
void vgvtr(uint8_t a, uint8_t x, uint8_t y)
{
    CYC(3);      /* L7A8E STY */
    VGBRIT = y;  /* L7A8E STY VGBRIT */
    vgvtr1(a, x);
}

/* VGVTR1 ($7A90): sign-extend and scale x4 both signed byte deltas into the
 * 16-bit pairs XCOMP/XCOMP+1 (X) and XCOMP+2/XCOMP+3 (Y), then fall through
 * VGVTR2 into VGVCTR to emit the actual VCTR word. */
void vgvtr1(uint8_t a, uint8_t x)
{
    uint8_t hi;

    hi = (a & 0x80) ? 0xFF : 0x00;                                /* L7A90 LDY#0 / L7A92 ASL / L7A93 BCC / L7A95 DEY */
    CYC((a & 0x80) ? 8 : 7);                                      /* L7A90-L7A95: BCC taken when positive */
    CYC(13);                                                      /* L7A96-L7A9B */
    g.ram[A_XCOMP + 1] = hi;                                      /* L7A96 STY XCOMP+1 */
    g.ram[A_XCOMP + 1] = (uint8_t)((hi << 1) | ((a >> 6) & 1u));  /* L7A98 ASL / L7A99 ROL XCOMP+1 */
    g.ram[A_XCOMP] = (uint8_t)(a << 2);                           /* L7A9B STA XCOMP */

    hi = (x & 0x80) ? 0xFF : 0x00;  /* L7A9D TXA / L7A9E ASL / L7A9F LDY#0 / L7AA1 BCC / L7AA3 DEY */
    CYC((x & 0x80) ? 10 : 9);       /* L7A9D-L7AA3: BCC taken when positive */
    CYC(13);                        /* L7AA4-L7AA9 */
    g.ram[A_XCOMP + 3] = hi;        /* L7AA4 STY XCOMP+3 */
#ifdef LK_MUTATE_VGVTR1
    g.ram[A_XCOMP + 3] = (uint8_t)((hi << 1) | 1u);  /* MUTATED: should OR in ((x>>6)&1), not a forced 1 */
#else
    g.ram[A_XCOMP + 3] = (uint8_t)((hi << 1) | ((x >> 6) & 1u));  /* L7AA6 ASL / L7AA7 ROL XCOMP+3 */
#endif
    g.ram[A_XCOMP + 2] = (uint8_t)(x << 2);  /* L7AA9 STA XCOMP+2 */

    vgvtr2();  /* falls into VGVTR2 */
}

/* VGVTR2 ($7AAB): position the beam from XCOMP - X = &XCOMP, falls into
 * VGVCTR. */
void vgvtr2(void)
{
    CYC(2);                    /* L7AAB LDX */
    vgvctr((uint8_t)A_XCOMP);  /* L7AAB LDX #XCOMP */
}

/* VGVCTR ($7AAD): append a VCTR word from four zero-page cells at X (X lsb,
 * X msb, Y msb, Y lsb - the ROM's own comment order); top 3 bits of VGBRIT
 * become the intensity.  Every caller in this ROM passes X = XCOMP (the
 * routine itself is written, and translated, generically). */
void vgvctr(uint8_t x)
{
    uint8_t a;

    CYC(50);                                               /* L7AAD-L7AC5 */
    vg_put(0, g.ram[(uint8_t)(x + 2)]);                    /* L7AAD LDY#0 / L7AAF LDA $02,X / L7AB1 STA (VGLIST),Y */
    vg_put(1, (uint8_t)(g.ram[(uint8_t)(x + 3)] & 0x1F));  /* L7AB3 LDA $03,X / L7AB5 AND #$1F / L7AB7 INY / L7AB8 STA */
    vg_put(2, g.ram[x]);                                   /* L7ABA LDA $00,X / L7ABC INY / L7ABD STA */
    a = g.ram[(uint8_t)(x + 1)];                           /* L7ABF LDA $01,X */
    /* L7AC1 EOR VGBRIT / L7AC3 AND #$1F / L7AC5 EOR VGBRIT: keep the low 5
     * bits of the X-delta's high byte, take the top 3 (the intensity) from
     * VGBRIT - a masked merge done with two EORs. */
    a = (uint8_t)(((a ^ VGBRIT) & 0x1F) ^ VGBRIT);
    vgwai1(a, 2);  /* falls into VGWAI1, Y = 2 */
}
