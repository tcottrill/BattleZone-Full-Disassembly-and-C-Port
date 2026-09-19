"""Generate disasm/bzone_shapes.html (plus build/vector_tables.json): a
single self-contained page (inline SVG/CSS/JS, no external resources,
light/dark friendly) that draws every vector picture the $3000-$3FFF ROM
describes:

  1. the AVG display-list pictures (mountains/horizon, crosshair, radar
     ring, tank radar symbol, screen cracks, explosion picture, self-test
     screen, copyright/circle glyphs) - walked with vrender.walk(), exactly
     as the AVG chip would execute them (JSRL/SCAL/CNTR honoured). The
     canvas auto-fits to where the beam is actually lit (not to blank-move
     extent) and draws a zero-length lit VCTR as a dot, so point-flash
     pictures (EXPIC) and pictures made of short strokes joined by long
     blank jumps (RDRING) are still visible; each card's note says how many
     lit vectors/point-flashes/blank moves it decoded to;
  2. the full VGAN alphanumeric character set, each cell labelled with its
     Atari name, the character it draws and its ROM address;
  3. every 3D object (obstacles, tank, tank treads, radar blip, explosion
     debris, saucer, the R2D3 buzz bomb, the TR7 supertank, the BATTLEZONE
     logo letters) rendered as a wireframe straight from its OBJPNT point
     table and OBJTBL edge list (vtables.py), with per-edge brightness from
     the edge list's SBRITE opcodes (two stroke opacities). Default view is
     a fixed oblique projection with the object's height axis pointing up
     and a front-quarter-from-slightly-above camera angle (see the axis
     comment above build_objects() below for the BZONE.MAC citations);
     mouse-drag rotates about the height axis, plus an auto-rotate toggle.

No invented geometry: every coordinate comes from the ROM bytes (via
vtables.py, which reads the ROM image directly).
"""
import html, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import avg, vromsrc, vrender, vtables

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "bzone_shapes.html")
BUILD = os.path.join(HERE, "build")

PICTURES = [
    ("HORIZN", "Horizon line"),
    ("MTNS", "Mountain range (all 8 skyline segments + moon)"),
    ("XCROSS", "Gunsight / crosshair"),
    ("XCROS1", "Gunsight / crosshair, alternate"),
    ("RDRING", "Radar ring"),
    ("TSYMBL", "Tank radar symbol"),
    ("CRACKS", "Screen cracks (all 8 pieces)"),
    ("EXPIC", "Explosion picture (Atari's own comment: '16/16 PICTURE OF EXPLOSION')"),
    ("HATCH", "Self-test / diagnostic screen"),
    ("CIRCLE", "Circle glyph"),
    ("CPYRIT", "Copyright glyph"),
    ("PNTNT", "'Patent pending' glyph"),
]


def segs_json(segs):
    return [[round(x0, 2), round(y0, 2), round(x1, 2), round(y1, 2), z] for x0, y0, x1, y1, z in segs]


def compose_note(segs):
    """Say what an AVG picture is made of: BZMTNS.MAC's own vector count is
    not otherwise visible on the card, and a picture built entirely from
    zero-length 'dot' vectors (z>0 but dx=dy=0, e.g. EXPIC) or from lit
    strokes much shorter than the blank moves between them (e.g. RDRING's
    arcs) needs explaining, not just a small drawing."""
    lit_lines = dots = blanks = 0
    for x0, y0, x1, y1, z in segs:
        if z <= 0:
            blanks += 1
        elif x0 == x1 and y0 == y1:
            dots += 1
        else:
            lit_lines += 1
    parts = []
    if lit_lines:
        parts.append("%d lit vector%s" % (lit_lines, "" if lit_lines == 1 else "s"))
    if dots:
        parts.append("%d point flash%s (zero-length VCTR, z>0)" % (dots, "" if dots == 1 else "es"))
    if blanks:
        parts.append("%d blank move%s (z=0)" % (blanks, "" if blanks == 1 else "s"))
    return ", ".join(parts) if parts else "no vectors"


def build_pictures(M):
    out = []
    for nm, note in PICTURES:
        addr = M.addr_of(nm)
        if addr is None:
            continue
        r = vrender.walk(M.mem, addr, M)
        full_note = "%s - %s" % (note, compose_note(r["segs"]))
        out.append(dict(name=vromsrc.mangle(nm), addr="$%04X" % addr, note=full_note,
                        segs=segs_json(r["segs"])))
    return out


