#!/usr/bin/env python3
"""avg_prom_sim.py - GATE T: avg.c's AVG draw-time model against MAME and the
real Battlezone AVG state PROM.

avg.c charges every AVG instruction a fixed number of state-PROM ticks plus
the vector timer (avg.h TIMING) - a model copied unchanged from the Tempest
port's avg.c/.h (C:\\Source2026\\Tempest Dissasembly\\c_src\\tools\\
avg_prom_sim.py is what this file adapts).  This file derives the same
number independently: a transcription of MAME's actual AVG state machine
(avgdvg.cpp's run_state_machine + handler_0..7), run against the REAL 256x4
state PROM - Battlezone's own copy, ../roms/bzonea.zip member 036408-01.k7 -
and compared with what tests\\avgtime.exe (avg.c) prints for the same lists.

Evidence this is the SAME state machine Tempest's avg.c already proved
against 136002-125, not a new one to re-derive:
  - 036408-01.k7 (bzonea.zip) and 136002-125 (Tempest's ROM set) are
    byte-identical: both 256 bytes, CRC32 5903AF03 (checked by main()
    below, `--verify-prom`).
  - AAE's avg_bzone (mame_late_avgdvg.cpp, vgconf table ~line 1549) reuses
    avg_latch0, avg_latch2, avg_strobe0, avg_strobe1 and avg_data/avg_vggo/
    avg_vgrst UNCHANGED from the plain avg_default/avg_tempest config; its
    own bzone_latch1 ends by calling avg_latch1 (the OP/dvy assignment is
    identical, it just also records the clip-window corner first) and its
    bzone_strobe2/bzone_strobe3 both tail-call avg_common_strobe2/
    avg_common_strobe3 (the shared JSRL/RTSL/JMPL dispatch and the
    timer->cycles arithmetic) after their own monochrome-intensity/
    clip-window bookkeeping.  None of Battlezone's own handlers change an
    op code, a state transition or a cycle count - only STAT's Z/HI/IN
    latching and the drawn segment's colour differ, neither of which
    avg_prom_sim.py's Avg class (below) needs to model, since it only
    tracks the registers TIMING depends on.
  So the class below is Tempest's, verbatim - only the harness around it
  (PROM source, memory layout, what gets timed) is Battlezone-specific.

Unlike Tempest (whose master lists loop forever via JMPL to address 0) and
unlike the oracle-driven frame captures this milestone doesn't have yet,
Battlezone's ROM pictures are subroutines that end in RTSL - the hardware
has no "depth" concept, so timing "one call" needs a real caller: this file
(and tests\\avgtime.exe) wrap every picture/synthetic list as
  $2000: JSRL <target>
  $2002: HALT
exactly as BZONE.MAC's own display-list builder would, and time from GO
($2000) to the HALT the RTSL falls back into - see tests\\avgtime.c's
header for the full rationale.

    python tools/avg_prom_sim.py                  pictures + synthetic lists (GATE T)
    python tools/avg_prom_sim.py --frames DIR ...  frame_NNNN.vram captures (M1+, not yet produced)
    python tools/avg_prom_sim.py --verify-prom     just print the CRC32 check
"""
import argparse
import binascii
import glob
import os
import re
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
CSRC = os.path.dirname(HERE)
ROOT = os.path.dirname(CSRC)
DISASM = os.path.join(ROOT, "disasm")
sys.path.insert(0, HERE)
import avg_gate_v  # noqa: E402  (reuses load_entries/write_index/clean)

MASTER_CLOCK = 12_096_000
ROM_ZIP = os.path.join(ROOT, "roms", "bzonea.zip")
PROM_MEMBER = "036408-01.k7"
TEMPEST_PROM = os.path.join(ROOT, "..", "Tempest Dissasembly", "disasm", "_survey",
                            "roms_extracted", "tempest", "136002-125.d7")
ROM_IMAGE = os.path.join(DISASM, "_survey", "roms_extracted", "bzone_cpu64k.bin")
EXE_PATH = os.path.join(CSRC, "tests", "avgtime.exe")

