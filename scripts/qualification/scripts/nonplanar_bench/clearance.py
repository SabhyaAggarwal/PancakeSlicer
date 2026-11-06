"""Independent chronological clearance sweep, a Python port of curveslice `mini_screen`
(atomized/src/atom/clearance.py) generalised to orca.head_envelope.v1 heads.

It is the benchmark's check on workstream E's C++ verifier and shares no code with it. Semantics,
as in mini_screen:

- Every move, travel or extrusion, is split into sub-edges of at most `sweep` mm. For each sub-edge
  and each head box, the box is swept over the sub-edge's XY bounds and placed at the sub-edge's
  lowest Z. Its floor is compared with the highest material in the covered cells.
- Material is conservative max-height columns on a `pitch` grid: a deposited sub-edge raises every
  cell its bead footprint (XY bounds +- width/2) touches to the sub-edge's top Z. Voids are filled.
- A sub-edge deposits after it has been checked, so a move is never checked against material
  printed after it.
- Non-nozzle boxes are widened by `lateral_margin` and their floors lowered by `margin`. Boxes that
  fit inside the tip-contact allowance cylinder start at the allowance height (mini_screen's rule).
- `infinite_x` boxes span the whole grid in X, `fixed_x` boxes (curveslice) use absolute X.
- Unlike mini_screen, nozzle boxes do not see the current extrusion run (consecutive extruding
  moves with no travel between them), which is the bead being laid (ARCHITECTURE.md §6). Without
  this, the flat land's trailing edge "hits" its own bead on every downhill move steeper than
  atan(allowance_height / allowance_radius). `exempt_current_run=False` restores mini_screen.

Hits are potential interference: the model is conservative and may over-report near corners.
"""
import math

import numpy as np

TILE = 16


