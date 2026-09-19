"""Translate bzone_program_rom.asm (+ bzone_defines.asm) into ca65 syntax, assemble
and link it with cc65 (ca65 + ld65, flat.cfg) and compare the binary with the
program ROM $5000-$7FFF.

This is the independent third-party check: only the syntax is translated
(.alias NAME VALUE -> NAME = VALUE, [ ] grouping -> ( ), .org/.include dropped with
the equates inlined at the top); instruction text is passed through untouched, so
ca65 makes its own zero-page/absolute decisions.

    python emit_ca65.py [--cc65 DIR] [--no-build]
Default cc65 directory: the Tempest project's ../../Tempest Dissasembly/cc65-win32/bin
(this project carries no cc65 copy of its own), else ca65/ld65 on PATH.
Exit status 1 on assembler errors or any mismatch.
"""
import os, re, sys, subprocess, argparse
HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(HERE, "build")
LISTING = os.path.join(HERE, "bzone_program_rom.asm")
DEFINES = os.path.join(HERE, "bzone_defines.asm")
ROM64K = os.path.join(HERE, "_survey", "roms_extracted", "bzone_cpu64k.bin")
CFG = os.path.join(HERE, "flat.cfg")
DEFAULT_CC65 = os.path.normpath(os.path.join(HERE, "..", "..", "Tempest Dissasembly", "cc65-win32", "bin"))

ALIAS = re.compile(r"^\.alias\s+(\S+)\s+([^;\s]+)\s*(;.*)?$")
BYTE_LINE = re.compile(r"^(.*\.byte\s+)(.*)$", re.I)

def split_code(line):
    i = line.find(";")
    return (line, "") if i < 0 else (line[:i], line[i:])

def fix_byte_operand(item):
    """A .byte item that is a plain difference of two labels (A-B) is fine when both are
    CODE-segment labels, but errors ('Address size 2 does not match fragment size 1') when
    one side is a same-address alias from another module (a plain absolute '=' constant in
    ca65, not a segment-relative label) - e.g. ENDZON-ZONTBL, where ENDZON shares BZSOUN's
    start address and gets a '.alias' line instead of its own listing label.  <(...) forces
    the low byte, which is a no-op for a value that already fits in one (as any correct
    .byte operand must) and resolves ca65's address-size check."""
    it = item.strip()
    if it and not it.startswith("<") and not it.startswith(">") and re.search(r"[A-Za-z_][A-Za-z0-9_]*\s*-\s*[A-Za-z_]", it):
        return "<(%s)" % it
    return item

def translate(line):
    code, com = split_code(line.rstrip("\n"))
    t = code.strip()
    if not t:
        return line.rstrip("\n")
    if t.startswith(".org") or t.startswith(".include"):
        return None
    m = ALIAS.match(line.strip())
    if m:
        return "%s = %s%s" % (m.group(1), m.group(2).replace("[", "(").replace("]", ")"),
                              ("  " + m.group(3)) if m.group(3) else "")
    code = code.replace("[", "(").replace("]", ")")
    m = BYTE_LINE.match(code)
    if m:
        head, rest = m.group(1), m.group(2)
        items = [fix_byte_operand(x) for x in rest.split(",")]
        code = head + ", ".join(items)
    return code + com

def convert(dst):
    out = ['.setcpu "6502"', ""]
    out.append("; ---- equates from %s" % os.path.basename(DEFINES))
    for ln in open(DEFINES):
        t = translate(ln)
        if t is not None:
            out.append(t)
    out.append("")
    out.append('.segment "CODE"')
    for ln in open(LISTING):
        t = translate(ln)
        if t is not None:
            out.append(t)
    with open(dst, "w", newline="\n") as fp:
        fp.write("\n".join(out) + "\n")
    return len(out)

def tool(cc65, name):
    exe = name + (".exe" if os.name == "nt" else "")
    p = os.path.join(cc65, exe) if cc65 else exe
    return p if (not cc65 or os.path.exists(p)) else name

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cc65", default=DEFAULT_CC65 if os.path.isdir(DEFAULT_CC65) else None)
    ap.add_argument("--no-build", action="store_true", help="only write build/program_ca65.s")
    a, _ = ap.parse_known_args()
    os.makedirs(BUILD, exist_ok=True)
    src = os.path.join(BUILD, "program_ca65.s")
    n = convert(src)
    print("wrote %s (%d lines)" % (os.path.relpath(src, HERE), n))
    if a.no_build:
        return 0
    obj = os.path.join(BUILD, "program.o")
    binf = os.path.join(BUILD, "program.bin")
    try:
        subprocess.run([tool(a.cc65, "ca65"), "--cpu", "6502", "-o", obj, src], check=True)
        subprocess.run([tool(a.cc65, "ld65"), "-C", CFG, "-o", binf, obj], check=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        print("!! ca65/ld65 failed: %s" % exc)
        return 1
    got = open(binf, "rb").read()
    ref = open(ROM64K, "rb").read()[0x5000:0x8000]
    bad = sum(1 for x, y in zip(got, ref) if x != y) + abs(len(got) - len(ref))
    print("ca65 round trip: %d bytes, %d mismatches" % (len(got), bad))
    if bad:
        firsts = [i for i in range(min(len(got), len(ref))) if got[i] != ref[i]][:10]
        print("   first differences: " + ", ".join("$%04X %02X!=%02X" % (0x5000 + i, got[i], ref[i]) for i in firsts))
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
