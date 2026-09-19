"""Emit disasm/bzone_vector_rom.asm - byte-exact listing of $3000-$3FFF.

Every statement comes from build/srclines.json (Track A's byte-exact
re-assembly of BZMTNS.MAC + VGMC/VGAN), reclassified and independently
decoded from the ROM image by vromsrc.py/avg.py. verify_vrom.py re-checks
the written file completely independently of this script.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import avg, vromsrc, vtables

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "bzone_vector_rom.asm")

HEADER = """;Battlezone (Atari, 1980) - vector ROM $3000-$3FFF, byte-exact listing.
;
;Source: BZMTNS.MAC's own AVG display lists and 3D/lookup tables, with
;VGMC.MAC's vector macros (.INCLUDEd; VGMC.MAC's own header: 'PROGRAMMER:
;ED LOGG') and VGAN.MAC's alphanumeric character set (.INCLUDEd partway
;through; found via macsrc.py's ALT_DIRS in ../red-baron-main; VGAN.MAC's
;own header also says 'PROGRAMMER: ED LOGG'). BZMTNS.MAC's own header
;carries no programmer credit, so the display lists/tables below are shown
;unattributed beyond what BZMTNS.MAC itself states. The vector ROM holds
;three kinds of thing:
;  1. AVG display lists: mountains/horizon (HORIZN, MTNS/MTN0-7), the
;     tank-view crosshair (XCROSS, XCROS1), radar ring (RDRING), tank radar
;     symbol (TANKS/TSYMBL), screen-crack pieces (CRACK0-7/CRACKS), the
;     explosion picture (EXPIC), the self-test/diagnostic screen (HATCH)
;     and the full VGAN alphanumeric character set (CHAR.*).
;  2. 3D object point tables (label: .BYTE offset-to-next-table, then one
;     .NWORD/.MWORD X,Y,Z per point - see build/vector_tables.json) and the
;     OBJPNT pointer array indexing them by object #.
;  3. A few small lookup tables read directly by the 6502 (ARCTAN sine/cos
;     helper, BONTBL/MISLVL difficulty tables, PTBLO1/LOGOBJ).
;No 6502 code executes out of this range: every byte here is either read by
;the AVG chip as an instruction stream, or read by the 6502 as plain table
;data (DRAW/PNTPUT in BZONE.MAC) - vromsrc.py requires every statement's
;mnemonic to be an AVG op or .byte/.word/.NWORD/.MWORD, with no exceptions.
;
;AVG opcodes (see avg.py, derived from VGMC.MAC): VCTR dx,dy,z = long
;vector (4 bytes); SVEC = short vector (2 bytes, VGMC's VCTR macro picks it
;automatically for even |dx|,|dy| <= 30) - both decoded straight from the
;ROM bits, independent of which macro name (VCTR/GVCTR) the source used.
;z 0-7 = beam intensity (0 = blank/move only), straight from VGMC.MAC's own
;comment - Battlezone's AVG is monochrome, no colour RAM. STAT z[,hi,in] =
;$6xxx: z loads the intensity latch; with hi/in given it also sets up a
;clip-window corner (hi = upper-right vs lower-left, in = inside/outside);
;bare 'STAT z' sets the 'not a window' bit ($0400). SCAL b,$ll = binary /
;linear scale. CNTR/RTSL/HALT take no operand (VGMC's macros supply none,
;so the ROM only ever holds their fixed word). JSRL/JMPL operand = the
;target label ((target-$2000)/2 is what the word actually encodes; VG
;address space $2000-$3FFF covers both vector RAM and this ROM).
;
;Labels are Atari's names, mangled '.' -> '_', '$' -> 'S_', local 'NN$' ->
;'SCOPE_NN'. Two Atari names occasionally share one address (an 'end of the
;previous table' bare marker immediately followed by the next table's own
;label, or a later 'CHAR.0 = CHAR.O' alias); the listing shows both but
;names tables/targets after the one build/srclines.json recorded as that
;statement's own label.
;
;Verify with: python disasm/verify_vrom.py
;Re-generate with: python disasm/emit_vrom.py
"""


def fmt_operand_words(mn, words):
    if mn == "VCTR":
        return " ".join("%04X" % w for w in words)
    return " ".join("%04X" % w for w in words)


def picture_note(vt, name):
    return vtables.AVG_PICTURE_NOTES.get(name)


def build_reverse_maps(vt):
    """label -> list of (index, role) where role is 'points' or 'edges'."""
    rev = {}
    for o in vt["objects"]:
        for role, key in (("points", "point_table"), ("edges", "edge_list")):
            nm = o[key]
            if nm:
                rev.setdefault(nm, []).append((o["index"], role))
    return rev


def emit(path=OUT):
    M = vromsrc.Model()
    _, vstmts = vromsrc.all_vstmts(M)
    vt = vtables.build(M)
    rev = build_reverse_maps(vt)

    # cross reference: who JSRL/JMPLs to each vrom label, and who points at
    # each table via a .word pointer (OBJPNT et al, found anywhere in the
    # full program - build/srclines.json covers all modules)
    refs = {}
    for r in M.srclines:
        head = vromsrc.head_of(r["mnemonic_text"])
        if head in ("JSRL", "JMPL"):
            a = r["addr"]
            st = M.stmt_by_addr.get(a)
            if st is None:
                continue
            try:
                mn, oper, ln = avg.decode(M.mem, a)
            except Exception:
                continue
            if mn not in ("JSRL", "JMPL"):
                continue
            t = avg.decode_raw(M.mem, a)["target"]
            refs.setdefault(t, []).append((a, mn, r["file"]))

    L = [HEADER, ".org $3000", "",
         ";----------------------------[ AVG display lists / data tables ]----------------------------"]

    covered = 0x3000
    for v in vstmts:
        assert v.addr == covered, "gap at $%04X (statement at $%04X)" % (covered, v.addr)
        covered = v.addr + v.len
        names = M.names_at(v.addr)
        if names:
            L.append("")
            for i, nm in enumerate(names):
                clabel = vromsrc.mangle(nm)
                bits = []
                note = picture_note(vt, nm)
                if note:
                    bits.append(note)
                if nm in rev:
                    for idx, role in rev[nm]:
                        bits.append("object #%d %s table" % (idx, role))
                rs = refs.get(v.addr, [])
                if rs:
                    bits.append("%d ref%s" % (len(rs), "" if len(rs) == 1 else "s"))
                extra = (" ;" + "; ".join(bits)) if bits else (" ;same address" if i else "")
                L.append("%s:%s%s" % (clabel, " " * max(1, 16 - len(clabel)), extra))
        addr_label = "V%04X" % v.addr
        if v.kind == "avg":
            mn, oper = v.mnemonic, v.operand
            words_hex = fmt_operand_words(mn, v.words)
            note = v.comment
            src_note = ""
            if vromsrc.head_of(v.src) == "GVCTR":
                src_note = "  (src: %s)" % v.src
            full_note = ("  " + note if note else "") + src_note
            L.append(("%s:  %-6s %-22s ;%s%s" % (addr_label, mn, oper, words_hex, full_note)).rstrip())
        else:
            if v.mnemonic == ".byte":
                vals = ", ".join("$%02X" % b for b in v.words)
            else:
                vals = ", ".join("$%04X" % w for w in v.words)
            note = ("  " + v.comment) if v.comment else ""
            L.append("%s:  %-6s %-22s ;%s%s" % (addr_label, v.mnemonic, vals, v.src, note))

    # the 3 ROM-fill bytes at the very end ($3FFD-$3FFF), not emitted by any
    # source statement (confirmed: last statement, LOGOBJ, ends at $3FFD)
    fill = list(M.mem[0x3FFD:0x4000])
    assert covered == 0x3FFD and fill == [0, 0, 0]
    L.append("")
    L.append("V3FFD:  .byte  $%02X, $%02X, $%02X          ;ROM fill - not emitted by any source "
             "statement (BZMTNS.MAC's LOGOBJ, the last table, ends at $3FFD)" % tuple(fill))
    covered = 0x4000
    assert covered == 0x4000

    L += ["", ";----------------------------[ cross reference: JSRL/JMPL into vector ROM ]"
              "----------------------------",
          ";name              addr   referenced from"]
    for a in sorted(refs):
        if not (0x3000 <= a < 0x4000):
            continue
        nm = M.target_name(a)
        items = ["$%04X %s (%s)" % (r, mn, f) for r, mn, f in refs[a]]
        chunks, cur = [], ""
        for it in items:
            if cur and len(cur) + len(it) + 2 > 100:
                chunks.append(cur + ",")
                cur = it
            else:
                cur = (cur + ", " + it) if cur else it
        chunks.append(cur)
        L.append(";%-17s $%04X  %s" % (nm, a, chunks[0]))
        for c in chunks[1:]:
            L.append(";%-17s        %s" % ("", c))

    open(path, "w", encoding="ascii", errors="replace").write("\n".join(L) + "\n")
    return len(vstmts)


if __name__ == "__main__":
    n = emit()
    print("wrote", OUT)
    print("  statements: %d" % n)
