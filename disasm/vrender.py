"""AVG display-list interpreter for Battlezone vector-ROM pictures.

Walks an AVG subroutine starting at a JSRL/JMPL target (or any label),
maintaining beam position/scale/intensity exactly as the AVG chip would,
and collects the lit line segments. Used by emit_shapes.py to draw the
mountains/horizon, crosshair, radar ring, tank symbol, cracks, hatch/self-
test screen, explosion picture and the VGAN character set.

Per VGMC.MAC's own comment: "ZZ specifies the beam intensity (0 through 7,
0 is no intensity, 7 is brightest)" - Battlezone's AVG has no colour RAM,
so Z is used directly as the segment's brightness (no Tempest-style
'Z==1 means use the last STAT value' substitution, which is specific to
the colour AVG boards - Battlezone's STAT sets the intensity latch for the
*next* CNTR-anchored window/reference, not per-vector brightness).
"""
import os
import avg

HERE = os.path.dirname(os.path.abspath(__file__))

MAX_STEPS = 4000
MAX_DEPTH = 6


def clean(name):
    if name is None:
        return name
    return name.replace(".", "_").replace("$", "S_")


def walk(mem, start, M=None, max_steps=MAX_STEPS, max_depth=MAX_DEPTH):
    """Execute AVG instructions from `start` (a byte address in $2000-$3FFF)
    until RTSL/HALT or the step budget runs out. Returns:
      segs: [(x0,y0,x1,y1,z)]   every VCTR/SVEC, lit or not (z=0 = blank)
      dots: [(x,y)]             (Battlezone's AVG has no explicit dot op;
                                  kept for API symmetry with vtables' TDOT)
      info: dict(steps, calls, halted, jsrl_targets)
    Beam starts centred at (0,0); CNTR resets to (0,0); JSRL pushes a return
    address (vector-RAM/ROM call stack, 5 levels on real hardware - we don't
    bound it beyond max_depth, which is generous)."""
    segs, jsrl_targets = [], set()
    x = y = 0.0
    scale = 1.0
    steps = 0
    stack = []
    a = start
    halted = False
    while steps < max_steps:
        steps += 1
        if a is None or a < 0x2000 or a >= 0x4000:
            break
        d = avg.decode_raw(mem, a)
        op = d["op"]
        if op in ("VCTR", "SVEC"):
            dx, dy, z = d["dx"] * scale, d["dy"] * scale, d["z"]
            nx, ny = x + dx, y + dy
            segs.append((x, y, nx, ny, z))
            x, y = nx, ny
            a += d["len"]
        elif op == "CNTR":
            x = y = 0.0
            a += d["len"]
        elif op == "SCAL":
            scale = avg.scal_factor(d["b"], d["l"])  # SCAL 0,$00 -> ~1.0 (full size)
            a += d["len"]
        elif op == "STAT":
            a += d["len"]
        elif op == "JSRL":
            jsrl_targets.add(d["target"])
            if len(stack) < max_depth:
                stack.append(a + d["len"])
                a = d["target"]
            else:
                a += d["len"]
        elif op == "JMPL":
            jsrl_targets.add(d["target"])
            a = d["target"]
        elif op == "RTSL":
            if stack:
                a = stack.pop()
            else:
                break
        elif op == "HALT":
            halted = True
            break
        else:
            break
    return dict(segs=segs, info=dict(steps=steps, halted=halted,
                jsrl_targets=sorted(jsrl_targets)))


def bbox(segs):
    xs = [p for s in segs for p in (s[0], s[2])]
    ys = [p for s in segs for p in (s[1], s[3])]
    if not xs:
        return (0, 0, 0, 0)
    return (min(xs), min(ys), max(xs), max(ys))


# -------------------------------------------------------------- projection
def project_oblique(x, y, z, angle_deg=45.0, k=0.5):
    """Simple fixed oblique (cavalier/cabinet-style) projection: screen X =
    x + k*z*cos(angle), screen Y = y + k*z*sin(angle) (z toward viewer along
    a diagonal). Used as the static fallback when JS auto-rotate is off."""
    import math
    a = math.radians(angle_deg)
    return x + k * z * math.cos(a), y + k * z * math.sin(a)
