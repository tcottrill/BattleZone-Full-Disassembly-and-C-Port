"""Battlezone data tables needed to render 3D objects and AVG pictures.

Two kinds of table matter here:

  * 3D point tables (BZMTNS.MAC, vector ROM $3000-$3FFF): a label, one
    '.BYTE nextlabel-thislabel' offset byte, then N '.NWORD X,Y,Z' (or, for
    the BATTLEZONE logo letters, '.MWORD X,Y,Z' - same 3-word-per-point
    layout, but X,Y are scaled x4 and Z only x2) triples - one 6-byte group
    per 3D point. OBJPNT ($3888, BZMTNS.MAC:485) is a 44-entry .WORD
    pointer array, one pointer per object type, indexed by "object #".

  * Edge/connection lists (OBJTBL, BZONE.MAC:4399 - program ROM, *not* in
    $3000-$3FFF, but build/srclines.json covers the whole program so it is
    read the same way as everything else): a 44-entry .WORD pointer array,
    index-for-index parallel to OBJPNT, each pointing at a byte-opcode list
    that DRAW (BZONE.MAC:1864, the 'D.TOP' computed-goto dispatcher) walks:
      opcode = byte & 7, point index = (byte & ^HF8) >> 1 (i.e. index*8,
      so at most 31 points per object)
        0 TDOT   idx   - draw a dot at point idx
        1 SBRITE val   - set brightness (val = high nibble, val&^HF0)
        2 BVCTR  idx   - move (blank vector) to point idx
        3 TLABS  idx   - reset current position to point idx (new polyline)
        4 TVCTR  idx   - draw a lit vector to point idx (edge)
        5 TSPCL        - insert the EXPIC explosion AVG picture (10 point flashes)
        6 TSLIM        - end of this vector list (falls through)
        $FF            - OBJEND, end of the whole per-object command list
    A TLABS/TVCTR/BVCTR run is one polyline: TLABS sets the pen-up start
    point, each following TVCTR draws an edge to the next point (implicit
    connection to the *previous* point in the list), BVCTR lifts the pen
    and moves without drawing (starts a new disconnected segment), TDOT
    draws a single dot (no line). An object can hold several such runs
    (Battlezone's tank, saucer, radar-dish table etc. all draw 2-3 separate
    polylines with SBRITE brightness changes between them).

This module resolves both arrays through vromsrc.Model (symbols + srclines,
independent of macasm.py's own output) and writes build/vector_tables.json.
"""
import json, os, re
import avg, vromsrc

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "build", "vector_tables.json")
H = lambda a: "$%04X" % a

EDGE_OP_RE = re.compile(r"^(TLABS|TVCTR|BVCTR|TDOT|SBRITE|TSPCL|TSLIM|OBJEND)\s*(\S*)$")

