#!/usr/bin/env python3
"""Generate FUNCTIONS.md - the routine/data index - from the listings.

Everything here is derived, not transcribed: names, addresses and the
one-line descriptions come straight from `bzone_program_rom.asm` and
`bzone_vector_rom.asm` (which are themselves generated from Atari's
source by emit.py/emit_vrom.py); caller/reference counts are computed
by scanning every instruction operand in both listings; the grouping
follows the source's own module and `.SBTTL` section structure via
`build/srclines.json`. So the index cannot drift from the listing -
regenerate and it is correct again.

Cross-checked against build/symbols.json (macasm.py's own symbol table,
summarised in build/macasm_report.md) and against verify.py /
verify_vrom.py's byte counts: the script exits with an error and does
NOT write FUNCTIONS.md if its own counts don't reconcile.

CALLING_NOTES.md, the hand-written entry/exit contracts, is a separate
document; this script only builds the index it refers to.

Usage:
    python mkfunctions.py
"""
import collections
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(HERE, "build")
PROG_ASM = os.path.join(HERE, "bzone_program_rom.asm")
VEC_ASM = os.path.join(HERE, "bzone_vector_rom.asm")
OUT = os.path.join(HERE, "FUNCTIONS.md")

PROG_LO, PROG_HI = 0x5000, 0x8000
VEC_LO, VEC_HI = 0x3000, 0x4000

MODULE_ORDER = ["BZMTNS", "BZONE", "BZSOUN", "VGUT", "BZSTST"]
MODULE_DESC = {
    "BZMTNS": "Mountains/terrain: the AVG display lists and 3-D point/edge "
              "tables that live entirely in the vector ROM $3000-$3FFF - no "
              "6502 code of its own.",
    "BZONE": "Main game module: page-0 variables, the main (vector-sync) "
             "loop, logo, high-score table, IRQ handler, mountain/volcano "
             "drawing, 3-D translate/rotate, explosions, object projection, "
             "collision checks, tank/robot/buzz-bomber/saucer motion and AI, "
             "radar, messages and information display. Includes the coin "
             "routine (TC65/COIN65, MOOLAH) and the alphanumeric "
             "vector-character macros (ASCVG, VGAN.MAC).",
    "BZSOUN": "Discrete sound sequencer: the $1840 latch writers, MODSND "
              "and SNDON/SNDOFF.",
    "VGUT": "Vector generator utilities: VGADD/VGVCTR, VGHALT, VGHEX(Z), "
            "VGVTR*, VGDOT, VGJMPL/VGJSRL/VGRTSL, VGCNTR, VGSTAT/VGSTA1, "
            "VGSCAL/VGSCA1.",
    "BZSTST": "Operator self-test: RAM, ROM checksums, the vector "
              "generator, switches, the POKEY and the EAROM. Includes the "
              "Mathbox diagnostic (MBDIAG.MAC).",
}

JUMP_MNEMONICS = {"JSR", "JMP", "BEQ", "BNE", "BCC", "BCS", "BPL", "BMI",
                   "BVC", "BVS"}
VEC_JUMP_MNEMONICS = {"JSRL", "JMPL"}

HEADER_DASH_RE = re.compile(r"^;-{20,}$")
HEADER_TITLE_RE = re.compile(r"^; (\S+)(?: - (.*))?$")
HEADER_CONT_RE = re.compile(r"^;   (.*)$")
HEADER_AKA_RE = re.compile(r"^;\s+\(also: (.*)\)$")
MODULE_RE = re.compile(r"^; MODULE (\S+)(?: \(continued\))?\s+(\S+\.MAC)$")
SBTTL_RE = re.compile(r"^;\.SBTTL (.*)$")
LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):\s*(?:;.*)?$")
ADDR_RE = re.compile(r"^([LV])([0-9A-Fa-f]{4}):\s+(\S.*)$")
ALIAS_RE = re.compile(r"^\.alias\s+(\S+)\s+\$([0-9A-Fa-f]+)")
IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def load_json(name):
    with open(os.path.join(BUILD, name), encoding="utf-8") as fp:
        return json.load(fp)