# Synthetic subroutines - byte-for-byte identical to tests\avgtime.c's
# SYNTH_WORDS / SYNTH_ENTRIES (see that file's header for what each does and
# the avg.py avg.encode() calls that produced the words).
SYNTH_WORDS = [
    0x0000, 0x0000,                              # 0,1: wrapper (overwritten)
    0, 0, 0, 0, 0, 0,                             # 2-7: unused
    0x7340, 0x1ED4, 0xA1F4, 0xC000,               # 8-11: A  ($2010)
    0x8040, 0x02BC, 0x5F38, 0xC000,               # 12-15: B ($2018)
    0xA012, 0xC000,                               # 16-17: C ($2020)
    0x5B65, 0xA015, 0xC000,                       # 18-20: C1 ($2024)
    0x42FD, 0xA018, 0xC000,                       # 21-23: C2 ($202A)
    0x4121, 0xC000,                               # 24-25: C3 ($2030)
    0x4FD1, 0xC000,                               # 26-27: D  ($2034)
    0x0000, 0x0000, 0xC000,                       # 28-30: E  ($2038)
]
SYNTH_ENTRIES = [("SYN_A", 0x2010), ("SYN_B", 0x2018), ("SYN_C", 0x2020),
                  ("SYN_D", 0x2034), ("SYN_E", 0x2038)]


def words_to_bytes(words, size=0x1000):
    buf = bytearray(size)
    for i, w in enumerate(words):
        buf[2 * i] = w & 0xFF
        buf[2 * i + 1] = (w >> 8) & 0xFF
    return bytes(buf)


def load_prom():
    with zipfile.ZipFile(ROM_ZIP) as z:
        data = z.read(PROM_MEMBER)
    assert len(data) == 0x100, (PROM_MEMBER, len(data))
    return data


def verify_prom(prom):
    crc = binascii.crc32(prom) & 0xFFFFFFFF
    ok = crc == 0x5903AF03
    print("bzonea.zip %s: %d bytes, CRC32 %08X (%s Tempest's 136002-125)" %
          (PROM_MEMBER, len(prom), crc, "==" if ok else "!="))
    if os.path.exists(TEMPEST_PROM):
        tdata = open(TEMPEST_PROM, "rb").read()
        tcrc = binascii.crc32(tdata) & 0xFFFFFFFF
        print("Tempest 136002-125.d7: %d bytes, CRC32 %08X" % (len(tdata), tcrc))
        ok = ok and tcrc == crc
    return ok