def build_chars(M, vt):
    out = []
    for c in vt["vgan_characters"]:
        addr = int(c["addr"][1:], 16)
        r = vrender.walk(M.mem, addr, M)
        ch = c["source_name"][5:] if c["source_name"].startswith("CHAR.") else "?"
        out.append(dict(name=c["name"], addr=c["addr"], char=ch, segs=segs_json(r["segs"])))
    return out


# --------------------------------------------------------------------------
# 3D axis order: which of the 3 .NWORD/.MWORD words is "up"?
#
# BZONE.MAC's PNTPUT (the routine that turns a point table into screen
# points) reads each point as 3 words and feeds them to the Mathbox:
#   BZONE.MAC:1993-2024  X/Y matrix setup: the table's word0/word1 are
#     copied to XMATRX and YMATRX/YSTRT2 and rotated together by the
#     object's heading (TANGLE/PTBLO2+1, BZONE.MAC:1994-2011) - these two
#     share one 2D rotation, i.e. they are the ground-plane axes.
#   BZONE.MAC:2049-2056  "STA ZX,PNTTBL ;C MOVE X COORD TO TABLE" - word0's
#     result is explicitly labelled the X coordinate.
#   BZONE.MAC:2057-2070  the table's word2 is copied to DDEND, then passed
#     through KLUDGE (BZONE.MAC:2083-2100: "DESCRIPTION: WILL ADD
#     APPROPRIATE Z OFFSET FOR EXPLOSION OBJECTS", "INPUT PARAMS: DDEND,
#     PTBLO2,EXPOSZ") - KLUDGE's own comment calls word2 "Z" and adds
#     EXPOSZ, the explosion piece's height-over-time, to it - i.e. word2 is
#     the vertical axis, not part of the ground-plane rotation.
# The ROM data agrees: PYRTBL's 4 base points all share word2=-80 and only
# the apex (word2=+80) differs; TNKTBL's hull rectangles step from
# word2=-80 (bottom, the tracks) up through -52,-30,12 to +20 (the highest
# points, the hatch/turret detail near the back).
#
# So a table row (word0, word1, word2) is (X, Y, Z) with Z = height/up -
# vtables.py already keeps that order (x=word0, y=word1, z=word2); the
# JS projection below (rotate()) uses p[2] as the screen-vertical axis.
def build_objects(vt):
    out = []
    for o in vt["objects"]:
        pname, ename = o["point_table"], o["edge_list"]
        if not pname or not ename:
            continue
        pt = vt["point_tables"].get(pname)
        el = vt["edge_lists"].get(ename)
        if not pt or not el:
            continue
        points = [[p["x"], p["y"], p["z"]] for p in pt["points"]]
        segments = [dict(kind=s["kind"], a=s["a"], b=s["b"], brite=s["brite"])
                    for s in el["segments"]]
        out.append(dict(index=o["index"], name=pname, edge=ename, note=o["note"],
                        npts=len(points), points=points, segments=segments))
    return out


CSS = """
:root {
  --bg: #f4f5f7; --fg: #1a1d23; --card: #ffffff; --border: #d6d9e0;
  --muted: #6b7280; --accent: #2563eb; --vec: #1a1d23; --vec-dim: #94a3b8;
}
@media (prefers-color-scheme: dark) {
  :root { --bg: #14161a; --fg: #e6e8eb; --card: #1d2026; --border: #33373f;
          --muted: #9aa2af; --accent: #60a5fa; --vec: #e6e8eb; --vec-dim: #4b5563; }
}
* { box-sizing: border-box; }
body { margin: 0; padding: 16px; background: var(--bg); color: var(--fg);
       font: 14px/1.4 -apple-system, Segoe UI, Helvetica, Arial, sans-serif; }
h1 { font-size: 20px; margin: 0 0 4px; }
h2 { font-size: 16px; margin: 28px 0 10px; border-bottom: 1px solid var(--border); padding-bottom: 6px; }
p.sub { color: var(--muted); margin: 0 0 16px; max-width: 900px; }
.bar { display: flex; gap: 12px; align-items: center; flex-wrap: wrap; margin: 10px 0 4px;
       color: var(--muted); font-size: 13px; }
.bar label { display: inline-flex; gap: 4px; align-items: center; cursor: pointer; }
.grid { display: flex; flex-wrap: wrap; gap: 10px; }
.card { background: var(--card); border: 1px solid var(--border); border-radius: 6px;
        padding: 8px; width: 132px; text-align: center; }
.card.big { width: 220px; }
.card canvas { background: transparent; display: block; margin: 0 auto; touch-action: none; cursor: grab; }
.card .nm { font-weight: 600; margin-top: 4px; word-break: break-word; }
.card .addr { color: var(--muted); font-size: 11px; }
.card .note { color: var(--muted); font-size: 11px; margin-top: 2px; }
footer { color: var(--muted); font-size: 12px; margin-top: 30px; }
code { background: var(--border); padding: 1px 4px; border-radius: 3px; }
"""