def run_and_capture(script):
    """Run a verifier script and return its stdout (for cross-checking its
    printed numbers, not just trusting a cached build/ artifact)."""
    r = subprocess.run([sys.executable, os.path.join(HERE, script)],
                        cwd=HERE, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("!! %s failed (needed for FUNCTIONS.md's totals):\n%s"
                  % (script, r.stdout + r.stderr))
    return r.stdout


# ---------------------------------------------------------------- parsing --

class Entry:
    __slots__ = ("names", "addr", "src", "is_header", "header_text",
                 "sbttl", "mnemonic", "is_directive", "size",
                 "module", "file", "kind", "desc", "callers")

    def __init__(self, names, addr, src, is_header, header_text, sbttl,
                 mnemonic, is_directive):
        self.names = names
        self.addr = addr
        self.src = src              # 'prog' or 'vec'
        self.is_header = is_header
        self.header_text = header_text
        self.sbttl = sbttl
        self.mnemonic = mnemonic
        self.is_directive = is_directive
        self.size = 0


def parse_listing(path, src):
    """-> (entries, all_lines) - all_lines kept for the reference scan."""
    lines = open(path, encoding="utf-8").read().splitlines()
    entries = []
    cur_sbttl = ""
    pending_names = []          # bare "NAME:" lines waiting for an address
    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]

        m = MODULE_RE.match(line)
        if m:
            i += 1
            continue

        m = SBTTL_RE.match(line)
        if m:
            cur_sbttl = m.group(1).strip()
            i += 1
            continue

        m = ALIAS_RE.match(line)
        if m:
            name, addr = m.group(1), int(m.group(2), 16)
            entries.append(Entry([name], addr, src, False, "", cur_sbttl,
                                  "(alias)", None))
            i += 1
            continue

        if HEADER_DASH_RE.match(line):
            # header block: dash, "; NAME - desc", continuation lines, dash
            j = i + 1
            if j >= n:
                break
            m2 = HEADER_TITLE_RE.match(lines[j])
            if not m2:
                i += 1
                continue
            desc_lines = [m2.group(2) or ""]
            j += 1
            while j < n and not HEADER_DASH_RE.match(lines[j]):
                cm = HEADER_CONT_RE.match(lines[j])
                if cm and not HEADER_AKA_RE.match(lines[j]):
                    desc_lines.append(cm.group(1))
                j += 1
            # j now on the closing dash (or ran off the end - shouldn't happen)
            i = j + 1
            header_text = desc_lines[0].strip()
            # collect the "NAME:" lines that follow (primary + any aka)
            names = []
            while i < n:
                lm = LABEL_RE.match(lines[i])
                if not lm:
                    break
                names.append(lm.group(1))
                i += 1
            if not names:
                names = [m2.group(1)]
            # find the address line
            addr, mnemonic, is_directive = _find_addr_line(lines, i, n)
            if addr is None:
                continue
            entries.append(Entry(names, addr, src, True, header_text,
                                  cur_sbttl, mnemonic, is_directive))
            continue

        m = LABEL_RE.match(line)
        if m:
            pending_names.append(m.group(1))
            i += 1
            continue

        m = ADDR_RE.match(line)
        if m and pending_names:
            addr = int(m.group(2), 16)
            rest = m.group(3)
            mnemonic = rest.split(None, 1)[0]
            is_directive = mnemonic.startswith(".")
            entries.append(Entry(pending_names, addr, src, False, "",
                                  cur_sbttl, mnemonic, is_directive))
            pending_names = []
            i += 1
            continue

        i += 1

    return entries, lines


def _find_addr_line(lines, i, n):
    while i < n:
        m = ADDR_RE.match(lines[i])
        if m:
            addr = int(m.group(2), 16)
            rest = m.group(3)
            mnemonic = rest.split(None, 1)[0]
            return addr, mnemonic, mnemonic.startswith(".")
        if LABEL_RE.match(lines[i]) or lines[i].strip() == "":
            i += 1
            continue
        break
    return None, None, None


