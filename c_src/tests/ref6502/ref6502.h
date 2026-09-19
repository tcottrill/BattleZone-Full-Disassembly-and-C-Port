/* ref6502.h - a complete NMOS 6502 interpreter, used only by refrun.c to run
 * the real Lunar Lander ROM as a differential-testing oracle for the C port.
 *
 * All 151 documented opcodes, NMOS decimal-mode ADC/SBC quirks, the
 * zero-page-indexed and (zp,X)/(zp),Y pointer wraparound, and the JMP
 * ($xxFF) indirect page bug are implemented. Cycle counting is not needed
 * (nothing here times against real hardware cycles) so cpu_step() executes
 * exactly one instruction and returns.
 *
 * Memory is reached only through the read/write callbacks, so this file has
 * no notion of ROM, RAM, or hardware registers - refrun.c supplies all of
 * that.
 */
#ifndef REF6502_H
#define REF6502_H

#include <stdint.h>

/* Status register bit masks (P). Bit 5 (FLAG_U) is not a real flip-flop on
 * the 6502; it always reads back as 1. Bit 4 (FLAG_B) is not stored either -
 * it only exists in the byte pushed to the stack by BRK/PHP (0) vs. an
 * IRQ/NMI push (1... actually cleared); see cpu_nmi() below. */
enum {
    CPU_C = 0x01,
    CPU_Z = 0x02,
    CPU_I = 0x04,
    CPU_D = 0x08,
    CPU_B = 0x10,
    CPU_U = 0x20,
    CPU_V = 0x40,
    CPU_N = 0x80
};

typedef struct cpu {
    uint16_t pc;
    uint8_t  a, x, y, s, p;

    uint8_t (*read)(uint16_t addr);
    void    (*write)(uint16_t addr, uint8_t value);
} cpu;

/* Loads PC from the reset vector ($FFFC/$FFFD) and sets a conventional
 * post-reset register state (S = $FD, P = I|U set). The Lunar Lander reset
 * routine ($7B84) sets S and P itself (LDX #$FF/TXS, CLD) before it does
 * anything that depends on them, so the exact post-reset S/P values here
 * are not load-bearing - only the PC fetch from $FFFC matters. */
void cpu_reset(cpu *c);

/* Executes exactly one instruction at c->pc, updating all registers and
 * flags and advancing c->pc past it. On an opcode outside the 151
 * documented NMOS 6502 opcodes, prints the PC and opcode to stderr and
 * calls exit(2) - this project never expects the ROM to execute one. */
void cpu_step(cpu *c);

/* Delivers one NMI: pushes PCH, PCL, then P (with the B bit clear and the
 * unused bit set, per real 6502 interrupt hardware), sets the I flag, and
 * loads PC from the NMI vector ($FFFA/$FFFB). The caller decides when an
 * NMI is due; this file has no timer of its own. */
void cpu_nmi(cpu *c);

#endif /* REF6502_H */