def emit(path=OUT):
    M = vromsrc.Model()
    vt = vtables.build(M)
    os.makedirs(BUILD, exist_ok=True)
    json.dump(vt, open(os.path.join(BUILD, "vector_tables.json"), "w", encoding="utf-8"),
              indent=1, ensure_ascii=False)

    pictures = build_pictures(M)
    chars = build_chars(M, vt)
    objects = build_objects(vt)

    data = dict(pictures=pictures, chars=chars, objects=objects)
    data_json = json.dumps(data, ensure_ascii=False)

    html_out = HTML_TEMPLATE.replace("__CSS__", CSS).replace("__DATA__", data_json) \
        .replace("__NPIC__", str(len(pictures))).replace("__NCHAR__", str(len(chars))) \
        .replace("__NOBJ__", str(len(objects)))
    open(path, "w", encoding="utf-8").write(html_out)
    return len(pictures), len(chars), len(objects)


HTML_TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Battlezone vector ROM shapes</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>__CSS__</style>
</head>
<body>
<h1>Battlezone (Atari, 1980) &mdash; vector ROM $3000-$3FFF shapes</h1>
<p class="sub">Every AVG display-list picture, the VGAN character set, and every 3D object
decoded straight from the ROM image (see <code>disasm/bzone_vector_rom.asm</code>,
<code>disasm/vtables.py</code>). __NPIC__ AVG pictures, __NCHAR__ characters, __NOBJ__ 3D
objects. No invented geometry &mdash; every coordinate is a ROM byte.</p>

<h2>AVG display lists</h2>
<div class="grid" id="pictures"></div>

<h2>VGAN character set</h2>
<div class="grid" id="chars"></div>

<h2>3D objects</h2>
<div class="bar">
  <label><input type="checkbox" id="autorotate"> auto-rotate</label>
  <span>drag a shape to rotate it &middot; double-click to reset</span>
</div>
<div class="grid" id="objects"></div>

<footer>Generated by disasm/emit_shapes.py from _survey/roms_extracted/bzone_cpu64k.bin.
Point/edge table format: build/vector_tables.json.</footer>

<script>
const DATA = __DATA__;

function fitAndDraw(ctx, w, h, bbox, drawFn) {
  const [minx, miny, maxx, maxy] = bbox;
  const bw = Math.max(1, maxx - minx), bh = Math.max(1, maxy - miny);
  const pad = 10;
  const scale = Math.min((w - 2 * pad) / bw, (h - 2 * pad) / bh);
  const cx = w / 2, cy = h / 2;
  const midx = (minx + maxx) / 2, midy = (miny + maxy) / 2;
  ctx.clearRect(0, 0, w, h);
  const vec = getComputedStyle(document.body).getPropertyValue('--vec').trim();
  const vecDim = getComputedStyle(document.body).getPropertyValue('--vec-dim').trim();
  drawFn((x, y) => [cx + (x - midx) * scale, cy - (y - midy) * scale], vec, vecDim);
}

// Bounding box of what is actually drawn: prefer the extent of the LIT
// (z>0) strokes over the full extent including blank (z=0) moves. Some
// pictures (e.g. RDRING) position several short bright arcs with long
// blank jumps between them - fitting to the jumps' extent would shrink the
// arcs to invisibility, so fit to where the beam is actually lit instead.
function segBBox(segs) {
  const lit = segs.filter(s => s[4] > 0);
  const src = lit.length ? lit : segs;
  let minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
  for (const [x0, y0, x1, y1] of src) {
    minx = Math.min(minx, x0, x1); maxx = Math.max(maxx, x0, x1);
    miny = Math.min(miny, y0, y1); maxy = Math.max(maxy, y0, y1);
  }
  if (minx > maxx) return [-1, -1, 1, 1];
  if (maxx - minx < 4) { const cx = (minx + maxx) / 2; minx = cx - 4; maxx = cx + 4; }
  if (maxy - miny < 4) { const cy = (miny + maxy) / 2; miny = cy - 4; maxy = cy + 4; }
  return [minx, miny, maxx, maxy];
}