class Columns:
    def __init__(self, lo, hi, pitch):
        self.lo = np.asarray(lo, float)
        self.pitch = pitch
        shape = np.maximum(1, np.ceil((np.asarray(hi, float) - self.lo) / pitch).astype(int))
        self.z = np.full(tuple(shape), -np.inf)
        self.tiles = np.full(tuple((shape + TILE - 1) // TILE), -np.inf)
        self.ceiling = -np.inf

    def _cells(self, lo, hi):



        nx, ny = self.z.shape
        ax = max(0, math.floor((lo[0] - self.lo[0]) / self.pitch))
        ay = max(0, math.floor((lo[1] - self.lo[1]) / self.pitch))
        bx = min(nx, math.floor((min(hi[0], self.lo[0] + nx * self.pitch) - self.lo[0]) / self.pitch) + 1)
        by = min(ny, math.floor((min(hi[1], self.lo[1] + ny * self.pitch) - self.lo[1]) / self.pitch) + 1)
        return (ax, ay, bx, by) if bx > ax and by > ay else None

    def top(self, lo, hi):
        cells = self._cells(lo, hi)
        if cells is None:
            return -np.inf
        ax, ay, bx, by = cells
        t = self.tiles[ax // TILE:(bx - 1) // TILE + 1, ay // TILE:(by - 1) // TILE + 1]
        tmax = t.max()
        if tmax == -np.inf:
            return tmax
        return float(self.z[ax:bx, ay:by].max())

    def tile_top(self, lo, hi):
        cells = self._cells(lo, hi)
        if cells is None:
            return -np.inf
        ax, ay, bx, by = cells
        return float(self.tiles[ax // TILE:(bx - 1) // TILE + 1, ay // TILE:(by - 1) // TILE + 1].max())

    def deposit(self, lo, hi, top):
        cells = self._cells(lo, hi)
        if cells is None:
            return
        ax, ay, bx, by = cells
        s = self.z[ax:bx, ay:by]
        np.maximum(s, top, out=s)
        t = self.tiles[ax // TILE:(bx - 1) // TILE + 1, ay // TILE:(by - 1) // TILE + 1]
        np.maximum(t, top, out=t)
        self.ceiling = max(self.ceiling, top)


def extrusion_runs(moves):
    """Run id per move: increments at every non-extruding move."""
    return np.cumsum(~moves.extruding)


def prepared_boxes(head):
    """(name, lo, hi, infinite_x, infinite_y, fixed_x, margin, nozzle) as the sweep applies them."""
    out = []
    for b in head.boxes:
        lo, hi = (np.array(v, float) for v in head.widened(b))
        if not (b.fixed_x or b.infinite_x or b.infinite_y):
            radius = math.hypot(max(abs(lo[0]), abs(hi[0])), max(abs(lo[1]), abs(hi[1])))
            if radius <= head.allowance_radius:
                lo[2] = max(lo[2], head.allowance_height)
        if lo[2] < hi[2]:
            out.append((b.name, lo, hi, b.infinite_x, b.infinite_y, b.fixed_x, 0.0 if b.nozzle else head.margin,
                        b.nozzle))
    return out


def sweep(moves, head, pitch=0.25, sweep_step=0.25, max_samples=50_000_000, exempt_current_run=True):
    """Run the chronological screen. `moves` is gcode.Moves. Returns a report dict."""
    boxes = prepared_boxes(head)
    nozzle = np.array([exempt_current_run and x[7] for x in boxes], bool)
    if not len(moves):
        return {"ran": True, "hit_moves": [], "hit_lines": [], "per_move": [], "component_hits": {},
                "min_clearance": math.inf, "samples": 0}
    reach = max([max(np.abs(lo[:2]).max(), np.abs(hi[:2]).max()) for _, lo, hi, ix, iy, fx, _, _ in boxes
                 if not (ix or iy or fx)] + [1.0])
    pts = np.vstack([moves.a[:, :2], moves.b[:, :2]])
    glo, ghi = pts.min(0) - reach - 2, pts.max(0) + reach + 2
    for _, lo, hi, ix, iy, fx, _, _ in boxes:
        if fx:
            glo[0], ghi[0] = min(glo[0], lo[0]), max(ghi[0], hi[0])
    cols = Columns(glo, ghi, pitch)
    prev = Columns(glo, ghi, pitch) if nozzle.any() else cols
    pending = []
    floors = np.array([lo[2] - m for _, lo, _, _, _, _, m, _ in boxes])
    lowest_floor = floors.min() if len(floors) else math.inf
    per_move = np.full(len(moves), np.inf)
    component_hits, hit_rows, first = {}, [], []
    samples = 0
    known_bead = (moves.width > 0) & (moves.height > 0)
    unmodelled = int((moves.extruding & ~known_bead).sum())



    a_all, b_all, w_all, ext_all = moves.a, moves.b, moves.width, moves.extruding & known_bead


    own = np.flatnonzero(moves.extruding & ~moves.role_mask({"Custom"}))
    print_start = int(own[0]) if len(own) else len(moves)
    for i in range(len(moves)):
        a, b = a_all[i], b_all[i]
        if not ext_all[i] and pending:
            for args in pending:
                prev.deposit(*args)
            pending.clear()
        n = max(1, math.ceil(np.linalg.norm(b - a) / sweep_step))
        samples += n
        if samples > max_samples:
            raise RuntimeError("clearance sample budget exceeded")
        check = min(a[2], b[2]) + lowest_floor < cols.ceiling - 1e-5
        r = 0.5 * w_all[i]
        hit = False
        worst = math.inf
        for j in range(n):
            p0 = a + (b - a) * (j / n)
            p1 = a + (b - a) * ((j + 1) / n)
            xylo, xyhi = np.minimum(p0[:2], p1[:2]), np.maximum(p0[:2], p1[:2])
            zmin = min(p0[2], p1[2])
            if check:
                for k, (name, lo, hi, ix, iy, fx, m, _) in enumerate(boxes):
                    floor = zmin + floors[k]
                    if cols.ceiling <= floor + 1e-5:
                        continue
                    qlo = np.array([cols.lo[0] if ix else (lo[0] if fx else xylo[0] + lo[0]),
                                    cols.lo[1] if iy else xylo[1] + lo[1]])
                    qhi = np.array([np.inf if ix else (hi[0] if fx else xyhi[0] + hi[0]),
                                    np.inf if iy else xyhi[1] + hi[1]])
                    src = prev if nozzle[k] else cols
                    if src.tile_top(qlo, qhi) <= floor + 1e-5:
                        continue
                    top = src.top(qlo, qhi)
                    if i >= print_start:
                        worst = min(worst, floor - top)
                    if top > floor + 1e-5 and i >= print_start:
                        component_hits[name] = component_hits.get(name, 0) + 1
                        if not hit and len(first) < 64:
                            first.append({"move": i, "line": int(moves.line[i]), "component": name,
                                          "overlap_mm": round(top - floor, 4), "tip": p1.round(3).tolist()})
                        hit = True
            if ext_all[i]:
                cols.deposit(xylo - r, xyhi + r, max(p0[2], p1[2]))
                if prev is not cols:
                    pending.append((xylo - r, xyhi + r, max(p0[2], p1[2])))
                check = check or min(a[2], b[2]) + lowest_floor < cols.ceiling - 1e-5
        per_move[i] = worst
        if hit:
            hit_rows.append(i)
    finite = per_move[np.isfinite(per_move)]
    return {"ran": True, "hit_moves": hit_rows, "hit_lines": sorted({int(moves.line[i]) for i in hit_rows}),
            "per_move": per_move, "component_hits": component_hits, "first_hits": first,
            "min_clearance": float(finite.min()) if len(finite) else math.inf, "samples": samples,
            "pitch_mm": pitch, "sweep_mm": sweep_step, "head": head.id,
            "unmodelled_extrusion_moves": unmodelled,
            "scope": "continuous sub-edge AABB sweep vs chronological max-height columns; voids filled; "
                     "potential hits can be false positives",
            "omissions": ["bed and machine self-collision", "tilting tool (rotary) poses",
                          "support and thermal process", "head geometry accuracy (calibrated=%s)" % head.calibrated]
                         + (["Deposition of %d extrusion moves without width/height tags (startup purge); all moves are still checked" % unmodelled] if unmodelled else [])}