# Best-effort common names for the 44 OBJPNT/OBJTBL slots, from reading how
# BZONE.MAC uses each object # (GAME.MAC-level names are not in the source,
# so these are descriptions, not Atari identifiers - the ROM label is always
# shown alongside). Where the game meaning is genuinely unclear the note
# says so instead of guessing.
OBJECT_NOTES = {
    0: "Pyramid (terrain obstacle)",
    1: "Cube / block (terrain obstacle)",
    2: "Enemy tank",
    3: "Cannon shell (player/enemy shot, 5-point pyramid shape)",
    4: "Tank tread piece, animation frame 4",
    5: "Tank tread piece, animation frame 5",
    6: "Tank tread piece, animation frame 6",
    7: "Tank tread piece, animation frame 7",
    8: "Tank tread piece, animation frame 8",
    9: "Tank tread piece, animation frame 9",
    10: "Tank tread piece, animation frame A",
    11: "Tank tread piece, animation frame B",
    12: "Rock / boulder, pyramid form (reuses PYROBJ edges)",
    13: "Radar blip marker",
    14: "Explosion origin (TSPCL - inserts the EXPIC AVG picture, no point geometry of its own)",
    15: "Rock / boulder, block form (reuses CUBOBJ edges)",
    16: "Explosion debris, piece shape 1",
    17: "Explosion debris, piece shape 2",
    18: "Explosion debris, piece shape 0",
    19: "Radar blip marker (reused)",
    20: "Explosion debris, piece shape 2 (reused)",
    21: "Explosion debris, piece shape 1 (reused)",
    22: "The R2D3 Buzz Bomb (BZONE.MAC:3190-3196, '.SBTTL R2D3 BUZZ BOMBER ... "
        "CONTROLS MOVEMENT OF R2D3-BUZZ-BOMB ... FLIES THE BUZZ BOMB DIRECTLY AT "
        "PLAYER, AT HIGH SPEED, SWOOPING IN ON HIM' - the flying homing enemy, "
        "often called 'the missile' by players); MISLVL, referenced by BUZBOM, is "
        "the same difficulty table BZMTNS.MAC stores in the vector ROM.",
    23: "BATTLEZONE logo, letters part 1 (BATBL)",
    24: "Explosion debris, piece shape 2 (reused)",
    25: "Explosion debris, piece shape 4",
    26: "Explosion debris, piece shape 1 (reused)",
    27: "Explosion debris, piece shape 3",
    28: "Explosion debris, piece shape 1 (reused)",
    29: "Explosion debris, piece shape 4 (reused)",
    30: "BATTLEZONE logo, letters part 2 (TLETBL)",
    31: "BATTLEZONE logo, word ZONE (ZONTBL)",
    32: "Flying saucer (bonus UFO)",
    33: "The TR7 supertank (BZONE.MAC:3690-3696, '.SBTTL CHECK FOR HIGH POWERED "
        "TANK ROBOT ... BLOCK NAME: TR7CHK ... DETERMINES IF WE HAVE OR SHOULD "
        "HAVE THE TR7 INSTEAD OF THE OLD CLVUKER' - TR7CHK returns carry set once "
        "NOR2D3 >= 5, swapping the ordinary tank for this one).",
    34: "(unused - 0,0)",
    35: "(unused - 0,0)",
    36: "Explosion particle ring, frame 0 (8 dots, smallest radius)",
    37: "Explosion particle ring, frame 1",
    38: "Explosion particle ring, frame 2",
    39: "Explosion particle ring, frame 3",
    40: "Explosion particle ring, frame 4",
    41: "Explosion particle ring, frame 5",
    42: "Explosion particle ring, frame 6",
    43: "Explosion particle ring, frame 7 (8 dots, largest radius)",
}

AVG_PICTURE_NOTES = {
    "HORIZN": "Horizon line (single long VCTR)",
    "MTNS": "Mountain range picture - calls all 8 MTN0-MTN7 skyline segments",
    "MTN0": "Mountain/skyline segment 0 (includes the moon)",
    "MTN1": "Mountain/skyline segment 1", "MTN2": "Mountain/skyline segment 2",
    "MTN3": "Mountain/skyline segment 3", "MTN4": "Mountain/skyline segment 4",
    "MTN5": "Mountain/skyline segment 5", "MTN6": "Mountain/skyline segment 6",
    "MTN7": "Mountain/skyline segment 7",
    "CIRCLE": "Small circle (bullet point glyph, used by CPYRIT/PNTNT)",
    "CPYRIT": "Copyright symbol (c) + circle",
    "PNTNT": "'Patent pending'-style circled glyph",
    "EXPIC": "Explosion picture, 10 point flashes (GVCTR-scaled x4)",
    "XCROSS": "Gunsight / crosshair (tank view)",
    "XCROS1": "Gunsight / crosshair, alternate (with extra tick marks)",
    "RDRING": "Radar ring",
    "TANKS": "Tank symbol (radar blip glyph)",
    "TSYMBL": "Tank symbol entry point (calls TANKS)",
    "CRACK0": "Screen-crack piece 0 (collision effect)",
    "CRACK1": "Screen-crack piece 1", "CRACK2": "Screen-crack piece 2",
    "CRACK3": "Screen-crack piece 3", "CRACK4": "Screen-crack piece 4",
    "CRACK5": "Screen-crack piece 5", "CRACK6": "Screen-crack piece 6",
    "CRACK7": "Screen-crack piece 7",
    "CRACKS": "Screen-crack picture - calls all 8 CRACK0-CRACK7 pieces",
    "HATCH": "Self-test / diagnostic screen (grid, scale test, brightness bars)",
}


