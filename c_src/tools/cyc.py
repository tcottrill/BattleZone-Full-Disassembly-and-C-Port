"""cyc.py - 6502 cycle costs of ROM instructions, for CYC() annotations.

    python tools\\cyc.py 7A76-7A7E [6353 ...]

For each address range (inclusive, hex, no $) walks the program ROM
(progrom.c, $5000-$7FFF) instruction by instruction from the first address
and prints address, opcode, mnemonic, base cycles, and a running total.  A
conditional branch is shown at its not-taken cost (2); add 1 on the path that
takes it.  Same cycle model as tests\\refrun.c / tests\\lockstep.c (their
base_cycles[] table): NMOS base cycles, +1 for a taken branch, no page-cross
penalty.  See TRANSLATING.md "Cycle counting".
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# base_cycles[] from tests\lockstep.c / tests\refrun.c (keep identical)
CYC = [
    7,6,0,0,0,3,5,0,3,2,2,0,0,4,6,0,
    2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
    6,6,0,0,3,3,5,0,4,2,2,0,4,4,6,0,
    2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
    6,6,0,0,0,3,5,0,3,2,2,0,3,4,6,0,
    2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
    6,6,0,0,0,3,5,0,4,2,2,0,5,4,6,0,
    2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
    0,6,0,0,3,3,3,0,2,0,2,0,4,4,4,0,
    2,6,0,0,4,4,4,0,2,5,2,0,0,5,0,0,
    2,6,2,0,3,3,3,0,2,2,2,0,4,4,4,0,
    2,5,0,0,4,4,4,0,2,4,2,0,4,4,4,0,
    2,6,0,0,3,3,5,0,2,2,2,0,4,4,6,0,
    2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
    2,6,0,0,3,3,5,0,2,2,2,0,4,4,6,0,
    2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
]

NAMES = "BRK ORA ??? ??? ??? ORA ASL ??? PHP ORA ASL ??? ??? ORA ASL ??? BPL ORA ??? ??? ??? ORA ASL ??? CLC ORA ??? ??? ??? ORA ASL ??? " \
        "JSR AND ??? ??? BIT AND ROL ??? PLP AND ROL ??? BIT AND ROL ??? BMI AND ??? ??? ??? AND ROL ??? SEC AND ??? ??? ??? AND ROL ??? " \
        "RTI EOR ??? ??? ??? EOR LSR ??? PHA EOR LSR ??? JMP EOR LSR ??? BVC EOR ??? ??? ??? EOR LSR ??? CLI EOR ??? ??? ??? EOR LSR ??? " \
        "RTS ADC ??? ??? ??? ADC ROR ??? PLA ADC ROR ??? JMP ADC ROR ??? BVS ADC ??? ??? ??? ADC ROR ??? SEI ADC ??? ??? ??? ADC ROR ??? " \
        "??? STA ??? ??? STY STA STX ??? DEY ??? TXA ??? STY STA STX ??? BCC STA ??? ??? STY STA STX ??? TYA STA TXS ??? ??? STA ??? ??? " \
        "LDY LDA LDX ??? LDY LDA LDX ??? TAY LDA TAX ??? LDY LDA LDX ??? BCS LDA ??? ??? LDY LDA LDX ??? CLV LDA TSX ??? LDY LDA LDX ??? " \
        "CPY CMP ??? ??? CPY CMP DEC ??? INY CMP DEX ??? CPY CMP DEC ??? BNE CMP ??? ??? ??? CMP DEC ??? CLD CMP ??? ??? ??? CMP DEC ??? " \
        "CPX SBC ??? ??? CPX SBC INC ??? INX SBC NOP ??? CPX SBC INC ??? BEQ SBC ??? ??? ??? SBC INC ??? SED SBC ??? ??? ??? SBC INC ???".split()


def length(op):
    lo = op & 0x1F
    if op in (0x20, 0x2C, 0x4C, 0x6C) or (op & 0x0C) == 0x0C or lo in (0x19, 0x1B, 0x1D, 0x1E, 0x1F) or op == 0xBE:
        return 3
    if op in (0x00, 0x40, 0x60) or (op & 0x0F) in (0x08, 0x0A) or op in (0x8A, 0x9A, 0xAA, 0xBA, 0xCA, 0xEA):
        return 1
    return 2


def load_rom():
    text = open(os.path.join(HERE, "..", "progrom.c")).read()
    body = text[text.index("{") + 1:text.rindex("}")]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    rom = bytes(int(t, 16) for t in re.findall(r"0x([0-9A-Fa-f]{2})", body))
    assert len(rom) == 0x3000, len(rom)
    return rom


def main():
    rom = load_rom()
    grand = 0
    for arg in sys.argv[1:]:
        lo, _, hi = arg.partition("-")
        a, end = int(lo, 16), int(hi or lo, 16)
        total = 0
        while a <= end:
            op = rom[a - 0x5000]
            n = length(op)
            c = CYC[op]
            total += c
            note = "  (+1 if taken)" if (op & 0x1F) == 0x10 else ""
            print("  L%04X  %02X %-3s  %d  sum %3d%s" % (a, op, NAMES[op], c, total, note))
            a += n
        print("L%s: %d cycles" % (arg, total))
        grand += total
    if len(sys.argv) > 2:
        print("all ranges: %d cycles" % grand)


if __name__ == "__main__":
    main()
