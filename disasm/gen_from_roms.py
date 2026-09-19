#!/usr/bin/env python3
"""Rebuild the Tempest rev-3 CPU image from a ROM set and regenerate + verify every
derived listing.

ROMs are matched by part number (136002-xxx) and CRC32, not by file name, so a
MAME `tempest3.zip` (2K chips) or `tempest.zip` (4K chips, same image), a directory
holding those zips, or a directory of loose files all work.

    2K set (tempest3)            4K set (tempest)
    136002-123 np3  $3000        136002-138 np3  $3000-$3FFF  vector ROM
    136002-124 r3   $3800
    136002-113 d1   $9000        136002-133 d1   $9000-$9FFF
    136002-114 e1   $9800
    136002-115 f1   $A000        136002-134 f1   $A000-$AFFF
    136002-316 h1   $A800
    136002-217 j1   $B000        136002-235 j1   $B000-$BFFF
    136002-118 k1   $B800
    136002-119 lm1  $C000        136002-136 lm1  $C000-$CFFF
    136002-120 mn1  $C800
    136002-121 p1   $D000        136002-237 p1   $D000-$DFFF
    136002-222 r1   $D800
Everything else in the 64K image is $FF (the $E000-$FFFF mirror is not written;
the listings document it).

Usage:
    python gen_from_roms.py [ROMDIR-or-ZIP] [--check] [--cc65 DIR] [--macasm] [--image-only]

Default ROM location: ../roms.  Steps:
  1. build _survey/roms_extracted/bzone_cpu64k.bin (the image every tool reads)
  2. macasm.py (re-assemble Atari's source; only if build/symbols.json or
     build/srclines.json is missing or older than macasm.py / the source, or --macasm),
     commented_src.py if build/commented_src.json is missing
  3. Track B, if present: emit_vrom.py, verify_vrom.py, emit_shapes.py
     (emit_shapes.py also writes build/vector_tables.json + cam_scripts.json)
  4. emit.py, emit_defines.py, verify.py
  5. check_ramrefs.py (every RAM-range memory operand in the program ROM lies inside a named
     cell from bzone_defines.asm, or is individually explained - the stack page, a couple of
     indexed-addressing bases that never actually land where their own operand points)
  6. emit_alt.py, if both ROM sets (bzone.zip and bzonea.zip) were found: disassembles the
     149 bytes where MAME's other $5000 chip revision differs from the one this source
     assembles to
  7. --check: emit_ca65.py (ca65 + ld65 round trip of the program listing)
A failing step is reported with '!!' and the chain continues; exit status 1 if any failed.
"""
import argparse, os, subprocess, sys, zipfile, zlib, glob

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
IMAGE = os.path.join(HERE, "_survey", "roms_extracted", "bzone_cpu64k.bin")
BUILD = os.path.join(HERE, "build")
SRC_DIR = os.path.join(ROOT, "battlezone-main")

# part number (0364NN) -> load address; the $5000 chip decides the revision by CRC32
LAYOUT = {"22": 0x3000, "21": 0x3800, "14": 0x5000, "13": 0x5800, "12": 0x6000,
          "11": 0x6800, "10": 0x7000, "09": 0x7800}
CRCS = {"22": 0x7414177b, "21": 0x8ea8f939, "13": 0x5d9d9111, "12": 0xab55cbd2,
        "11": 0xad281297, "10": 0x0b7bfaa4, "09": 0x1e14e919}
# The $5000 chip exists in two versions.  "src" is the one Atari's source assembles to and
# the one Atari's own release disk carries as 036414.02 (BZONE.DOC, 31-AUG-81); MAME calls
# that image 036414-01 (set bzonea) and gives the name 036414-02 to the other one (set
# bzone).  "alt" is that other image; it differs in 149 bytes ($5340, $54A8-$553C).
REV_5000 = {"src": 0xefbc3fa0, "alt": 0x13de36d5}

def candidates(path, prefer="bzonea.zip"):
    """Yield (label, bytes) for every 2K file reachable from path (dir, zip or file)."""
    paths = []
    if os.path.isdir(path):
        for p in sorted(glob.glob(os.path.join(path, "*"))):
            if os.path.isfile(p):
                paths.append(p)
    else:
        paths.append(path)
    zips = []
    for p in paths:
        if p.lower().endswith(".zip"):
            zips.append(p)
            continue
        if os.path.getsize(p) == 0x800:
            yield os.path.basename(p), open(p, "rb").read()
    zips.sort(key=lambda z: (os.path.basename(z).lower() != prefer, z))
    for z in zips:
        with zipfile.ZipFile(z) as zf:
            for info in zf.infolist():
                if info.file_size == 0x800:
                    yield "%s:%s" % (os.path.basename(z), info.filename), zf.read(info)