class Avg:
    """MAME's avg_device state machine, transcribed (Tempest's
    tools/avg_prom_sim.py, unchanged - see this file's header for why
    Battlezone's avg_bzone doesn't need its own version).  The vector
    output is dropped; every register that feeds the cycle counts is kept."""

    def __init__(self, prom, mem, start_pc=0):
        self.prom, self.mem = prom, mem
        self.state_latch = 0
        self.bin_scale = self.scale = 0
        self.pc = start_pc
        self.sp = 0
        self.halt = 0
        self.dvx = self.dvy = self.dvy12 = self.op = 0
        self.timer = self.int_latch = 0
        self.data = 0
        self.stack = [0] * 4

    def OP0(self): return self.op & 1
    def OP1(self): return (self.op >> 1) & 1
    def OP2(self): return (self.op >> 2) & 1

    def state_addr(self):
        return ((((self.state_latch >> 4) ^ 1) << 7) | (self.op << 4) | (self.state_latch & 0xF))

    def h0(self):
        self.dvy = (self.dvy & 0x1F00) | self.data
        self.pc += 1
        return 0

    def h1(self):
        self.dvy12 = (self.data >> 4) & 1
        self.op = self.data >> 5
        self.int_latch = 0
        self.dvy = (self.dvy12 << 12) | ((self.data & 0xF) << 8)
        self.dvx = 0
        self.pc += 1
        return 0

    def h2(self):
        self.dvx = (self.dvx & 0x1F00) | self.data
        self.pc += 1
        return 0

    def h3(self):
        self.int_latch = self.data >> 4
        self.dvx = ((self.int_latch & 1) << 12) | ((self.data & 0xF) << 8) | (self.dvx & 0xFF)
        self.pc += 1
        return 0

    def h4(self):
        if self.OP0():
            self.stack[self.sp & 3] = self.pc
        else:
            i = 0
            while ((((self.dvy ^ (self.dvy << 1)) & 0x1000) == 0)
                   and (((self.dvx ^ (self.dvx << 1)) & 0x1000) == 0) and i < 16):
                i += 1
                self.dvy = (self.dvy & 0x1000) | ((self.dvy << 1) & 0x1FFF)
                self.dvx = (self.dvx & 0x1000) | ((self.dvx << 1) & 0x1FFF)
                self.timer = (self.timer >> 1) | 0x4000 | (self.OP1() << 7)
            if self.OP1():
                self.timer &= 0xFF
        return 0

    def h5(self):
        if not self.OP2():
            for _ in range(self.bin_scale):
                self.timer = (self.timer >> 1) | 0x4000 | (self.OP1() << 7)
            if self.OP1():
                self.timer &= 0xFF
        if self.OP2():
            self.sp = (self.sp - 1) & 0xF if self.OP1() else (self.sp + 1) & 0xF
        return 0

    def h6(self):
        if self.OP2():
            if self.OP0():
                self.pc = self.dvy << 1
            else:
                self.pc = self.stack[self.sp & 3]
        elif self.dvy12:
            self.scale = self.dvy & 0xFF
            self.bin_scale = (self.dvy >> 8) & 7
        return 0

    def h7(self):
        cycles = 0
        self.halt = self.OP0()
        if not self.OP0() and not self.OP2():
            cycles = (0x100 - (self.timer & 0xFF)) if self.OP1() else (0x8000 - self.timer)
            self.timer = 0
        if self.OP2():
            cycles = 0x8000 - self.timer
            self.timer = 0
        return cycles

    def run(self, limit=4_000_000):
        """-> ('HALT', cycles less the lead-in) or ('BUDGET', cycles)."""
        H = (self.h0, self.h1, self.h2, self.h3, self.h4, self.h5, self.h6, self.h7)
        cycles = steps = 0
        while steps < limit:
            self.state_latch = (self.state_latch & 0x10) | (self.prom[self.state_addr()] & 0xF)
            if (self.state_latch >> 3) & 1:
                self.data = self.mem[(self.pc ^ 1) & 0x1FFF]
                cycles += H[self.state_latch & 7]()
            self.state_latch = (self.halt << 4) | (self.state_latch & 0xF)
            cycles += 8
            steps += 1
            if self.halt:
                return "HALT", cycles - 8
        return "BUDGET", cycles


def wrapped_mem(vram, vrom, entry):
    """CPU $2000/$2002 = JSRL <entry>; HALT, into a COPY of vram (vrom
    untouched) - the "8K flat AVG space" the sim indexes with pc^1 & 0x1FFF."""
    v = bytearray(vram)
    jsrl = 0xA000 | (((entry - 0x2000) >> 1) & 0x1FFF)
    v[0], v[1] = jsrl & 0xFF, (jsrl >> 8) & 0xFF
    v[2], v[3] = 0x00, 0x20
    return bytes(v) + bytes(vrom)


def time_pictures(prom, mem64k, entries):
    vram = mem64k[0x2000:0x3000]
    vrom = mem64k[0x3000:0x4000]
    out = {}
    for name, addr in entries:
        stop, cyc = Avg(prom, wrapped_mem(vram, vrom, addr)).run()
        out[name] = (stop, cyc)
    return out


def time_synth(prom):
    vram = words_to_bytes(SYNTH_WORDS)
    vrom = bytes(0x1000)
    out = {}
    for name, addr in SYNTH_ENTRIES:
        stop, cyc = Avg(prom, wrapped_mem(vram, vrom, addr)).run()
        out[name] = (stop, cyc)
    return out


def parse_c_time(path):
    """-> {name: (addr, stop, cycles)} from a TIME NAME ADDR STOP CYCLES file."""
    out = {}
    with open(path) as f:
        for line in f:
            if not line.startswith("TIME "):
                continue
            _, name, addrhex, stop, cyc = line.split()
            out[name] = (int(addrhex, 16), stop, int(cyc))
    return out


