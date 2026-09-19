"""Byte-exact verifier for disasm/bzone_vector_rom.asm.

Reads the listing back WITHOUT using vromsrc/emit_vrom's data model: collects
labels from a first pass, then re-encodes every V-line (AVG pseudo-ops via
avg.encode, .byte/.word taken as literal $hex/decimal values) at its stated
address and compares with the 64K ROM image. Requires the lines to cover
$3000-$3FFF exactly once, contiguously. Exit status 1 on any mismatch.
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import avg

HERE = os.path.dirname(os.path.abspath(__file__))
LISTING = os.path.join(HERE, "bzone_vector_rom.asm")

LINE = re.compile(r"^V([0-9A-F]{4}):\s+(\S+)\s*(.*?)\s*(?:;.*)?$")
LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")


def value(tok, syms):
    tok = tok.strip()
    if tok.startswith("$"):
        return int(tok[1:], 16)
    if re.match(r"^-?\d+$", tok):
        return int(tok)
    return syms[tok]


def main(path=LISTING):
    mem = avg.load_image()
    lines = open(path, encoding="ascii").read().split("\n")
    syms, pending = {}, []
    # pass 1: symbols (every 'NAME:' banner line refers to the next V-line)
    for ln in lines:
        m = LINE.match(ln)
        if m:
            for p in pending:
                syms[p] = int(m.group(1), 16)
            pending = []
            continue
        m = LABEL.match(ln)
        if m:
            pending.append(m.group(1))
    # pass 2: bytes
    pc, nbytes, nlines, bad = 0x3000, 0, 0, []
    for no, ln in enumerate(lines, 1):
        m = LINE.match(ln)
        if not m:
            continue
        a, mn, oper = int(m.group(1), 16), m.group(2), m.group(3)
        if a != pc:
            bad.append("line %d: address $%04X, expected $%04X" % (no, a, pc))
            pc = a
        try:
            if mn == ".byte":
                data = [value(x, syms) & 0xFF for x in oper.split(",")]
            elif mn == ".word":
                data = []
                for x in oper.split(","):
                    v = value(x, syms)
                    data += [v & 0xFF, v >> 8]
            else:
                if mn in ("JSRL", "JMPL"):
                    oper = "$%04X" % value(oper, syms)
                words = avg.encode(mn, oper)
                if words is None:
                    raise ValueError("cannot encode %s %s" % (mn, oper))
                data = []
                for w in words:
                    data += [w & 0xFF, w >> 8]
        except (KeyError, ValueError) as e:
            bad.append("line %d: %s" % (no, e))
            continue
        rom = list(mem[a:a + len(data)])
        if rom != data:
            bad.append("line %d: $%04X listing %s ROM %s" % (
                no, a, " ".join("%02X" % b for b in data), " ".join("%02X" % b for b in rom)))
        pc = a + len(data)
        nbytes += len(data)
        nlines += 1
    if pc != 0x4000:
        bad.append("listing ends at $%04X, expected $4000" % pc)
    print("bzone_vector_rom.asm: %d lines, %d bytes verified, %d symbols, %d MISMATCHES"
          % (nlines, nbytes, len(syms), len(bad)))
    for b in bad[:30]:
        print("  !!", b)
    return 0 if not bad and nbytes == 0x1000 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else LISTING))