function drawSegs(canvas, segs) {
  const ctx = canvas.getContext('2d');
  const w = canvas.width, h = canvas.height;
  fitAndDraw(ctx, w, h, segBBox(segs), (proj, vec) => {
    for (const [x0, y0, x1, y1, z] of segs) {
      if (z <= 0) continue;
      const [sx0, sy0] = proj(x0, y0), [sx1, sy1] = proj(x1, y1);
      ctx.strokeStyle = vec; ctx.fillStyle = vec;
      ctx.globalAlpha = Math.max(0.25, z / 7);
      ctx.lineWidth = 1.2;
      // a zero-length lit VCTR (dx=dy=0, z>0) is a point flash (e.g.
      // EXPIC's 16/16 explosion picture) - a canvas line can't show that,
      // so draw a small dot instead.
      if (Math.abs(sx0 - sx1) < 0.4 && Math.abs(sy0 - sy1) < 0.4) {
        ctx.beginPath(); ctx.arc(sx0, sy0, 1.6, 0, 2 * Math.PI); ctx.fill();
      } else {
        ctx.beginPath(); ctx.moveTo(sx0, sy0); ctx.lineTo(sx1, sy1); ctx.stroke();
      }
    }
    ctx.globalAlpha = 1;
  });
}

function makeCard(container, cls, w, h) {
  const card = document.createElement('div');
  card.className = 'card ' + cls;
  const canvas = document.createElement('canvas');
  canvas.width = w; canvas.height = h;
  card.appendChild(canvas);
  container.appendChild(card);
  return { card, canvas };
}

function addLabel(card, lines) {
  for (const [cls, text] of lines) {
    const d = document.createElement('div');
    d.className = cls;
    d.textContent = text;
    card.appendChild(d);
  }
}

// static cards (pictures + chars) are re-drawn on a light/dark switch too
const staticCards = [];

// ---------------------------------------------------------------- pictures
const picGrid = document.getElementById('pictures');
for (const p of DATA.pictures) {
  const { card, canvas } = makeCard(picGrid, '', 104, 104);
  drawSegs(canvas, p.segs);
  staticCards.push([canvas, p.segs]);
  addLabel(card, [['nm', p.name], ['addr', p.addr], ['note', p.note]]);
}

// ---------------------------------------------------------------- chars
const charGrid = document.getElementById('chars');
for (const c of DATA.chars) {
  const { card, canvas } = makeCard(charGrid, '', 72, 84);
  drawSegs(canvas, c.segs);
  staticCards.push([canvas, c.segs]);
  addLabel(card, [
    ['nm', c.name],
    ['note', "draws '" + (c.char || ' ') + "'"],
    ['addr', c.addr],
  ]);
}

// ---------------------------------------------------------------- 3D objects
// Point order is (X, Y, Z) = (ground-plane, ground-plane, HEIGHT) - see the
// build_objects() comment in emit_shapes.py for the BZONE.MAC citations.
// ry = yaw about the vertical/height axis Z (mixes X,Y only - dragging
// left/right spins the object in place, matching how the game itself
// rotates objects about their vertical axis by heading). rx = a camera
// pitch that blends ground-plane depth (y1) into the screen-vertical
// coordinate along with height (z), so a positive rx looks down at the
// object from slightly above (its far/forward side lifts toward the top
// of the card) without needing real hidden-line removal or a perspective
// divide - a fixed oblique projection, exactly as for the AVG pictures.
function rotate(p, rx, ry) {
  const [x, y, z] = p;
  const cy = Math.cos(ry), sy = Math.sin(ry);
  const x1 = x * cy - y * sy;
  const y1 = x * sy + y * cy;
  const cx = Math.cos(rx), sx = Math.sin(rx);
  const screenUp = z * cx + y1 * sx;
  const depth = y1 * cx - z * sx;
  return [x1, screenUp, depth];
}

