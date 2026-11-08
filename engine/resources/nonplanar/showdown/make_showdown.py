#!/usr/bin/env python3
"""Parametric showdown parts (docs/nonplanar/SHOWDOWN.md): the parts tilted-plane and curved-layer
slicers are shown off with, generated rather than committed.

    python3 -I resources/nonplanar/showdown/make_showdown.py --out <dir> [--only name,...]

Writes <name>.stl per part and showdown.json (name, kind, description). Units mm, lowest point at
z = 0, every part fits the MINI's 180 mm bed. The parts that also exist as public meshes on this
box (the real-model gallery: helmet, car shell, spoon, phone stand, ...) are listed in
showdown.json under `gallery` and read from there by scripts/nonplanar/showdown.py.
"""
import argparse
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "scripts" / "nonplanar_bench"))
import _env  # noqa: E402,F401
import numpy as np  # noqa: E402

import geometry as g  # noqa: E402

PARTS = {}


def part(kind, desc):
    def wrap(fn):
        PARTS[fn.__name__] = (fn, kind, desc)
        return fn
    return wrap


def rect(x0, x1, y0, y1, fn, pitch=0.5):
    return g.rect_solid(x0, x1, y0, y1, int(round((x1 - x0) / pitch)), int(round((y1 - y0) / pitch)), fn)


def disk(r, fn, pitch=0.5):
    return g.disk_solid(0, 0, r, int(round(r / pitch)), max(96, int(2 * math.pi * r / pitch)), fn)


@part("classic", "Slanted-top box 50 x 40, top rising 12 deg along X, 8-18.6 mm")
def slanted_box():
    return rect(-25, 25, -20, 20, lambda x, y: 13.3 + math.tan(math.radians(12)) * x, pitch=1.0)


@part("classic", "Slanted-top box 50 x 40, top rising 25 deg along X")
def slanted_box_25():
    return rect(-25, 25, -20, 20, lambda x, y: 16.0 + math.tan(math.radians(25)) * x, pitch=1.0)


@part("classic", "Wavy coaster: disc R45, 4 mm base, 1.4 mm egg-crate waves, 18 mm period")
def wavy_coaster():
    k = 2 * math.pi / 18
    return disk(45, lambda x, y: 5.4 + 1.4 * np.sin(k * x) * np.cos(k * y))


@part("classic", "Barrel roof tile 80 x 50: 8 deg pitch along X, two 25 mm barrels across Y")
def roof_tile():
    t = math.tan(math.radians(8))
    return rect(-40, 40, -25, 25, lambda x, y: 9 + t * x + 3.5 * np.cos(math.pi * y / 25) ** 2)


@part("classic", "Topographic map 110 x 90: five Gaussian hills, 3-24 mm, slopes to ~45 deg")
def topo_map():
    hills = [(-25, -15, 14, 15), (20, 18, 18, 12), (30, -25, 9, 8), (-30, 25, 10, 10), (5, -5, 6, 20)]

    def z(x, y):
        out = np.full_like(x, 3.0)
        for cx, cy, s, h in hills:
            out = out + h * np.exp(-((x - cx) ** 2 + (y - cy) ** 2) / (2 * s * s))
        return out
    return rect(-55, 55, -45, 45, z, pitch=0.6)


@part("classic", "Knob: R16 x 10 mm cylinder with an R22 spherical dome, edge 47 deg")
def dome_knob():
    R, r = 22.0, 16.0
    zc = 10 - math.sqrt(R * R - r * r)
    return disk(r, lambda x, y: zc + np.sqrt(np.maximum(R * R - x * x - y * y, 0)), pitch=0.4)


@part("unlayered", "Ergonomic grip 120 x 34: superelliptic hump, four 1.6 mm finger grooves")
def ergo_grip():
    def z(x, y):
        u, v = x / 60, y / 17
        base = 6 + 16 * np.sqrt(np.clip(1 - u ** 4, 0, 1)) * np.sqrt(np.clip(1 - v ** 2, 0, 1)) ** 0.6
        return base - 1.6 * np.clip(np.cos(2 * math.pi * (x + 9) / 24), 0, 1) ** 2 * (np.abs(x) < 42)
    xs, ys = np.meshgrid(np.linspace(-60, 60, 241), np.linspace(-1, 1, 69), indexing="ij")
    px, py = xs, ys * 17 * np.sqrt(np.clip(1 - (xs / 60.5) ** 6, 0.04, 1))
    return g.grid_solid(px, py, z(px, py), 0.0)


