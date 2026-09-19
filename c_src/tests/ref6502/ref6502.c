/* ref6502.c - NMOS 6502 interpreter. See ref6502.h for the contract. */
#include <stdio.h>
#include <stdlib.h>

#include "ref6502.h"

/* ---------------------------------------------------------------- flags */

static void set_flag(cpu *c, uint8_t mask, int cond)
{
    if (cond)
        c->p = (uint8_t)(c->p | mask);
    else
        c->p = (uint8_t)(c->p & ~mask);
}

static void set_zn(cpu *c, uint8_t v)
{
    set_flag(c, CPU_Z, v == 0);
    set_flag(c, CPU_N, (v & 0x80) != 0);
}

/* ---------------------------------------------------------------- stack */

static void push8(cpu *c, uint8_t v)
{
    c->write((uint16_t)(0x0100 + c->s), v);
    c->s = (uint8_t)(c->s - 1);
}

static uint8_t pop8(cpu *c)
{
    c->s = (uint8_t)(c->s + 1);
    return c->read((uint16_t)(0x0100 + c->s));
}

static void push16(cpu *c, uint16_t v)
{
    push8(c, (uint8_t)(v >> 8));
    push8(c, (uint8_t)(v & 0xFF));
}

static uint16_t pop16(cpu *c)
{
    uint16_t lo = pop8(c);
    uint16_t hi = pop8(c);
    return (uint16_t)(lo | (hi << 8));
}

/* -------------------------------------------------------------- fetch */

static uint8_t fetch8(cpu *c)
{
    uint8_t v = c->read(c->pc);
    c->pc = (uint16_t)(c->pc + 1);
    return v;
}

static uint16_t fetch16(cpu *c)
{
    uint16_t lo = fetch8(c);
    uint16_t hi = fetch8(c);
    return (uint16_t)(lo | (hi << 8));
}

/* --------------------------------------------------------- addressing */
/* Each returns the effective address for a memory operand and advances
 * c->pc past the instruction's operand bytes. Zero-page-indexed and
 * (zp,X)/(zp),Y pointer fetches wrap within page zero, matching real NMOS
 * 6502 behaviour (the pointer bytes never cross into page 1). */

static uint16_t addr_zp(cpu *c)  { return fetch8(c); }
static uint16_t addr_zpx(cpu *c) { return (uint8_t)(fetch8(c) + c->x); }
static uint16_t addr_zpy(cpu *c) { return (uint8_t)(fetch8(c) + c->y); }
static uint16_t addr_abs(cpu *c) { return fetch16(c); }
static uint16_t addr_absx(cpu *c) { return (uint16_t)(fetch16(c) + c->x); }
static uint16_t addr_absy(cpu *c) { return (uint16_t)(fetch16(c) + c->y); }

static uint16_t addr_indx(cpu *c)
{
    uint8_t zp = (uint8_t)(fetch8(c) + c->x);
    uint8_t lo = c->read(zp);
    uint8_t hi = c->read((uint8_t)(zp + 1));
    return (uint16_t)(lo | (hi << 8));
}

static uint16_t addr_indy(cpu *c)
{
    uint8_t zp = fetch8(c);
    uint8_t lo = c->read(zp);
    uint8_t hi = c->read((uint8_t)(zp + 1));
    uint16_t base = (uint16_t)(lo | (hi << 8));
    return (uint16_t)(base + c->y);
}

/* JMP ($xxxx): the NMOS page bug. If the pointer's low byte is $FF, the
 * high byte is fetched from the start of the same page, not the next one. */
static uint16_t addr_ind_jmp(cpu *c)
{
    uint16_t ptr = fetch16(c);
    uint8_t lo = c->read(ptr);
    uint8_t hi = c->read((uint16_t)((ptr & 0xFF00) | ((ptr + 1) & 0x00FF)));
    return (uint16_t)(lo | (hi << 8));
}

static void branch(cpu *c, int taken)
{
    int8_t off = (int8_t)fetch8(c);
    if (taken)
        c->pc = (uint16_t)(c->pc + off);
}

/* -------------------------------------------------------- ALU helpers */