# --------------------------------------------------------- reference scan --

def scan_refs(lines):
    """-> {name: [(addr, mnemonic), ...]} for every operand token in the
    listing that names a known label (scanned generically; filtered to
    KNOWN_NAMES by the caller)."""
    refs = collections.defaultdict(list)
    for line in lines:
        m = ADDR_RE.match(line)
        if not m:
            continue
        addr = int(m.group(2), 16)
        rest = m.group(3)
        mnemonic = rest.split(None, 1)[0]
        operand = rest.split(";", 1)[0][len(mnemonic):].strip()
        if not operand:
            continue
        for tok in IDENT_RE.findall(operand):
            refs[tok].append((addr, mnemonic))
    return refs


# --------------------------------------------------------------- grouping --

def addr_module_index(srclines):
    """Sorted (addr, module, file) list for bisecting a listing address to
    the source module/file it came from."""
    idx = sorted({(x["addr"], x["module"], x["file"]) for x in srclines},
                 key=lambda t: t[0])
    return idx


def module_file_for(addr, idx):
    import bisect
    addrs = [t[0] for t in idx]
    p = bisect.bisect_right(addrs, addr) - 1
    if p < 0:
        return "?", "?"
    return idx[p][1], idx[p][2]


def main():
    symbols = load_json("symbols.json")
    vector_tables = load_json("vector_tables.json")
    srclines = load_json("srclines.json")

    prog_entries, prog_lines = parse_listing(PROG_ASM, "prog")
    vec_entries, vec_lines = parse_listing(VEC_ASM, "vec")
    all_entries = prog_entries + vec_entries

    # --- group entries sharing one address, compute size from distinct
    #     addresses within each ROM region ---
    def region_sizes(entries, lo, hi):
        distinct = sorted(set(e.addr for e in entries))
        size_of = {}
        for k, a in enumerate(distinct):
            nxt = distinct[k + 1] if k + 1 < len(distinct) else hi
            size_of[a] = nxt - a
        for e in entries:
            e.size = size_of[e.addr]

    region_sizes(prog_entries, PROG_LO, PROG_HI)
    region_sizes(vec_entries, VEC_LO, VEC_HI)

    # merge same-address entries in the same file into one row (aka names),
    # preferring a header entry over a bare one at that address
    def rank(e):
        # prefer a real header, then any real content line, over a bare
        # module-boundary ".alias NAME $addr" marker (which carries no
        # mnemonic/is_directive of its own)
        if e.is_header:
            return 2
        if e.mnemonic != "(alias)":
            return 1
        return 0

    def merge(entries):
        by_addr = {}
        for e in entries:
            if e.addr not in by_addr:
                by_addr[e.addr] = e
            else:
                other = by_addr[e.addr]
                if rank(e) > rank(other):
                    e.names = e.names + [n for n in other.names if n not in e.names]
                    by_addr[e.addr] = e
                else:
                    other.names += [n for n in e.names if n not in other.names]
        return sorted(by_addr.values(), key=lambda e: e.addr)

    prog_rows = merge(prog_entries)
    vec_rows = merge(vec_entries)

    # --- module/file attribution (by address, via srclines.json) ---
    idx = addr_module_index(srclines)
    for e in prog_rows + vec_rows:
        mod, fil = module_file_for(e.addr, idx)
        e.module, e.file = mod, fil
    # BZMTNS contributes no program-ROM bytes; all vector-ROM entries not
    # otherwise attributed by srclines fall back to BZMTNS/BZMTNS.MAC
    for e in vec_rows:
        if e.module not in MODULE_ORDER:
            e.module, e.file = "BZMTNS", "BZMTNS.MAC"

    # --- vector-ROM data classification, from build/vector_tables.json ---
    avg_pic_names = {p["name"] for p in vector_tables["avg_pictures"]}
    vgan_names = {c["name"] for c in vector_tables["vgan_characters"]}
    point_names = set(vector_tables["point_tables"])
    edge_names = set(vector_tables["edge_lists"])
    obj_note = {}
    for o in vector_tables["objects"]:
        if o.get("point_table"):
            obj_note[o["point_table"]] = o.get("note", "")
        if o.get("edge_list"):
            obj_note[o["edge_list"]] = o.get("note", "")
    pic_note = {p["name"]: p.get("note", "") for p in vector_tables["avg_pictures"]}
    vgan_note = {c["name"]: "VGAN character (source name %s)" % c.get("source_name", "")
                 for c in vector_tables["vgan_characters"]}
    misc_note = {k: "lookup table (%s)" % v.get("first_statement", "")
                 for k, v in vector_tables["misc_tables"].items()}

    srcline_comment = {}
    for x in srclines:
        if x.get("comment"):
            srcline_comment.setdefault(x["addr"], x["comment"])

    def classify(e):
        names = e.names
        if e.src == "prog":
            if e.mnemonic == "(alias)":
                return "alias", ""
            if e.is_header:
                return "routine", e.header_text
            if e.is_directive:
                if "MESSAGE" in e.sbttl.upper():
                    return "message", srcline_comment.get(e.addr, "")
                return "data table", srcline_comment.get(e.addr, "")
            return "local entry", ""
        else:
            if e.mnemonic == "(alias)":
                return "alias", ""
            for nm in names:
                if nm in avg_pic_names:
                    return "AVG picture", pic_note.get(nm, "")
                if nm in vgan_names:
                    return "AVG picture", vgan_note.get(nm, "")
                if nm in point_names or nm in edge_names:
                    return "3D table", obj_note.get(nm, "")
                if nm in misc_note:
                    return "data table", misc_note.get(nm, "")
            return "data table", srcline_comment.get(e.addr, "")

    for e in prog_rows + vec_rows:
        e.kind, e.desc = classify(e)

    # ---------------------------------------------------------- refs scan --
    known = set()
    for e in prog_rows + vec_rows:
        known.update(e.names)

    # Message-table propagation: only LNGTBL itself sits under the
    # `.SBTTL MESSAGE ROUTINE` heading; the actual per-language pointer
    # tables and per-message text blocks it fans out to (through two more
    # levels of `.word` pointers) live under `.SBTTL DATA STRUCTURES`
    # instead, indistinguishable from other data there by section alone.
    # Follow LNGTBL's own operand references transitively and reclassify
    # whatever "data table" entries it reaches as "message" too.
    name_to_row = {}
    for e in prog_rows:
        for nm in e.names:
            name_to_row[nm] = e

    def forward_idents(e):
        lo, hi = e.addr, e.addr + e.size
        out = set()
        for line in prog_lines:
            m = ADDR_RE.match(line)
            if not m:
                continue
            a = int(m.group(2), 16)
            if not (lo <= a < hi):
                continue
            rest = m.group(3)
            mnemonic = rest.split(None, 1)[0]
            operand = rest.split(";", 1)[0][len(mnemonic):].strip()
            out.update(IDENT_RE.findall(operand))
        return out & known

    frontier = [e for e in prog_rows if e.kind == "message"]
    seen = set()
    while frontier:
        e = frontier.pop()
        if e.addr in seen:
            continue
        seen.add(e.addr)
        for nm in forward_idents(e):
            tgt = name_to_row.get(nm)
            if tgt is None or tgt.addr in seen:
                continue
            if tgt.kind == "data table":
                tgt.kind, tgt.desc = "message", tgt.desc
            frontier.append(tgt)

    raw_refs = scan_refs(prog_lines)
    for k, v in scan_refs(vec_lines).items():
        raw_refs.setdefault(k, []).extend(v)

    def entry_refs(e):
        out = []
        for nm in e.names:
            out.extend(raw_refs.get(nm, []))
        # exclude a table/routine's own definition line referencing itself
        # (doesn't happen structurally, but guard anyway)
        return [r for r in out if r[0] != e.addr or r[1] not in JUMP_MNEMONICS | VEC_JUMP_MNEMONICS]

    for e in prog_rows + vec_rows:
        refs = entry_refs(e)
        if e.kind in ("routine", "local entry"):
            e.callers = sum(1 for a, mn in refs if mn in JUMP_MNEMONICS)
        elif e.kind == "alias":
            e.callers = 0
        else:
            e.callers = len(refs)

    # -------------------------------------------------- reconcile totals --
    def kind_count(rows, kinds):
        return sum(1 for e in rows if e.kind in kinds)

    # symbols.json's own name -> kind, for cross-checking by name (addresses
    # can't be compared directly since macasm.py's names are the original
    # source spelling - '.'/'$' unmangled, local "N$" not yet scoped to
    # "ROUTINE_N" - while the listing's names are already mangled).
    sym_kind_of = {}
    for s in symbols:
        sym_kind_of.setdefault(s["name"], s["kind"])

    def name_count(rows, kinds):
        # Count every name shown, except one macasm.py itself classifies as
        # an 'equate' (a same-address alias the listing shows for
        # documentation - e.g. VGROM/PRGROM, BZSTST's own names for the
        # vector/program ROM base used in its checksum range math - not a
        # ROM-resident code/data/local symbol). Local labels ("1$" mangled
        # to e.g. "ROUTINE_1" in the listing) never string-match
        # symbols.json's unmangled name and so are counted as-is.
        return sum(1 for e in rows if e.kind in kinds for n in e.names
                   if sym_kind_of.get(n) != "equate")

    my_prog_named = name_count(prog_rows, {"routine", "local entry", "data table", "message", "alias"})
    my_vec_named = name_count(vec_rows, {"AVG picture", "3D table", "data table", "alias"})

    sym_prog = sum(1 for s in symbols
                   if isinstance(s["addr"], int) and PROG_LO <= s["addr"] < PROG_HI
                   and s["kind"] in ("code", "data", "local"))
    sym_vec = sum(1 for s in symbols
                  if isinstance(s["addr"], int) and VEC_LO <= s["addr"] < VEC_HI
                  and s["kind"] in ("code", "data", "local"))

    problems = []
    if my_prog_named != sym_prog:
        my_names = set()
        for e in prog_rows:
            my_names.update(e.names)
        sym_names = {s["name"] for s in symbols
                     if isinstance(s["addr"], int) and PROG_LO <= s["addr"] < PROG_HI
                     and s["kind"] in ("code", "data", "local")}
        problems.append(
            "program ROM named-location count %d != symbols.json code+data+local count %d "
            "in %s (missing here: %s; extra here: %s)"
            % (my_prog_named, sym_prog, PROG_ASM,
               sorted(sym_names - my_names)[:10], sorted(my_names - sym_names)[:10]))
    if my_vec_named != sym_vec:
        my_names = set()
        for e in vec_rows:
            my_names.update(e.names)
        sym_names = {s["name"] for s in symbols
                     if isinstance(s["addr"], int) and VEC_LO <= s["addr"] < VEC_HI
                     and s["kind"] in ("code", "data", "local")}
        problems.append(
            "vector ROM named-location count %d != symbols.json code+data+local count %d "
            "in %s (missing here: %s; extra here: %s)"
            % (my_vec_named, sym_vec, VEC_ASM,
               sorted(sym_names - my_names)[:10], sorted(my_names - sym_names)[:10]))

    verify_out = run_and_capture("verify.py")
    vm = re.search(r"instructions\s*:\s*(\d+)", verify_out)
    dm = re.search(r"data bytes\s*:\s*(\d+)", verify_out)
    vb = re.search(r"verified bytes\s*:\s*(\d+) of (\d+)", verify_out)
    mm = re.search(r"MISMATCHES\s*:\s*(\d+)", verify_out)
    if not (vm and dm and vb and mm):
        sys.exit("!! could not parse verify.py's output:\n%s" % verify_out)
    if int(mm.group(1)) != 0:
        problems.append("verify.py reports %s MISMATCHES - fix that before FUNCTIONS.md is meaningful" % mm.group(1))
    verify_data_bytes = int(dm.group(1))
    verify_total_bytes = int(vb.group(2))
    # Byte totals are counted the same way verify.py itself does: every
    # "Lxxxx: mnemonic ..." content LINE (not just the ones carrying a
    # label - a routine can have a couple of trailing unlabelled data
    # bytes before the next label, e.g. DISTBL_25 at $551A ends with
    # `.byte $45,$52` right before DISTBL_40's own label), classified data
    # iff its mnemonic is exactly `.byte`/`.word` (verify.py's own rule),
    # sized by the gap to the next content line's address.
    content = sorted({(int(m.group(2), 16), m.group(3).split(None, 1)[0])
                       for m in (ADDR_RE.match(l) for l in prog_lines) if m},
                      key=lambda t: t[0])
    my_prog_data_bytes = my_prog_code_bytes = 0
    for k, (a, mn) in enumerate(content):
        length = (content[k + 1][0] if k + 1 < len(content) else PROG_HI) - a
        if mn in (".byte", ".word"):
            my_prog_data_bytes += length
        else:
            my_prog_code_bytes += length
    if my_prog_data_bytes + my_prog_code_bytes != PROG_HI - PROG_LO:
        problems.append("program-ROM listing byte coverage %d != %d (%d-%d)"
                         % (my_prog_data_bytes + my_prog_code_bytes, PROG_HI - PROG_LO, PROG_LO, PROG_HI))
    if my_prog_data_bytes != verify_data_bytes:
        problems.append("program-ROM data-table byte total %d != verify.py's 'data bytes' %d"
                         % (my_prog_data_bytes, verify_data_bytes))
    if my_prog_code_bytes != verify_total_bytes - verify_data_bytes:
        problems.append("program-ROM code byte total %d != verify.py's (verified bytes - data bytes) %d"
                         % (my_prog_code_bytes, verify_total_bytes - verify_data_bytes))

    vrom_out = run_and_capture("verify_vrom.py")
    vv = re.search(r"(\d+) bytes verified, (\d+) symbols, (\d+) MISMATCHES", vrom_out)
    if not vv:
        sys.exit("!! could not parse verify_vrom.py's output:\n%s" % vrom_out)
    if int(vv.group(3)) != 0:
        problems.append("verify_vrom.py reports %s MISMATCHES" % vv.group(3))

    routine_count = kind_count(prog_rows, {"routine"})
    header_total = routine_count
    prog_refs = load_json("program_refs.json")["routines"]
    if len(prog_refs) != header_total:
        problems.append("routine-header count %d != build/program_refs.json's routines count %d"
                         % (header_total, len(prog_refs)))

    if problems:
        sys.stderr.write("!! FUNCTIONS.md totals do not reconcile - not writing the file:\n")
        for p in problems:
            sys.stderr.write("   - %s\n" % p)
        sys.exit(1)

    # --------------------------------------------------------------- write --
    def name_of(e):
        return e.names[0]

    def aka_of(e):
        return e.names[1:]

    L = ["# Battlezone - routine and data index", "",
         "Every named location in `bzone_program_rom.asm` and "
         "`bzone_vector_rom.asm`, grouped by the source module it came "
         "from and, inside that, by Atari's own `.SBTTL` sections in "
         "source order.", "",
         "GENERATED by `mkfunctions.py` - do not hand-edit. Names and the "
         "one-line descriptions are the listing's own routine-header text "
         "(Atari's source, or `[note]` written for this disassembly); "
         "caller/reference counts are computed by scanning every "
         "instruction operand in both listings for JSR/JMP/branch "
         "(routines) or any reference (data tables, AVG pictures, 3-D "
         "tables). `CALLING_NOTES.md` explains how the pieces fit "
         "together for the routines this index only counts.", ""]

    total = len(prog_rows) + len(vec_rows)
    L += ["%d named locations (%d program ROM, %d vector ROM) across %d modules."
          % (total, len(prog_rows), len(vec_rows), len(MODULE_ORDER)), ""]

    by_module = collections.defaultdict(list)
    for e in prog_rows + vec_rows:
        by_module[e.module if e.module in MODULE_ORDER else "BZMTNS"].append(e)

    L.append("| module | what | named |")
    L.append("|---|---|---|")
    for mod in MODULE_ORDER:
        L.append("| `%s` | %s | %d |" % (mod, MODULE_DESC[mod].split(".")[0],
                                          len(by_module[mod])))
    L.append("")

    kind_totals = collections.Counter(e.kind for e in prog_rows + vec_rows)
    L.append("| kind | count |")
    L.append("|---|---|")
    for k in ("routine", "local entry", "data table", "message", "AVG picture", "3D table", "alias"):
        if kind_totals[k]:
            L.append("| %s | %d |" % (k, kind_totals[k]))
    L.append("")

    for mod in MODULE_ORDER:
        rows = sorted(by_module[mod], key=lambda e: e.addr)
        if not rows:
            continue
        L += ["", "## %s" % mod, "", MODULE_DESC[mod], ""]
        section = None
        cur_file = None
        pending = []

        def flush():
            if not pending:
                return
            L.append("| addr | name | kind | size | refs | description |")
            L.append("|---|---|---|---|---|---|")
            L.extend(pending)
            L.append("")
            del pending[:]

        for e in rows:
            if e.sbttl and e.sbttl != section:
                flush()
                section = e.sbttl
                L += ["### %s" % section.capitalize(), ""]
            if e.file != cur_file and e.file not in (mod + ".MAC",):
                cur_file = e.file
                if cur_file and cur_file != "?":
                    pending.append("| | *(from `%s`)* | | | | |" % cur_file)
            nm = name_of(e)
            aka = aka_of(e)
            nm_disp = "`%s`" % nm
            if aka:
                nm_disp += " (also `%s`)" % "`, `".join(aka)
            desc = (e.desc or e.header_text or "").replace("|", "\\|")
            pending.append("| `$%04X` | %s | %s | %d | %s | %s |"
                            % (e.addr, nm_disp, e.kind, e.size,
                               e.callers if e.callers else "-", desc))
        flush()

    L += ["", "## Totals (reconciled)", "",
          "These numbers are cross-checked at generation time against "
          "`build/symbols.json` (from `macasm_report.md`), "
          "`build/program_refs.json`, `verify.py` and `verify_vrom.py`; "
          "the script exits non-zero and does not write this file if any "
          "of them disagree.", "",
          "| check | this file | tool |",
          "|---|---|---|",
          "| program-ROM named locations (code+data+local) | %d | %d (symbols.json) |"
          % (my_prog_named, sym_prog),
          "| vector-ROM named locations (code+data+local) | %d | %d (symbols.json) |"
          % (my_vec_named, sym_vec),
          "| program-ROM data-table bytes | %d | %d (verify.py) |"
          % (my_prog_data_bytes, verify_data_bytes),
          "| program-ROM code bytes | %d | %d (verify.py) |"
          % (my_prog_code_bytes, verify_total_bytes - verify_data_bytes),
          "| routine headers | %d | %d (program_refs.json) |"
          % (header_total, len(prog_refs)),
          "| program ROM MISMATCHES | 0 | %s (verify.py) |" % mm.group(1),
          "| vector ROM MISMATCHES | 0 | %s (verify_vrom.py) |" % vv.group(3),
          ""]

    open(OUT, "w", encoding="utf-8").write("\n".join(L).rstrip() + "\n")
    print("wrote %s (%d entries: %d program ROM, %d vector ROM; %d routine headers)"
          % (OUT, total, len(prog_rows), len(vec_rows), header_total))


if __name__ == "__main__":
    main()