@part("unlayered", "Computer mouse shell 110 x 62, hump 34 mm toward the palm")
def mouse_shell():
    a, b = 55.0, 31.0

    def z(x, y):
        u = (x + 12) / (a + 12 * np.sign(x + 12))
        s = np.clip(1 - u ** 2 - (y / b) ** 2, 0, 1)
        return 4 + 30 * s ** 0.7
    xs, ps = np.meshgrid(np.linspace(-1, 1, 221), np.linspace(-1, 1, 125), indexing="ij")
    px = xs * a
    py = ps * b * np.sqrt(np.clip(1 - xs ** 2, 0.02, 1))
    return g.grid_solid(px, py, z(px, py), 0.0)


@part("unlayered", "Curved container lid R50: 4 mm rim, 7 mm shallow dome, R6 centre knob")
def curved_lid():
    def z(x, y):
        r = np.hypot(x, y)
        dome = 4 + 7 * np.clip(1 - (r / 50) ** 2, 0, 1)
        knob = 3 * np.sqrt(np.clip(1 - (r / 6) ** 2, 0, 1))
        return dome + knob
    return disk(50, z)


@part("unlayered", "Airfoil (NACA 2412, chord 90) lying on its flat side, span 70")
def airfoil_wing():
    upper, lower = g.naca4_profile("2412", 90.0, 300)
    xs = np.linspace(0.3, 89.7, 300)
    zu = np.interp(xs, upper[:, 0], upper[:, 1])
    zl = np.interp(xs, lower[:, 0], lower[:, 1])
    th = np.maximum(zu - zl, 0)
    return rect(0.3, 89.7, -35, 35, lambda x, y: 1.2 + np.interp(x, xs, th) * 1.6, pitch=0.5)


@part("unlayered", "Raised text 'ORCA' 0.8 mm proud on a R40 cylindrical top, 90 x 44")
def text_curve():
    glyphs = {"O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
              "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
              "C": ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
              "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"]}
    px_mm, word = 2.4, "ORCA"
    bits = np.array([[c == "1" for c in "0".join(glyphs[ch][row] for ch in word)] for row in range(7)])
    w, h = bits.shape[1] * px_mm, 7 * px_mm

    def z(x, y):
        base = 8 + np.sqrt(40 ** 2 - y ** 2) - math.sqrt(40 ** 2 - 22 ** 2)
        i = np.floor((x + w / 2) / px_mm).astype(int)
        j = np.floor((h / 2 - y) / px_mm).astype(int)
        ok = (i >= 0) & (i < bits.shape[1]) & (j >= 0) & (j < 7)
        on = np.zeros_like(x, bool)
        on[ok] = bits[j[ok], i[ok]]
        return base + 0.8 * on
    return rect(-45, 45, -22, 22, z, pitch=0.2)


@part("unlayered", "Swept duct: R12 half-pipe swept 90 deg around an R50 bend, on a 2 mm plinth")
def swept_duct():
    s, t = np.meshgrid(np.linspace(0, math.pi / 2, 181), np.linspace(-1, 1, 97), indexing="ij")
    rc = 50 + 12 * t
    px, py = rc * np.cos(s) - 25, rc * np.sin(s) - 25
    return g.grid_solid(px, py, 2 + 12 * np.sqrt(np.clip(1 - t ** 2, 0, 1)) ** 0.9, 0.0)


@part("gm", "Curved hook (J profile) 15 mm thick: 60 mm stem, R18 curl, overhanging arc")
def curved_hook():
    inner = [(21 + 15 * math.cos(a), 50 + 15 * math.sin(a)) for a in np.linspace(math.pi, 0, 41)]
    outer = [(21 + 21 * math.cos(a), 50 + 21 * math.sin(a)) for a in np.linspace(0, math.pi, 41)]
    prof = [(0, 0), (6, 0)] + inner + [(36, 44), (42, 44)] + outer
    return g.extrude_xz(prof[:-1] + [(0, 50)], -7.5, 7.5)


