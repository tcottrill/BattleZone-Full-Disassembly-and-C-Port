"""cyc_audit.py - cross-check CYC(n) charges against the ROM.

    python tools\\cyc_audit.py file.c [...]

For every line `CYC(<integer>); /* Lxxxx-Lyyyy ...` (a plain integer charge
whose comment starts with one address range), compares n with the base-cycle
sum of the ROM instructions from Lxxxx through Lyyyy (tools\\cyc.py's model,
conditional branches counted not-taken).  Prints the lines that differ.  A
difference is not always an error - the comment may describe a block that
ends in a taken branch (+1) or skips a JSR - but each one deserves a look.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cyc  # noqa: E402

LINE = re.compile(r"CYC\((\d+)\);\s*/\*\s*L([0-9A-F]{4})-L?([0-9A-F]{4})\b(.*)")


def rom_sum(rom, a, end):
    total = 0
    while a <= end:
        op = rom[a - 0x5000]
        total += cyc.CYC[op]
        a += cyc.length(op)
    return total


def main():
    rom = cyc.load_rom()
    bad = 0
    for path in sys.argv[1:]:
        for n, line in enumerate(open(path, encoding="latin-1"), 1):
            m = LINE.search(line)
            if not m:
                continue
            charged, a, end, rest = int(m.group(1)), int(m.group(2), 16), int(m.group(3), 16), m.group(4)
            if not (0x5000 <= a <= end <= 0x7FFF):
                continue
            want = rom_sum(rom, a, end)
            if want != charged:
                bad += 1
                print("%s:%d: CYC(%d) but L%04X-L%04X = %d%s" % (path, n, charged, a, end, want, rest[:60]))
    print("%d difference(s)" % bad)


if __name__ == "__main__":
    main()