/* ADC. Binary mode is the ordinary add-with-carry. Decimal mode follows the
 * documented NMOS behaviour: the accumulator gets the BCD-corrected result,
 * but the N and V flags come from the intermediate binary-ish sum (low
 * nibble corrected, high nibble not yet), and Z comes from the plain binary
 * sum as if D were clear - both are well-known NMOS quirks, not bugs in
 * this interpreter. */
static void op_adc(cpu *c, uint8_t v)
{
    uint8_t a0 = c->a;
    unsigned carry_in = (c->p & CPU_C) ? 1u : 0u;

    if (c->p & CPU_D) {
        unsigned al = (a0 & 0x0Fu) + (v & 0x0Fu) + carry_in;
        if (al >= 0x0A)
            al = ((al + 0x06) & 0x0F) + 0x10;
        unsigned t = (a0 & 0xF0u) + (v & 0xF0u) + al;
        uint8_t t_byte = (uint8_t)t;
        unsigned bin_sum = (unsigned)a0 + v + carry_in;

        set_flag(c, CPU_Z, (uint8_t)bin_sum == 0);
        set_flag(c, CPU_N, (t_byte & 0x80) != 0);
        set_flag(c, CPU_V, ((~(a0 ^ v)) & (a0 ^ t_byte) & 0x80) != 0);
        if (t >= 0xA0)
            t += 0x60;
        set_flag(c, CPU_C, t >= 0x100);
        c->a = (uint8_t)t;
    } else {
        unsigned sum = (unsigned)a0 + v + carry_in;
        uint8_t result = (uint8_t)sum;
        set_flag(c, CPU_C, sum > 0xFF);
        set_flag(c, CPU_V, ((~(a0 ^ v)) & (a0 ^ result) & 0x80) != 0);
        c->a = result;
        set_zn(c, c->a);
    }
}

/* SBC. On NMOS, the flags (C, N, V, Z) are ALWAYS the binary-subtraction
 * result, in both binary and decimal mode; only the accumulator's stored
 * value gets the BCD correction in decimal mode. */
static void op_sbc(cpu *c, uint8_t v)
{
    uint8_t a0 = c->a;
    int carry_in = (c->p & CPU_C) ? 1 : 0;
    int bin = (int)a0 - (int)v - (1 - carry_in);
    uint8_t bin_result = (uint8_t)bin;

    set_flag(c, CPU_C, bin >= 0);
    set_flag(c, CPU_V, ((a0 ^ v) & (a0 ^ bin_result) & 0x80) != 0);
    set_zn(c, bin_result);

    if (c->p & CPU_D) {
        int al = (int)(a0 & 0x0F) - (int)(v & 0x0F) - (1 - carry_in);
        if (al < 0)
            al = ((al - 0x06) & 0x0F) - 0x10;
        int t = (int)(a0 & 0xF0) - (int)(v & 0xF0) + al;
        if (t < 0)
            t -= 0x60;
        c->a = (uint8_t)(t & 0xFF);
    } else {
        c->a = bin_result;
    }
}

static uint8_t do_asl(cpu *c, uint8_t v)
{
    set_flag(c, CPU_C, (v & 0x80) != 0);
    v = (uint8_t)(v << 1);
    set_zn(c, v);
    return v;
}

static uint8_t do_lsr(cpu *c, uint8_t v)
{
    set_flag(c, CPU_C, (v & 0x01) != 0);
    v = (uint8_t)(v >> 1);
    set_zn(c, v);
    return v;
}

static uint8_t do_rol(cpu *c, uint8_t v)
{
    int carry_in = (c->p & CPU_C) ? 1 : 0;
    set_flag(c, CPU_C, (v & 0x80) != 0);
    v = (uint8_t)((v << 1) | carry_in);
    set_zn(c, v);
    return v;
}

static uint8_t do_ror(cpu *c, uint8_t v)
{
    int carry_in = (c->p & CPU_C) ? 1 : 0;
    set_flag(c, CPU_C, (v & 0x01) != 0);
    v = (uint8_t)((v >> 1) | (carry_in << 7));
    set_zn(c, v);
    return v;
}

static void do_cmp(cpu *c, uint8_t reg, uint8_t v)
{
    set_flag(c, CPU_C, reg >= v);
    set_zn(c, (uint8_t)(reg - v));
}

