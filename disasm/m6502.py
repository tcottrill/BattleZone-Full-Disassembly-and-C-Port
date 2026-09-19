"""Complete legal-opcode 6502 tables + encoder/decoder (copied from the Space Duel project; used by the Tempest MAC65 assembler)."""

IMP,ACC,IMM,ZP,ZPX,ZPY,ABS,ABX,ABY,IND,IZX,IZY,REL = range(13)
SIZE = {IMP:1,ACC:1,IMM:2,ZP:2,ZPX:2,ZPY:2,ABS:3,ABX:3,ABY:3,IND:3,IZX:2,IZY:2,REL:2}
MODE_NAME = {IMP:"imp",ACC:"acc",IMM:"imm",ZP:"zp",ZPX:"zp,x",ZPY:"zp,y",ABS:"abs",
             ABX:"abs,x",ABY:"abs,y",IND:"(abs)",IZX:"(zp,x)",IZY:"(zp),y",REL:"rel"}

# mnemonic -> {mode: opcode}   (documented NMOS 6502 instructions only)
TAB = {
 "ADC":{IMM:0x69,ZP:0x65,ZPX:0x75,ABS:0x6D,ABX:0x7D,ABY:0x79,IZX:0x61,IZY:0x71},
 "AND":{IMM:0x29,ZP:0x25,ZPX:0x35,ABS:0x2D,ABX:0x3D,ABY:0x39,IZX:0x21,IZY:0x31},
 "ASL":{ACC:0x0A,ZP:0x06,ZPX:0x16,ABS:0x0E,ABX:0x1E},
 "BCC":{REL:0x90},"BCS":{REL:0xB0},"BEQ":{REL:0xF0},"BMI":{REL:0x30},
 "BNE":{REL:0xD0},"BPL":{REL:0x10},"BVC":{REL:0x50},"BVS":{REL:0x70},
 "BIT":{ZP:0x24,ABS:0x2C},
 "BRK":{IMP:0x00},
 "CLC":{IMP:0x18},"CLD":{IMP:0xD8},"CLI":{IMP:0x58},"CLV":{IMP:0xB8},
 "CMP":{IMM:0xC9,ZP:0xC5,ZPX:0xD5,ABS:0xCD,ABX:0xDD,ABY:0xD9,IZX:0xC1,IZY:0xD1},
 "CPX":{IMM:0xE0,ZP:0xE4,ABS:0xEC},
 "CPY":{IMM:0xC0,ZP:0xC4,ABS:0xCC},
 "DEC":{ZP:0xC6,ZPX:0xD6,ABS:0xCE,ABX:0xDE},
 "DEX":{IMP:0xCA},"DEY":{IMP:0x88},
 "EOR":{IMM:0x49,ZP:0x45,ZPX:0x55,ABS:0x4D,ABX:0x5D,ABY:0x59,IZX:0x41,IZY:0x51},
 "INC":{ZP:0xE6,ZPX:0xF6,ABS:0xEE,ABX:0xFE},
 "INX":{IMP:0xE8},"INY":{IMP:0xC8},
 "JMP":{ABS:0x4C,IND:0x6C},
 "JSR":{ABS:0x20},
 "LDA":{IMM:0xA9,ZP:0xA5,ZPX:0xB5,ABS:0xAD,ABX:0xBD,ABY:0xB9,IZX:0xA1,IZY:0xB1},
 "LDX":{IMM:0xA2,ZP:0xA6,ZPY:0xB6,ABS:0xAE,ABY:0xBE},
 "LDY":{IMM:0xA0,ZP:0xA4,ZPX:0xB4,ABS:0xAC,ABX:0xBC},
 "LSR":{ACC:0x4A,ZP:0x46,ZPX:0x56,ABS:0x4E,ABX:0x5E},
 "NOP":{IMP:0xEA},
 "ORA":{IMM:0x09,ZP:0x05,ZPX:0x15,ABS:0x0D,ABX:0x1D,ABY:0x19,IZX:0x01,IZY:0x11},
 "PHA":{IMP:0x48},"PHP":{IMP:0x08},"PLA":{IMP:0x68},"PLP":{IMP:0x28},
 "ROL":{ACC:0x2A,ZP:0x26,ZPX:0x36,ABS:0x2E,ABX:0x3E},
 "ROR":{ACC:0x6A,ZP:0x66,ZPX:0x76,ABS:0x6E,ABX:0x7E},
 "RTI":{IMP:0x40},"RTS":{IMP:0x60},
 "SBC":{IMM:0xE9,ZP:0xE5,ZPX:0xF5,ABS:0xED,ABX:0xFD,ABY:0xF9,IZX:0xE1,IZY:0xF1},
 "SEC":{IMP:0x38},"SED":{IMP:0xF8},"SEI":{IMP:0x78},
 "STA":{ZP:0x85,ZPX:0x95,ABS:0x8D,ABX:0x9D,ABY:0x99,IZX:0x81,IZY:0x91},
 "STX":{ZP:0x86,ZPY:0x96,ABS:0x8E},
 "STY":{ZP:0x84,ZPX:0x94,ABS:0x8C},
 "TAX":{IMP:0xAA},"TAY":{IMP:0xA8},"TSX":{IMP:0xBA},
 "TXA":{IMP:0x8A},"TXS":{IMP:0x9A},"TYA":{IMP:0x98},
}

# opcode -> (mnemonic, mode); every opcode not present is an undocumented/illegal one.
OPS = {}
for _m, _d in TAB.items():
    for _mode, _op in _d.items():
        assert _op not in OPS, "opcode %02X declared twice" % _op
        OPS[_op] = (_m, _mode)
assert len(OPS) == 151, "expected 151 legal opcodes, got %d" % len(OPS)

BRANCHES = {"BCC","BCS","BEQ","BMI","BNE","BPL","BVC","BVS"}
FLOW_STOP = {"JMP","RTS","RTI","BRK"}          # execution does not continue past these
CALLS     = {"JSR"}

def decode(mem, addr):
    """Decode one instruction at addr. Returns (mnemonic, mode, operand, length)
    or None if the byte is not a documented opcode."""
    op = mem.get(addr)
    if op is None or op not in OPS:
        return None
    mn, mode = OPS[op]
    n = SIZE[mode]
    b = [mem.get(addr + i) for i in range(n)]
    if any(x is None for x in b):
        return None
    if n == 1:
        val = None
    elif n == 2:
        val = b[1]
        if mode == REL:
            val = (addr + 2 + (val - 256 if val > 127 else val)) & 0xFFFF
    else:
        val = b[1] | (b[2] << 8)
    return mn, mode, val, n