@part("gm", "Shelf bracket: 8 mm upright, 70 mm arm with an R60 convex top, 50 deg gusset underside")
def overhang_bracket():
    top = [(x, 40 + 6 * math.sin(math.pi * x / 70)) for x in np.linspace(70, 0, 36)]
    prof = [(0, 0), (8, 0), (8, 6), (8 + 26 * math.tan(math.radians(40)), 32), (70, 32)] + top
    return g.extrude_xz(prof, -10, 10)


@part("flat90", "Wall shelf: 8 x 30 x 25 mm wall, 3 mm shelf 20 mm out at its top, flat 90 deg underside at 22 mm")
def wall_shelf():
    return g.extrude_xz([(0, 0), (8, 0), (8, 22), (28, 22), (28, 25), (0, 25)], -15, 15)


@part("flat90", "T-table: 12 x 12 x 20 mm post, 42 x 42 x 3 mm plate, 15 mm flat 90 deg overhang on every side")
def t_table():
    post, plate, z0, z1 = 6.0, 21.0, 20.0, 23.0
    sq = lambda r, z: [(-r, -r, z), (r, -r, z), (r, r, z), (-r, r, z)]
    v = np.array(sq(post, 0) + sq(post, z0) + sq(plate, z0) + sq(plate, z1), float)
    f = [(0, 2, 1), (0, 3, 2), (12, 13, 14), (12, 14, 15)]  # post bottom, plate top
    for i in range(4):
        j = (i + 1) % 4
        f += [(i, j, 4 + j), (i, 4 + j, 4 + i)]            # post sides
        f += [(8 + i, 8 + j, 12 + j), (8 + i, 12 + j, 12 + i)]  # plate sides
        f += [(4 + i, 4 + j, 8 + j), (4 + i, 8 + j, 8 + i)]     # plate underside, the ring round the post
    return g.Mesh(v, f)


@part("slope", "Frustum with one face per slope: 15, 30, 40 and 50 deg sides, 20 x 20 flat top")
def slope_frustum():
    sl = [15, 30, 40, 50]  # +X, +Y, -X, -Y

    def z(x, y):
        H = 16.0
        out = np.full_like(x, H)
        for k, (dx, dy) in enumerate([(1, 0), (0, 1), (-1, 0), (0, -1)]):
            d = np.maximum(dx * x + dy * y - 10, 0)
            out = np.minimum(out, H - d * math.tan(math.radians(sl[k])))
        return np.maximum(out, 2.0)
    return rect(-40, 40, -30, 30, z, pitch=0.5)


@part("slope", "Cone R30 rising 38 deg to a rounded tip on a 2 mm plinth")
def cone_spike():
    t = math.tan(math.radians(38))
    return disk(30, lambda x, y: 2 + t * (30 - np.sqrt(x * x + y * y + 9)), pitch=0.4)


@part("unlayered", "Half torus (donut) R30, tube R10, on a 0.6 mm disc")
def donut():
    def z(x, y):
        d = np.hypot(x, y) - 30
        return np.sqrt(np.clip(100 - d * d, 0, None)) * 1.0 + 0.6
    return disk(40, z, pitch=0.4)


@part("classic", "Gable roof 70 x 50, 26.6 deg sides meeting in a rounded (hyperbolic) ridge")
def gable_round():
    return rect(-35, 35, -25, 25, lambda x, y: 20 - 0.5 * (np.sqrt(y * y + 49) - 7), pitch=0.5)


GALLERY = ["bowl", "cable_clip", "car_shell", "chamfer_box", "grooved_lid", "helmet_half", "knob", "lens",
           "phone_stand", "propeller", "spoon", "spot", "teapot", "text_plaque"]
FIXTURES = ["dome", "saddle"]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    only = set(filter(None, a.only.split(",")))
    meta = {"generated": {}, "gallery": GALLERY, "fixtures": FIXTURES}
    for name, (fn, kind, desc) in PARTS.items():
        meta["generated"][name] = {"kind": kind, "description": desc}
        if only and name not in only:
            continue
        m = fn().welded(1e-6)
        m.v[:, 2] -= m.v[:, 2].min()
        bad = m.check_closed()
        g.write_stl(m, a.out / (name + ".stl"), name)
        lo, hi = m.bbox()
        print("%-18s %7d tris  %5.1f x %5.1f x %5.1f  open edges %d" % (name, len(m.f), *(hi - lo), bad))
    (a.out / "showdown.json").write_text(json.dumps(meta, indent=1))


if __name__ == "__main__":
    main()