// SBRITE (BZONE.MAC:4233-4235) packs its value in the opcode byte's high
// nibble; D.SBRT (BZONE.MAC:1895-1919) turns that into the AVG intensity.
// Two stroke opacities is enough to show it: $C0 and up reads as 'bright'.
function briteAlpha(v) {
  if (v === null || v === undefined) return 0.55;
  return v >= 0xC0 ? 1.0 : 0.55;
}

function objectSegs(obj, rx, ry) {
  const proj = obj.points.map(p => rotate(p, rx, ry));
  const segs = [];
  for (const s of obj.segments) {
    const alpha = briteAlpha(s.brite);
    const z = alpha >= 1 ? 7 : 4; // drawSegs() maps z -> alpha = max(.25, z/7)
    if (s.kind === 'dot') {
      const [x, y] = proj[s.a];
      segs.push([x - 1.5, y, x + 1.5, y, z]);
      segs.push([x, y - 1.5, x, y + 1.5, z]);
    } else {
      const [x0, y0] = proj[s.a];
      const [x1, y1] = proj[s.b];
      segs.push([x0, y0, x1, y1, z]);
    }
  }
  return segs;
}

const objGrid = document.getElementById('objects');
const OBJ_DEFAULT_RX = 0.5, OBJ_DEFAULT_RY = 0.65;
const objState = [];
for (const obj of DATA.objects) {
  const { card, canvas } = makeCard(objGrid, 'big', 200, 170);
  const st = { rx: OBJ_DEFAULT_RX, ry: OBJ_DEFAULT_RY, canvas, obj };
  objState.push(st);
  addLabel(card, [
    ['nm', '#' + obj.index + ' ' + obj.name],
    ['addr', obj.edge + ' \\u00b7 ' + obj.npts + ' pts'],
    ['note', obj.note],
  ]);
  let dragging = false, lastX = 0, lastY = 0;
  canvas.addEventListener('mousedown', e => { dragging = true; lastX = e.clientX; lastY = e.clientY; canvas.style.cursor = 'grabbing'; });
  window.addEventListener('mouseup', () => { dragging = false; canvas.style.cursor = 'grab'; });
  window.addEventListener('mousemove', e => {
    if (!dragging) return;
    st.ry += (e.clientX - lastX) * 0.01;
    st.rx += (e.clientY - lastY) * 0.01;
    lastX = e.clientX; lastY = e.clientY;
    drawSegs(canvas, objectSegs(obj, st.rx, st.ry));
  });
  canvas.addEventListener('dblclick', () => {
    st.rx = OBJ_DEFAULT_RX; st.ry = OBJ_DEFAULT_RY;
    drawSegs(canvas, objectSegs(obj, st.rx, st.ry));
  });
  canvas.addEventListener('touchstart', e => { dragging = true; lastX = e.touches[0].clientX; lastY = e.touches[0].clientY; }, { passive: true });
  window.addEventListener('touchend', () => { dragging = false; });
  window.addEventListener('touchmove', e => {
    if (!dragging || !e.touches.length) return;
    st.ry += (e.touches[0].clientX - lastX) * 0.01;
    st.rx += (e.touches[0].clientY - lastY) * 0.01;
    lastX = e.touches[0].clientX; lastY = e.touches[0].clientY;
    drawSegs(canvas, objectSegs(obj, st.rx, st.ry));
  }, { passive: true });
  drawSegs(canvas, objectSegs(obj, st.rx, st.ry));
}

const autorotate = document.getElementById('autorotate');
function tick() {
  if (autorotate.checked) {
    for (const st of objState) {
      st.ry += 0.012;
      drawSegs(st.canvas, objectSegs(st.obj, st.rx, st.ry));
    }
  }
  requestAnimationFrame(tick);
}
requestAnimationFrame(tick);

// the CSS custom properties (--vec/--vec-dim) already follow the OS theme
// via @media (prefers-color-scheme); redraw every canvas so the stroke
// colour picked up by getComputedStyle() matches the new theme immediately
matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => {
  for (const [canvas, segs] of staticCards) drawSegs(canvas, segs);
  for (const st of objState) drawSegs(st.canvas, objectSegs(st.obj, st.rx, st.ry));
});
</script>
</body>
</html>
"""

if __name__ == "__main__":
    npic, nchar, nobj = emit()
    print("pictures: %d, characters: %d, 3D objects: %d" % (npic, nchar, nobj))
    print("wrote", OUT)