def build_image(path, rev="src"):
    """ROMs are matched by CRC32 alone: MAME, the Atari archive and older sets all name
    the same chips differently (the archive's 036414.02 is in fact the rev-1 image)."""
    want = dict(CRCS)
    want["14"] = REV_5000[rev]
    found = {}
    for label, data in candidates(path, "bzonea.zip" if rev == "src" else "bzone.zip"):
        crc = zlib.crc32(data) & 0xFFFFFFFF
        for p, c in want.items():
            if c == crc and p not in found:
                found[p] = (label, data)
    if all(p in found for p in want):
        img = bytearray([0xFF]) * 0x10000
        for p, a in LAYOUT.items():
            img[a:a + 0x800] = found[p][1]
        print("ROM set bzone %s:" % rev)
        for p, a in sorted(LAYOUT.items(), key=lambda kv: kv[1]):
            print("  0364%s  $%04X-$%04X  %s" % (p, a, a + 0x7FF, found[p][0]))
        image = IMAGE if rev == "src" else IMAGE.replace("bzone_cpu64k", "bzone_alt_cpu64k")
        old = open(image, "rb").read() if os.path.exists(image) else None
        os.makedirs(os.path.dirname(image), exist_ok=True)
        with open(image, "wb") as fp:
            fp.write(img)
        print("wrote %s%s" % (os.path.relpath(image, ROOT),
                              "" if old is None else (" (unchanged)" if old == bytes(img) else " (CHANGED)")))
        return True
    print("!! no complete %s set found under %s (matched parts: %s)" % (rev, path, ", ".join(sorted(found)) or "none"))
    return False

def run(script, *args):
    p = os.path.join(HERE, script)
    print("\n== %s %s" % (script, " ".join(args)))
    sys.stdout.flush()
    r = subprocess.run([sys.executable, p] + list(args), cwd=HERE)
    if r.returncode != 0:
        print("!! %s failed (exit %d)" % (script, r.returncode))
    return r.returncode == 0

def macasm_needed():
    outs = [os.path.join(BUILD, f) for f in ("symbols.json", "srclines.json")]
    if not all(os.path.exists(o) for o in outs):
        return True
    t = min(os.path.getmtime(o) for o in outs)
    ins = [os.path.join(HERE, f) for f in ("macasm.py", "macsrc.py", "rtexpr.py", "m6502.py")]
    ins += glob.glob(os.path.join(SRC_DIR, "*.MAC"))
    return any(os.path.exists(i) and os.path.getmtime(i) > t for i in ins)

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("roms", nargs="?", default=os.path.join(ROOT, "roms"),
                    help="ROM directory, set zip or loose file directory (default ../roms)")
    ap.add_argument("--check", action="store_true", help="ca65/ld65 round trip of the program listing")
    ap.add_argument("--cc65", default=None,
                    help="directory holding ca65/ld65 (default: emit_ca65.py's own default, "
                         "the Tempest project's ../../Tempest Dissasembly/cc65-win32/bin - "
                         "this project carries no cc65 copy of its own)")
    ap.add_argument("--macasm", action="store_true", help="always re-run macasm.py")
    ap.add_argument("--image-only", action="store_true", help="only build the 64K image")
    a = ap.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)
    if not os.path.exists(a.roms):
        sys.exit("ROM path %s does not exist" % a.roms)
    ok = build_image(os.path.abspath(a.roms))
    alt_ok = build_image(os.path.abspath(a.roms), "alt")   # optional: only reported here;
                                                            # gates emit_alt.py below
    if not ok:
        sys.exit(1)
    if a.image_only:
        return
    if not os.path.isdir(SRC_DIR):
        print("!! Atari source not found at %s: macasm/emit need it" % SRC_DIR)
        sys.exit(1)
    if a.macasm or macasm_needed():
        ok &= run("macasm.py")
    else:
        print("\n(macasm.py outputs are up to date; --macasm forces a re-run)")
    if not os.path.exists(os.path.join(BUILD, "commented_src.json")) and os.path.exists(os.path.join(HERE, "commented_src.py")):
        ok &= run("commented_src.py")
    for script in ("emit_vrom.py", "verify_vrom.py", "emit_shapes.py"):      # Track B (vector ROM)
        if os.path.exists(os.path.join(HERE, script)):
            ok &= run(script)
        else:
            print("\n(%s not present - skipped)" % script)
    ok &= run("emit.py")
    ok &= run("emit_defines.py")
    ok &= run("verify.py")
    ok &= run("check_ramrefs.py")
    ok &= run("mkfunctions.py")
    if alt_ok and os.path.exists(os.path.join(HERE, "_survey", "roms_extracted", "bzone_alt_cpu64k.bin")):
        ok &= run("emit_alt.py")
    else:
        print("\n(alt ROM image not available - emit_alt.py skipped; needs both bzone.zip and "
              "bzonea.zip under %s)" % os.path.relpath(a.roms, HERE))
    if a.check:
        ok &= run("emit_ca65.py", *(["--cc65", a.cc65] if a.cc65 else []))
    print("\n%s" % ("all steps passed" if ok else "!! some steps failed; see the !! lines above"))
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