def gate_t():
    if not verify_prom(load_prom()):
        print("GATE T: prom CRC mismatch - aborting")
        return 1
    if not os.path.exists(EXE_PATH):
        print("tests\\avgtime.exe not found - run build_avg.bat first")
        return 2
    if not os.path.exists(ROM_IMAGE):
        print("missing %s - run disasm/gen_from_roms.py first" % ROM_IMAGE)
        return 2

    prom = load_prom()
    mem64k = open(ROM_IMAGE, "rb").read()
    assert len(mem64k) == 0x10000

    entries = avg_gate_v.load_entries()
    avg_gate_v.write_index(entries, avg_gate_v.INDEX_PATH)

    os.makedirs(os.path.join(CSRC, "tests", "avg_out"), exist_ok=True)
    r = subprocess.run([EXE_PATH, "pictures", "tests\\avg_shapes_index.txt",
                         "..\\disasm\\_survey\\roms_extracted\\bzone_cpu64k.bin",
                         "tests\\avg_out\\avg_time_pictures.txt"],
                        cwd=CSRC, capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    r2 = subprocess.run([EXE_PATH, "synth", "tests\\avg_out\\avg_time_synth.txt"],
                         cwd=CSRC, capture_output=True, text=True)
    sys.stdout.write(r2.stdout)
    sys.stderr.write(r2.stderr)
    if r.returncode != 0 or r2.returncode != 0:
        print("tests\\avgtime.exe failed")
        return 2

    c_pictures = parse_c_time(os.path.join(CSRC, "tests", "avg_out", "avg_time_pictures.txt"))
    c_synth = parse_c_time(os.path.join(CSRC, "tests", "avg_out", "avg_time_synth.txt"))
    py_pictures = time_pictures(prom, mem64k, entries)
    py_synth = time_synth(prom)

    total = bad = 0
    for name, (stop, cyc) in list(py_pictures.items()) + list(py_synth.items()):
        table = c_pictures if name in c_pictures else c_synth
        total += 1
        c = table.get(name)
        if c is None:
            bad += 1
            print("MISMATCH %-10s: C side produced no TIME record" % name)
            continue
        _addr, c_stop, c_cyc = c
        if (stop, cyc) != (c_stop, c_cyc):
            bad += 1
            print("MISMATCH %-10s: PROM sim %s %d   avg.c %s %d" % (name, stop, cyc, c_stop, c_cyc))

    print("GATE T: %d lists timed, %d mismatches" % (total, bad))
    print("GATE T: %s" % ("PASS" if total > 0 and bad == 0 else "FAIL"))
    return 0 if total > 0 and bad == 0 else 1


def gate_t_frames(dirs):
    """Tempest-style frame_NNNN.vram comparison - not exercised until the
    oracle milestone produces captures; kept ready (avg_gate_v-independent)."""
    prom = load_prom()
    vrom_default = open(ROM_IMAGE, "rb").read()[0x3000:0x4000] if os.path.exists(ROM_IMAGE) else bytes(0x1000)
    exe = EXE_PATH
    total = bad = 0
    for d in dirs:
        table = {}
        r = subprocess.run([exe, "frames", d], capture_output=True, text=True)
        for line in r.stdout.splitlines():
            m = re.match(r"frame_(\d+)\s+(\S+)\s+ops=\d+\s+cycles=(\d+)", line)
            if m:
                table[int(m.group(1))] = (m.group(2), int(m.group(3)))
        nd = nbad = 0
        for p in sorted(glob.glob(os.path.join(d, "frame_*.vram"))):
            n = int(re.search(r"frame_(\d+)\.vram$", p).group(1))
            vram = open(p, "rb").read()
            stop, cyc = Avg(prom, vram + vrom_default).run()
            tstop, tcyc = table.get(n, ("?", -1))
            nd += 1
            if (stop, cyc) != (tstop, tcyc):
                nbad += 1
                if nbad <= 10:
                    print("frame_%04d  PROM %s %d   avg.c %s %d   *** MISMATCH" % (n, stop, cyc, tstop, tcyc))
        print("%s: %d lists, %d mismatches" % (d, nd, nbad))
        total += nd
        bad += nbad
    print("%d lists, %d mismatches" % (total, bad))
    return 0 if not bad and total else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--frames", nargs="+", metavar="DIR", help="frame_NNNN.vram captures (M1+)")
    ap.add_argument("--verify-prom", action="store_true", help="just print the CRC32 check")
    args = ap.parse_args()

    if args.verify_prom:
        return 0 if verify_prom(load_prom()) else 1
    if args.frames:
        return gate_t_frames(args.frames)
    return gate_t()


if __name__ == "__main__":
    sys.exit(main())