static void do_bit(cpu *c, uint8_t v)
{
    set_flag(c, CPU_Z, (c->a & v) == 0);
    set_flag(c, CPU_N, (v & 0x80) != 0);
    set_flag(c, CPU_V, (v & 0x40) != 0);
}

/* -------------------------------------------------------------- public */

void cpu_reset(cpu *c)
{
    c->s = 0xFD;
    c->p = (uint8_t)(CPU_U | CPU_I);
    c->a = c->x = c->y = 0;
    c->pc = (uint16_t)(c->read(0xFFFC) | (c->read(0xFFFD) << 8));
}

void cpu_nmi(cpu *c)
{
    push16(c, c->pc);
    push8(c, (uint8_t)((c->p & (uint8_t)~CPU_B) | CPU_U));
    c->p = (uint8_t)(c->p | CPU_I);
    c->pc = (uint16_t)(c->read(0xFFFA) | (c->read(0xFFFB) << 8));
}

void cpu_step(cpu *c)
{
    uint16_t instr_pc = c->pc;
    uint8_t op = fetch8(c);
    uint16_t a;
    uint8_t v;

    switch (op) {
    /* ---- ORA ---- */
    case 0x01: c->a |= c->read(addr_indx(c)); set_zn(c, c->a); break;
    case 0x05: c->a |= c->read(addr_zp(c));   set_zn(c, c->a); break;
    case 0x09: c->a |= fetch8(c);             set_zn(c, c->a); break;
    case 0x0D: c->a |= c->read(addr_abs(c));  set_zn(c, c->a); break;
    case 0x11: c->a |= c->read(addr_indy(c)); set_zn(c, c->a); break;
    case 0x15: c->a |= c->read(addr_zpx(c));  set_zn(c, c->a); break;
    case 0x19: c->a |= c->read(addr_absy(c)); set_zn(c, c->a); break;
    case 0x1D: c->a |= c->read(addr_absx(c)); set_zn(c, c->a); break;

    /* ---- AND ---- */
    case 0x21: c->a &= c->read(addr_indx(c)); set_zn(c, c->a); break;
    case 0x25: c->a &= c->read(addr_zp(c));   set_zn(c, c->a); break;
    case 0x29: c->a &= fetch8(c);             set_zn(c, c->a); break;
    case 0x2D: c->a &= c->read(addr_abs(c));  set_zn(c, c->a); break;
    case 0x31: c->a &= c->read(addr_indy(c)); set_zn(c, c->a); break;
    case 0x35: c->a &= c->read(addr_zpx(c));  set_zn(c, c->a); break;
    case 0x39: c->a &= c->read(addr_absy(c)); set_zn(c, c->a); break;
    case 0x3D: c->a &= c->read(addr_absx(c)); set_zn(c, c->a); break;

    /* ---- EOR ---- */
    case 0x41: c->a ^= c->read(addr_indx(c)); set_zn(c, c->a); break;
    case 0x45: c->a ^= c->read(addr_zp(c));   set_zn(c, c->a); break;
    case 0x49: c->a ^= fetch8(c);             set_zn(c, c->a); break;
    case 0x4D: c->a ^= c->read(addr_abs(c));  set_zn(c, c->a); break;
    case 0x51: c->a ^= c->read(addr_indy(c)); set_zn(c, c->a); break;
    case 0x55: c->a ^= c->read(addr_zpx(c));  set_zn(c, c->a); break;
    case 0x59: c->a ^= c->read(addr_absy(c)); set_zn(c, c->a); break;
    case 0x5D: c->a ^= c->read(addr_absx(c)); set_zn(c, c->a); break;

    /* ---- ADC ---- */
    case 0x61: op_adc(c, c->read(addr_indx(c))); break;
    case 0x65: op_adc(c, c->read(addr_zp(c)));   break;
    case 0x69: op_adc(c, fetch8(c));             break;
    case 0x6D: op_adc(c, c->read(addr_abs(c)));  break;
    case 0x71: op_adc(c, c->read(addr_indy(c))); break;
    case 0x75: op_adc(c, c->read(addr_zpx(c)));  break;
    case 0x79: op_adc(c, c->read(addr_absy(c))); break;
    case 0x7D: op_adc(c, c->read(addr_absx(c))); break;

    /* ---- SBC ---- */
    case 0xE1: op_sbc(c, c->read(addr_indx(c))); break;
    case 0xE5: op_sbc(c, c->read(addr_zp(c)));   break;
    case 0xE9: op_sbc(c, fetch8(c));             break;
    case 0xED: op_sbc(c, c->read(addr_abs(c)));  break;
    case 0xF1: op_sbc(c, c->read(addr_indy(c))); break;
    case 0xF5: op_sbc(c, c->read(addr_zpx(c)));  break;
    case 0xF9: op_sbc(c, c->read(addr_absy(c))); break;
    case 0xFD: op_sbc(c, c->read(addr_absx(c))); break;

    /* ---- CMP ---- */
    case 0xC1: do_cmp(c, c->a, c->read(addr_indx(c))); break;
    case 0xC5: do_cmp(c, c->a, c->read(addr_zp(c)));   break;
    case 0xC9: do_cmp(c, c->a, fetch8(c));             break;
    case 0xCD: do_cmp(c, c->a, c->read(addr_abs(c)));  break;
    case 0xD1: do_cmp(c, c->a, c->read(addr_indy(c))); break;
    case 0xD5: do_cmp(c, c->a, c->read(addr_zpx(c)));  break;
    case 0xD9: do_cmp(c, c->a, c->read(addr_absy(c))); break;
    case 0xDD: do_cmp(c, c->a, c->read(addr_absx(c))); break;

    /* ---- CPX / CPY ---- */
    case 0xE0: do_cmp(c, c->x, fetch8(c));            break;
    case 0xE4: do_cmp(c, c->x, c->read(addr_zp(c)));  break;
    case 0xEC: do_cmp(c, c->x, c->read(addr_abs(c))); break;
    case 0xC0: do_cmp(c, c->y, fetch8(c));            break;
    case 0xC4: do_cmp(c, c->y, c->read(addr_zp(c)));  break;
    case 0xCC: do_cmp(c, c->y, c->read(addr_abs(c))); break;

    /* ---- BIT ---- */
    case 0x24: do_bit(c, c->read(addr_zp(c)));  break;
    case 0x2C: do_bit(c, c->read(addr_abs(c))); break;

    /* ---- ASL ---- */
    case 0x0A: c->a = do_asl(c, c->a); break;
    case 0x06: a = addr_zp(c);   c->write(a, do_asl(c, c->read(a))); break;
    case 0x16: a = addr_zpx(c);  c->write(a, do_asl(c, c->read(a))); break;
    case 0x0E: a = addr_abs(c);  c->write(a, do_asl(c, c->read(a))); break;
    case 0x1E: a = addr_absx(c); c->write(a, do_asl(c, c->read(a))); break;

    /* ---- LSR ---- */
    case 0x4A: c->a = do_lsr(c, c->a); break;
    case 0x46: a = addr_zp(c);   c->write(a, do_lsr(c, c->read(a))); break;
    case 0x56: a = addr_zpx(c);  c->write(a, do_lsr(c, c->read(a))); break;
    case 0x4E: a = addr_abs(c);  c->write(a, do_lsr(c, c->read(a))); break;
    case 0x5E: a = addr_absx(c); c->write(a, do_lsr(c, c->read(a))); break;

    /* ---- ROL ---- */
    case 0x2A: c->a = do_rol(c, c->a); break;
    case 0x26: a = addr_zp(c);   c->write(a, do_rol(c, c->read(a))); break;
    case 0x36: a = addr_zpx(c);  c->write(a, do_rol(c, c->read(a))); break;
    case 0x2E: a = addr_abs(c);  c->write(a, do_rol(c, c->read(a))); break;
    case 0x3E: a = addr_absx(c); c->write(a, do_rol(c, c->read(a))); break;

    /* ---- ROR ---- */
    case 0x6A: c->a = do_ror(c, c->a); break;
    case 0x66: a = addr_zp(c);   c->write(a, do_ror(c, c->read(a))); break;
    case 0x76: a = addr_zpx(c);  c->write(a, do_ror(c, c->read(a))); break;
    case 0x6E: a = addr_abs(c);  c->write(a, do_ror(c, c->read(a))); break;
    case 0x7E: a = addr_absx(c); c->write(a, do_ror(c, c->read(a))); break;

    /* ---- INC / DEC ---- */
    case 0xE6: a = addr_zp(c);   c->write(a, v = (uint8_t)(c->read(a) + 1)); set_zn(c, v); break;
    case 0xF6: a = addr_zpx(c);  c->write(a, v = (uint8_t)(c->read(a) + 1)); set_zn(c, v); break;
    case 0xEE: a = addr_abs(c);  c->write(a, v = (uint8_t)(c->read(a) + 1)); set_zn(c, v); break;
    case 0xFE: a = addr_absx(c); c->write(a, v = (uint8_t)(c->read(a) + 1)); set_zn(c, v); break;
    case 0xC6: a = addr_zp(c);   c->write(a, v = (uint8_t)(c->read(a) - 1)); set_zn(c, v); break;
    case 0xD6: a = addr_zpx(c);  c->write(a, v = (uint8_t)(c->read(a) - 1)); set_zn(c, v); break;
    case 0xCE: a = addr_abs(c);  c->write(a, v = (uint8_t)(c->read(a) - 1)); set_zn(c, v); break;
    case 0xDE: a = addr_absx(c); c->write(a, v = (uint8_t)(c->read(a) - 1)); set_zn(c, v); break;
    case 0xE8: c->x = (uint8_t)(c->x + 1); set_zn(c, c->x); break;
    case 0xC8: c->y = (uint8_t)(c->y + 1); set_zn(c, c->y); break;
    case 0xCA: c->x = (uint8_t)(c->x - 1); set_zn(c, c->x); break;
    case 0x88: c->y = (uint8_t)(c->y - 1); set_zn(c, c->y); break;

    /* ---- LDA / LDX / LDY ---- */
    case 0xA1: c->a = c->read(addr_indx(c)); set_zn(c, c->a); break;
    case 0xA5: c->a = c->read(addr_zp(c));   set_zn(c, c->a); break;
    case 0xA9: c->a = fetch8(c);             set_zn(c, c->a); break;
    case 0xAD: c->a = c->read(addr_abs(c));  set_zn(c, c->a); break;
    case 0xB1: c->a = c->read(addr_indy(c)); set_zn(c, c->a); break;
    case 0xB5: c->a = c->read(addr_zpx(c));  set_zn(c, c->a); break;
    case 0xB9: c->a = c->read(addr_absy(c)); set_zn(c, c->a); break;
    case 0xBD: c->a = c->read(addr_absx(c)); set_zn(c, c->a); break;
    case 0xA2: c->x = fetch8(c);             set_zn(c, c->x); break;
    case 0xA6: c->x = c->read(addr_zp(c));   set_zn(c, c->x); break;
    case 0xAE: c->x = c->read(addr_abs(c));  set_zn(c, c->x); break;
    case 0xB6: c->x = c->read(addr_zpy(c));  set_zn(c, c->x); break;
    case 0xBE: c->x = c->read(addr_absy(c)); set_zn(c, c->x); break;
    case 0xA0: c->y = fetch8(c);             set_zn(c, c->y); break;
    case 0xA4: c->y = c->read(addr_zp(c));   set_zn(c, c->y); break;
    case 0xAC: c->y = c->read(addr_abs(c));  set_zn(c, c->y); break;
    case 0xB4: c->y = c->read(addr_zpx(c));  set_zn(c, c->y); break;
    case 0xBC: c->y = c->read(addr_absx(c)); set_zn(c, c->y); break;

    /* ---- STA / STX / STY ---- */
    case 0x81: c->write(addr_indx(c), c->a); break;
    case 0x85: c->write(addr_zp(c), c->a);   break;
    case 0x8D: c->write(addr_abs(c), c->a);  break;
    case 0x91: c->write(addr_indy(c), c->a); break;
    case 0x95: c->write(addr_zpx(c), c->a);  break;
    case 0x99: c->write(addr_absy(c), c->a); break;
    case 0x9D: c->write(addr_absx(c), c->a); break;
    case 0x86: c->write(addr_zp(c), c->x);   break;
    case 0x8E: c->write(addr_abs(c), c->x);  break;
    case 0x96: c->write(addr_zpy(c), c->x);  break;
    case 0x84: c->write(addr_zp(c), c->y);   break;
    case 0x8C: c->write(addr_abs(c), c->y);  break;
    case 0x94: c->write(addr_zpx(c), c->y);  break;

    /* ---- transfers ---- */
    case 0xAA: c->x = c->a; set_zn(c, c->x); break;             /* TAX */
    case 0x8A: c->a = c->x; set_zn(c, c->a); break;             /* TXA */
    case 0xA8: c->y = c->a; set_zn(c, c->y); break;             /* TAY */
    case 0x98: c->a = c->y; set_zn(c, c->a); break;             /* TYA */
    case 0xBA: c->x = c->s; set_zn(c, c->x); break;             /* TSX */
    case 0x9A: c->s = c->x; break;                              /* TXS (no flags) */

    /* ---- stack ---- */
    case 0x48: push8(c, c->a); break;                           /* PHA */
    case 0x68: c->a = pop8(c); set_zn(c, c->a); break;          /* PLA */
    case 0x08: push8(c, (uint8_t)(c->p | CPU_B | CPU_U)); break; /* PHP */
    case 0x28: c->p = (uint8_t)(pop8(c) | CPU_U); break;        /* PLP */

    /* ---- flags ---- */
    case 0x18: set_flag(c, CPU_C, 0); break;                    /* CLC */
    case 0x38: set_flag(c, CPU_C, 1); break;                    /* SEC */
    case 0x58: set_flag(c, CPU_I, 0); break;                    /* CLI */
    case 0x78: set_flag(c, CPU_I, 1); break;                    /* SEI */
    case 0xB8: set_flag(c, CPU_V, 0); break;                    /* CLV */
    case 0xD8: set_flag(c, CPU_D, 0); break;                    /* CLD */
    case 0xF8: set_flag(c, CPU_D, 1); break;                    /* SED */

    /* ---- branches ---- */
    case 0x10: branch(c, !(c->p & CPU_N)); break;               /* BPL */
    case 0x30: branch(c, (c->p & CPU_N) != 0); break;           /* BMI */
    case 0x50: branch(c, !(c->p & CPU_V)); break;               /* BVC */
    case 0x70: branch(c, (c->p & CPU_V) != 0); break;           /* BVS */
    case 0x90: branch(c, !(c->p & CPU_C)); break;               /* BCC */
    case 0xB0: branch(c, (c->p & CPU_C) != 0); break;           /* BCS */
    case 0xD0: branch(c, !(c->p & CPU_Z)); break;               /* BNE */
    case 0xF0: branch(c, (c->p & CPU_Z) != 0); break;           /* BEQ */

    /* ---- jumps / subroutines ---- */
    case 0x4C: c->pc = addr_abs(c); break;                      /* JMP abs */
    case 0x6C: c->pc = addr_ind_jmp(c); break;                  /* JMP (ind) */
    case 0x20: {                                                /* JSR abs */
        uint16_t target = addr_abs(c);
        push16(c, (uint16_t)(c->pc - 1));
        c->pc = target;
        break;
    }
    case 0x60: c->pc = (uint16_t)(pop16(c) + 1); break;         /* RTS */
    case 0x40: {                                                /* RTI */
        c->p = (uint8_t)(pop8(c) | CPU_U);
        c->pc = pop16(c);
        break;
    }
    case 0x00: {                                                /* BRK */
        c->pc = (uint16_t)(c->pc + 1);
        push16(c, c->pc);
        push8(c, (uint8_t)(c->p | CPU_B | CPU_U));
        set_flag(c, CPU_I, 1);
        c->pc = (uint16_t)(c->read(0xFFFE) | (c->read(0xFFFF) << 8));
        break;
    }

    /* ---- no-op ---- */
    case 0xEA: break;                                           /* NOP */

    default:
        fprintf(stderr, "ref6502: undocumented opcode $%02X at $%04X\n", op, instr_pc);
        exit(2);
    }
}
