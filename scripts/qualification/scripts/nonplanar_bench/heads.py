"""Head envelopes for the benchmark: an independent Python reading of "orca.head_envelope.v1"
(ARCHITECTURE.md §2.3) and of curveslice's envelopes.tsv, plus the plane slope limit
(INTERFACES.md §2, HeadEnvelope::max_slope). It deliberately shares no code with libslic3r.
"""
import json
import math
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

INF = float("inf")


@dataclass
class Box:
    name: str
    lo: np.ndarray
    hi: np.ndarray
    infinite_x: bool = False
    infinite_y: bool = False
    nozzle: bool = False
    fixed_x: bool = False


@dataclass
class Head:
    id: str
    boxes: list
    tip_flat_radius: float = 0.5
    allowance_radius: float = 0.0
    allowance_height: float = 0.0
    margin: float = 0.3
    lateral_margin: float = 0.3
    name: str = ""
    calibrated: bool = False
    provenance: str = ""
    extra: dict = field(default_factory=dict)

    def widened(self, b):
        """Box as consumers see it: non-nozzle boxes widened sideways by lateral_margin."""
        if b.nozzle or self.lateral_margin == 0:
            return b.lo, b.hi
        d = np.array([self.lateral_margin, self.lateral_margin, 0.0])
        return b.lo - d, b.hi + d

    def underside_height(self, d):
        """b(d): lowest box floor over horizontal offset d from the tip. margin not applied."""
        best = INF
        for b in self.boxes:
            lo, hi = self.widened(b)
            if (b.infinite_x or lo[0] <= d[0] <= hi[0]) and (b.infinite_y or lo[1] <= d[1] <= hi[1]):
                best = min(best, lo[2])
        return best

    def reach(self):
        r = 0.0
        for b in self.boxes:
            if b.infinite_x or b.infinite_y:
                continue
            lo, hi = self.widened(b)
            r = max(r, math.hypot(max(abs(lo[0]), abs(hi[0])), max(abs(lo[1]), abs(hi[1]))))
        return r

    def max_slope(self, phi, gap):
        """Steepest plane through the tip rising toward `phi` that clears the head, radians.

        The plane extends sideways, so a box point p at height z collides once
        tan(alpha) > (gap + z - margin) / (p . u); over a box the floor is lowest and p . u is
        extreme at a footprint corner. Nozzle boxes take no margin, infinite boxes are skipped (a
        plane rises without bound; gantry limits live in the minorant), and the tip allowance is not
        applied (the plane is previous material). Same reading as HeadEnvelope::max_slope."""
        u = np.array([math.cos(phi), math.sin(phi)])
        best = INF
        for b in self.boxes:
            if b.infinite_x or b.infinite_y:
                continue
            lo, hi = self.widened(b)
            proj = [np.dot(c, u) for c in ((lo[0], lo[1]), (hi[0], hi[1]), (lo[0], hi[1]), (hi[0], lo[1]))]
            pmin, pmax = min(proj), max(proj)
            if pmax <= 0:
                continue
            num = gap + lo[2] - (0.0 if b.nozzle else self.margin)
            if num >= 0:
                best = min(best, num / pmax)
            else:
                best = min(best, num / pmin) if pmin > 0 else -INF
        return math.pi / 2 if best == INF else math.atan(max(0.0, best))

    def slope_map(self, n, gap):
        return [self.max_slope(2 * math.pi * k / n, gap) for k in range(n)]


def head_from_dict(d):
    if d.get("schema") != "orca.head_envelope.v1":
        raise ValueError("unsupported head schema %r" % d.get("schema"))
    nz = d.get("nozzle", {})
    boxes = []
    if d.get("sides", 0):
        raise ValueError("N-side profiles are not supported by the benchmark port")
    top = float(d.get("side_profile_top", 100.0))
    for i, row in enumerate(sorted(d.get("side_profile", []), key=lambda r: r["height"])):
        boxes.append(Box("profile_%g" % row["height"], np.array([-row["xneg"], -row["yneg"], row["height"]], float),
                         np.array([row["xpos"], row["ypos"], top], float), nozzle=(i == 0 and row["height"] == 0)))
    for b in d.get("boxes", []):
        boxes.append(Box(b["name"], np.array(b["min"], float), np.array(b["max"], float),
                         bool(b.get("infinite_x")), bool(b.get("infinite_y")), bool(b.get("nozzle"))))
    return Head(d["id"], boxes, float(nz.get("tip_flat_radius", 0.5)), float(nz.get("allowance_radius", 0.0)),
                float(nz.get("allowance_height", 0.0)), float(d.get("margin", 0.3)),
                float(d.get("lateral_margin", 0.3)), d.get("name", ""), bool(d.get("calibrated", False)),
                d.get("provenance", ""))


def load_head(path):
    return head_from_dict(json.loads(Path(path).read_text()))


def head_from_envelopes_tsv(path, allowance_radius=3.0, allowance_height=1.0, margin=0.2):
    """curveslice envelopes.tsv (`name lo.xyz hi.xyz fixed_x`), read as mini_screen does: boxes
    whose names start with "nozzle" get no margin, fixed_x parts keep absolute X."""
    boxes = []
    for line in Path(path).read_text().splitlines():
        f = line.split()
        if len(f) != 8:
            raise ValueError("bad envelope row: %r" % line)
        boxes.append(Box(f[0], np.array(f[1:4], float), np.array(f[4:7], float),
                         nozzle=f[0].startswith("nozzle"), fixed_x=f[7] == "1"))
    return Head("curveslice-" + Path(path).stem, boxes, 0.5, allowance_radius, allowance_height, margin, 0.0)


def generic_conservative():
    """INTERFACES.md §2 HeadLibrary::generic_conservative(): a 15 deg four-sided pyramid with a
    20 mm gantry, written as stepped rows that cover the pyramid (row k reaches row k+1's height)."""
    t = math.tan(math.radians(15))
    rows = [{"height": 0.0, "xpos": 0.5, "xneg": 0.5, "ypos": 0.5, "yneg": 0.5}]
    for h in np.arange(0.25, 20.0, 0.25):
        r = (h + 0.25) / t
        rows.append({"height": float(h), "xpos": r, "xneg": r, "ypos": r, "yneg": r})
    return head_from_dict({"schema": "orca.head_envelope.v1", "id": "generic-conservative",
                           "nozzle": {"tip_flat_radius": 0.5, "allowance_radius": 0.75, "allowance_height": 0.1},
                           "margin": 0.05, "lateral_margin": 0.0, "side_profile": rows, "side_profile_top": 20.0,
                           "boxes": [{"name": "x_gantry", "min": [0, -1000, 20], "max": [0, 1000, 80],
                                      "infinite_x": True}]})


def find_head(name, search=()):
    """`name` is a path or a library id looked up in resources/nonplanar/heads (workstream A)."""
    p = Path(name)
    if p.suffix == ".json" and p.exists():
        return load_head(p)
    if name in ("", "generic-conservative"):
        for d in search:
            q = Path(d) / "generic-conservative.json"
            if q.exists():
                return load_head(q)
        return generic_conservative()
    for d in search:
        q = Path(d) / (name + ".json")
        if q.exists():
            return load_head(q)
    raise FileNotFoundError("head %r not found in %s" % (name, [str(s) for s in search]))