def nword_points(M, header_addr, header_val, mwordish=False):
    """Walk the .NWORD/.MWORD statements right after a table's .BYTE offset
    header and return the point list, scaled back to real coordinates."""
    npts = max(0, (header_val - 1) // 6)
    pts = []
    a = header_addr + 1
    for _ in range(npts):
        st = M.stmt_at(a)
        if st is None or st["len"] != 6:
            break
        w = [avg.word(M.mem, a), avg.word(M.mem, a + 2), avg.word(M.mem, a + 4)]

        def s16(v):
            v &= 0xFFFF
            return v - 0x10000 if v & 0x8000 else v
        sw = [s16(x) for x in w]
        head = st["mnemonic_text"].split(None, 1)[0].upper()
        if head == ".MWORD":
            x, y, z = sw[0] / 4.0, sw[1] / 4.0, sw[2] / 2.0
        else:
            x, y, z = sw[0] / 4.0, sw[1] / 4.0, sw[2] / 4.0
        pts.append(dict(addr=H(a), words=["$%04X" % v for v in w], x=x, y=y, z=z,
                        source=st["mnemonic_text"]))
        a += 6
    return pts


def point_table(M, name):
    addr = M.addr_of(name)
    if addr is None:
        return None
    st = M.stmt_at(addr)
    if st is None or not st["mnemonic_text"].upper().startswith(".BYTE"):
        return dict(name=name, addr=H(addr), points=[], note="no .BYTE offset header found")
    header_val = M.mem[addr]
    pts = nword_points(M, addr, header_val)
    return dict(name=name, addr=H(addr), header_byte=header_val, count=len(pts), points=pts)


def edge_list(M, name):
    """Walk one OBJTBL entry's byte-opcode list (BZONE.MAC:1864 DRAW/D.TOP).

    Besides the descriptive 'ops'/'polylines' (kept for the JSON dump),
    this also tracks the running SBRITE value (BZONE.MAC:1907-1918: D.SBRT
    sets VGSTAT's brightness from the opcode byte's high nibble, and every
    TVCTR/TDOT after it draws at that brightness until the next SBRITE) and
    emits a flat 'segments' list - one entry per drawn edge or dot, each
    carrying the brightness value in effect - for renderers that want
    per-edge brightness without re-deriving it from the polyline grouping."""
    ops, polylines, segments, cur = [], [], [], None
    brite = None
    a = addr = M.addr_of(name)
    if addr is None:
        return None
    while True:
        st = M.stmt_at(a)
        if st is None:
            break
        m = EDGE_OP_RE.match(st["mnemonic_text"].strip())
        if not m:
            break
        op, arg = m.group(1), m.group(2)
        idx = int(arg, 16) if arg else None
        ops.append(dict(addr=H(a), op=op, index=idx))
        if op == "TLABS":
            cur = [idx]
            polylines.append(dict(kind="line", brite=brite, points=cur))
        elif op == "TVCTR":
            if cur is None:
                cur = []
                polylines.append(dict(kind="line", brite=brite, points=cur))
            segments.append(dict(kind="line", a=cur[-1] if cur else idx, b=idx, brite=brite))
            cur.append(idx)
        elif op == "BVCTR":
            cur = [idx]
            polylines.append(dict(kind="line", brite=brite, points=cur))
        elif op == "TDOT":
            polylines.append(dict(kind="dot", brite=brite, points=[idx]))
            segments.append(dict(kind="dot", a=idx, b=idx, brite=brite))
        elif op == "SBRITE":
            brite = idx
            polylines.append(dict(kind="brite", value=idx, points=[]))
        elif op == "TSPCL":
            polylines.append(dict(kind="special", points=[]))
        elif op == "TSLIM":
            pass
        elif op == "OBJEND":
            a += st["len"]
            break
        a += st["len"]
    return dict(name=name, addr=H(addr), end=H(a), ops=ops, polylines=polylines,
                segments=segments,
                brite_format="SBRITE's opcode byte is 1|(value&^HF0) (BZONE.MAC:4233-4235); "
                    "DRAW's D.SBRT handler (BZONE.MAC:1895-1919) turns that high nibble into the "
                    "VGSTAT intensity for every TVCTR/TDOT drawn until the next SBRITE (default, "
                    "when an object's list has none, is whatever brightness the caller left set).")


def objects(M):
    objpnt_addr = M.addr_of("OBJPNT")
    objtbl_addr = M.addr_of("OBJTBL")
    n = 44
    pnt_words = [avg.word(M.mem, objpnt_addr + 2 * k) for k in range(n)]
    tbl_words = [avg.word(M.mem, objtbl_addr + 2 * k) for k in range(n)]
    out = []
    tables_cache, edges_cache = {}, {}
    for i in range(n):
        pa, ta = pnt_words[i], tbl_words[i]
        pname = M.target_name(pa) if pa else None
        tname = M.target_name(ta) if ta else None
        if pa and pname not in tables_cache:
            tables_cache[pname] = point_table(M, pname)
        if ta and tname not in edges_cache:
            edges_cache[tname] = edge_list(M, tname)
        out.append(dict(index=i, point_table=pname, edge_list=tname,
                        note=OBJECT_NOTES.get(i, "")))
    return out, tables_cache, edges_cache


def build(M=None):
    M = M or vromsrc.Model()
    objs, tables, edges = objects(M)
    picture_labels = sorted(
        n for n in AVG_PICTURE_NOTES if M.addr_of(n) is not None)
    pictures = []
    for n in picture_labels:
        a = M.addr_of(n)
        pictures.append(dict(name=n, addr=H(a), note=AVG_PICTURE_NOTES.get(n, "")))
    vgan_chars = []
    for e in M.symbols:
        nm = e.get("name") or ""
        if nm.startswith("CHAR.") and 0x3000 <= e.get("addr", -1) < 0x4000:
            vgan_chars.append(dict(name=vromsrc.mangle(nm), addr=H(e["addr"]), source_name=nm))
    vgan_chars.sort(key=lambda d: d["addr"])

    misc_tables = {}
    for nm in ("ARCTAN", "BONTBL", "MISLVL", "PTBLO1", "LOGOBJ", "EXPTBX", "EXPTBY", "IZVEL",
               "CHKS0", "CHKS1"):
        a = M.addr_of(nm)
        if a is None:
            continue
        st = M.stmt_at(a)
        misc_tables[nm] = dict(addr=H(a), first_statement=st["mnemonic_text"] if st else None)

    d = dict(
        rom="bzone_cpu64k.bin", generated_by="disasm/vtables.py",
        vector_rom_range="$3000-$3FFF",
        point_table_format="label: .BYTE <next_label>-<this_label> (byte offset to the next "
            "table; (offset-1)/6 = point count), then one .NWORD X,Y,Z (BZMTNS.MAC tables) or "
            ".MWORD X,Y,Z (BATBL/TLETBL/ZONTBL logo letters, BZONE.MAC) triple per 3D point. "
            "Each .NWORD word is stored as 4*value; .MWORD stores X,Y as 4*value but Z as 2*value. "
            "OBJPNT ($%04X, BZMTNS.MAC:485) is the 44-entry .WORD pointer array, one per object #."
            % M.addr_of("OBJPNT"),
        edge_list_format="OBJTBL ($%04X, BZONE.MAC:4399, program ROM) is a 44-entry .WORD pointer "
            "array parallel to OBJPNT. Each entry points at a byte-opcode list (DRAW / D.TOP, "
            "BZONE.MAC:1864): byte&7 selects the opcode (0 TDOT idx, 1 SBRITE val, 2 BVCTR idx, "
            "3 TLABS idx, 4 TVCTR idx, 5 TSPCL, 6 TSLIM), byte&^HF8 encodes point_index*8 "
            "(0..31), terminated by $FF (OBJEND). TLABS starts a new polyline at point idx; "
            "TVCTR draws an edge from the previous point to idx; BVCTR moves without drawing; "
            "TDOT plots a single point; TSPCL splices in the EXPIC AVG picture instead of point "
            "geometry." % M.addr_of("OBJTBL"),
        objects=objs,
        point_tables=tables,
        edge_lists=edges,
        avg_pictures=pictures,
        vgan_characters=vgan_chars,
        misc_tables=misc_tables,
    )
    return d


if __name__ == "__main__":
    d = build()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    json.dump(d, open(OUT, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    print("objects: %d, point tables: %d, edge lists: %d, AVG pictures: %d, VGAN chars: %d"
          % (len(d["objects"]), len(d["point_tables"]), len(d["edge_lists"]),
             len(d["avg_pictures"]), len(d["vgan_characters"])))
    for o in d["objects"]:
        pt = d["point_tables"].get(o["point_table"]) if o["point_table"] else None
        npts = pt["count"] if pt else 0
        print("  #%-2d pnt=%-8s edg=%-8s pts=%-3d %s" % (
            o["index"], o["point_table"], o["edge_list"], npts, o["note"][:60]))
    print("wrote", OUT)
