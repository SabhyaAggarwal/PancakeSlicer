#!/usr/bin/env python3
"""Benchmark metrics from a G-code file and the analytic part (PLAN.md "Acceptance metrics").

    python3 -I scripts/nonplanar_bench/metrics.py out.gcode --parts parts.json --part dome \\
        [--head prusa-mk4-nextruder] [--clearance] [--json metrics.json] [--grid-out err.npz]

Everything comes from the G-code text and parts.json; nothing from slicer internals.

- Deposited top: per 0.25 mm cell, the highest bead top (nozzle Z) of any extrusion whose bead
  footprint (path +- width/2) covers the cell centre. `top_error` uses every extrusion role: it is
  the surface a user sees. `top_error_top_role` uses only top-surface beads (Top surface, Ironing,
  and anything inside ;_NONPLANAR_BEGIN/END), which is the PLAN definition but undefined where a
  planar staircase shows walls. Error = deposited - mesh, over the part's top domain shrunk by its
  eval inset. RMS, P99 of |error|, mean.
- Coverage: cells topped by a conformal bead / cells whose mesh slope the head can print
  (slope_map at the shell height, every azimuth).
- Max slope per azimuth: gradient of the conformal deposited top, uphill azimuth in 15 deg bins.
- Volume: sum of E x filament area, and its ratio to the mesh volume.
- Max |v_z| and max tip speed: per segment, from F and the segment geometry (commanded values).
- Time: from the `; estimated printing time` / `; total estimated time` comment.
- Clearance (--clearance): clearance.py's chronological sweep with --head, an independent check
  on workstream E's verifier.
"""
import argparse
import json
import math
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import _env  # noqa: E402,F401
import numpy as np  # noqa: E402

import clearance  # noqa: E402
import gcode  # noqa: E402
import geometry as g  # noqa: E402
import heads  # noqa: E402

PITCH = 0.25
TOP_PITCH = 0.1
EXCLUDE_ROLES = {"Skirt", "Brim", "Prime tower", "Wipe tower", "Custom", "Support", "Support interface",
                 "Support transition", "Undefined"}
TOP_ROLES = {"Top surface"}

HEAD_DIRS = [Path(p) for p in os.environ.get("NONPLANAR_HEADS_DIR", "").split(os.pathsep) if p] + \
    [Path(__file__).resolve().parents[2] / "resources" / "nonplanar" / "heads"]


def domain_bbox(dom):
    if dom["kind"] == "circle":
        return np.array([dom["cx"] - dom["r"], dom["cy"] - dom["r"]]), np.array([dom["cx"] + dom["r"], dom["cy"] + dom["r"]])
    if dom["kind"] == "rect":
        return np.array([dom["x0"], dom["y0"]]), np.array([dom["x1"], dom["y1"]])
    if dom["kind"] == "mask":
        return g.grid_bbox(dom)
    los, his = zip(*(domain_bbox(d) for d in dom.get("parts", [dom.get("a")])))
    return np.min(los, 0), np.max(his, 0)


def part_offset(moves, part):
    """Part frame -> G-code frame shift, from the XY bounds of the part's own extrusions; or, for a part
    the slicer centred on the bed (`bed_centered`, as nonplanar_slice_tool does), exactly from the bed."""
    if part.get("offset_xy") is not None:
        return np.asarray(part["offset_xy"], float)
    area = moves.config.get("printable_area")
    if part.get("bed_centered") and area:
        bed = np.array([[float(c) for c in p.split("x")] for p in area.split(",")])
        lo, hi = np.array(part["bbox"][0][:2]), np.array(part["bbox"][1][:2])
        return (bed.min(0) + bed.max(0)) / 2 - (lo + hi) / 2
    m = moves.extruding & ~moves.role_mask(EXCLUDE_ROLES)
    if not m.any():
        return np.zeros(2)
    pts = np.vstack([moves.a[m, :2], moves.b[m, :2]])
    lo, hi = np.array(part["bbox"][0][:2]), np.array(part["bbox"][1][:2])
    return (pts.min(0) + pts.max(0)) / 2 - (lo + hi) / 2


def rasterize(moves, sel, origin, shape, pitch=PITCH, value="top"):
    """Per-cell max (value='top': bead top) or min (value='bottom': bead top - height) over the
    selected extrusion segments whose footprint (the capsule within width/2 of the segment) covers the
    cell centre, at the segment's height at its point nearest the centre. NaN where none does.
    Sample points every pitch/2 along a segment only enumerate candidate cells; the cover test is the
    exact segment distance, so a cell just inside a bead's edge between two samples is not missed.
    `sel` may be a list of selections: one grid each, from one pass over their union (a segment's covered cells
    and heights do not depend on the segments batched with it)."""
    sels = sel if isinstance(sel, list) else [sel]
    union = np.logical_or.reduce(sels)
    idx = np.flatnonzero(union)
    if not len(idx):
        out = [np.full(shape, np.nan) for _ in sels]
        return out if isinstance(sel, list) else out[0]
    member = np.stack([m[idx] for m in sels])


    from concurrent.futures import ThreadPoolExecutor
    n_t = threads()
    starts = list(range(0, len(idx), 2048))
    with ThreadPoolExecutor(n_t) as ex:
        parts = list(ex.map(lambda t: _rasterize_part(moves, idx, member, starts[t::n_t], origin, shape, pitch, value),
                            range(n_t)))
    out = [(np.fmax if value == "top" else np.fmin).reduce([g[k] for g in parts]) for k in range(len(sels))]
    return out if isinstance(sel, list) else out[0]


def _rasterize_part(moves, idx, member, starts, origin, shape, pitch, value):
    outs = [np.full(shape, np.nan) for _ in range(len(member))]
    flats = [o.ravel() for o in outs]
    for start in starts:
        batch = idx[start:start + 2048]
        bm = member[:, start:start + 2048]
        a, b = moves.a[batch], moves.b[batch]
        d = b - a
        L2 = np.einsum("ij,ij->i", d[:, :2], d[:, :2])
        n = np.maximum(1, np.ceil(np.sqrt(L2) / (pitch / 2)).astype(int)) + 1
        seg = np.repeat(np.arange(len(batch)), n)
        segm = bm[:, seg]

        t = np.empty(len(seg))
        first = np.cumsum(n) - n
        for k in np.unique(n):
            at = first[n == k]
            t[(at[:, None] + np.arange(k)).ravel()] = np.tile(np.linspace(0, 1, k), len(at))
        p = a[seg, :2] + d[seg, :2] * t[:, None]
        r = 0.5 * moves.width[batch][seg]

        k = int(math.ceil((float(r.max()) + pitch / 4) / pitch)) + 1
        ci = np.floor((p[:, 0] - origin[0]) / pitch).astype(int)
        cj = np.floor((p[:, 1] - origin[1]) / pitch).astype(int)
        sa, sd, sl = a[seg], d[seg], np.maximum(L2[seg], 1e-18)
        z0 = sa[:, 2] if value == "top" else sa[:, 2] - moves.height[batch][seg]
        for oi, oj, need in _reach(k, pitch):

            if need > float(r.max()) + pitch / 4:
                continue
            ii, jj = ci + oi, cj + oj
            cx = origin[0] + (ii + 0.5) * pitch
            cy = origin[1] + (jj + 0.5) * pitch
            u = np.clip(((cx - sa[:, 0]) * sd[:, 0] + (cy - sa[:, 1]) * sd[:, 1]) / sl, 0, 1)
            ok = (ii >= 0) & (jj >= 0) & (ii < shape[0]) & (jj < shape[1])
            ok &= (cx - sa[:, 0] - u * sd[:, 0])**2 + (cy - sa[:, 1] - u * sd[:, 1])**2 <= r**2
            keys = ii[ok] * shape[1] + jj[ok]
            vals = (z0 + u * sd[:, 2])[ok]
            inm = segm[:, ok]

            for flat, m in zip(flats, inm):
                (np.fmax if value == "top" else np.fmin).at(flat, keys[m], vals[m])
    return outs


def stats(err, weight=None):
    """RMS, P99 of |error|, mean and max |error| of the finite cells; `weight` (area per cell) weights all but P99."""
    ok = np.isfinite(err)
    e = err[ok]
    if not len(e):
        return None
    if weight is None:
        return {"rms": float(np.sqrt(np.mean(e**2))), "p99": float(np.percentile(np.abs(e), 99)),
                "mean": float(e.mean()), "max_abs": float(np.abs(e).max()), "cells": int(len(e))}
    w = np.asarray(weight)[ok]
    return {"rms": float(np.sqrt(np.average(e**2, weights=w))), "p99": float(np.percentile(np.abs(e), 99)),
            "mean": float(np.average(e, weights=w)), "max_abs": float(np.abs(e).max()), "cells": int(len(e))}





BEAD_STEP = 0.1
BRIDGE_GAP = 1.0


BEAD_SPREAD = 0.05


def bead_area(w, h):
    """Orca's bead section (Flow::mm3_per_mm, BeadModel::bead_area): a rectangle with semicircular ends."""
    return h * (w - h * (1 - math.pi / 4))


def bead_height_for_area(w, a):
    """The thinner root of bead_area(w, h) = a; the thickest bead where a is more than a bead of width w holds."""
    k = 1 - math.pi / 4
    disc = w * w - 4 * k * a
    return np.where(disc > 0, (w - np.sqrt(np.maximum(disc, 0))) / (2 * k), w / (2 * k))


def bead_heights(moves, sel):
    """Height of every selected bead from its extrusion: E over its length across the tool axis is the bead
    section, solved for h at the tagged width. Bambu G-code tags no height on most moves, and a curved or
    Z-anti-aliased bead changes height along a path. E carries the flow ratios (filament, per role, first layer);
    each role's median ratio of extruded to nominal section over its planar beads at their layer's tagged Z and
    height divides them out, so a nominal planar bead reads its own layer height, and so does the curved top
    shell's `nonplanar_top_finish_flow`. Flow that follows the geometry stays: ZAA's and FlowMod's."""
    fd = float(moves.config.get("filament_diameter", "1.75").split(",")[0])
    d = moves.b - moves.a
    across = d - np.einsum("ij,ij->i", d, moves.axis)[:, None] * moves.axis
    length = np.linalg.norm(across, axis=1)
    with np.errstate(divide="ignore", invalid="ignore"):
        area = moves.e * math.pi * fd * fd / 4 / length
        ratio = area / bead_area(moves.width, moves.height)
    planar = sel & (moves.shell < 0) & (np.abs(d[:, 2]) < 1e-6) & (moves.height > 0) & (moves.width > 0) & (length > 0.5)

    if moves.layer_z is not None and np.isfinite(moves.layer_z[planar]).any():
        planar &= np.abs(moves.a[:, 2] - moves.layer_z) < 1e-3
    k = np.ones(len(moves))
    overall = np.median(ratio[planar]) if planar.any() else 1.0
    for role in np.unique(moves.role[sel]):
        m = planar & (moves.role == role)
        k[moves.role == role] = np.median(ratio[m]) if m.sum() >= 5 else overall

    finish = float(moves.config.get("nonplanar_top_finish_flow", "1") or 1)
    k = np.where((moves.shell >= 0) & moves.role_mask(TOP_ROLES), k * finish, k)
    with np.errstate(invalid="ignore"):
        h = bead_height_for_area(moves.width, area / k)

    return np.where(sel & np.isfinite(h) & (length > 1e-6), h, moves.height)


def bead_samples(moves, sel, step=BEAD_STEP):
    """Centre samples of the selected beads, at most `step` apart along each road: (points, move index)."""
    idx = np.flatnonzero(sel & (moves.width > 0))
    a, b = moves.a[idx], moves.b[idx]
    n = np.maximum(1, np.ceil(np.linalg.norm(b[:, :2] - a[:, :2], axis=1) / step).astype(int))
    end = np.cumsum(n)
    first = end - n
    count = int(end[-1]) if len(end) else 0
    points, move = np.empty((count, 3)), np.empty(count, dtype=idx.dtype)
    for start in range(0, count, 65536):
        sl = slice(start, min(start + 65536, count))
        rank = np.arange(start, sl.stop)
        seg = np.searchsorted(end, rank, side="right")
        t = (rank - first[seg] + 0.5) / n[seg]
        points[sl] = a[seg] + (b[seg] - a[seg]) * t[:, None]
        move[sl] = idx[seg]
    return points, move


def threads():
    """Worker threads of one measurement: at most 4 (a slice's share of the host), within this process's CPUs."""
    return max(1, min(4, os.cpu_count() or 1))


def _reach(k, pitch):
    """The cell offsets (oi, oj) in -k..k with the radius a sample's disc needs to reach each: a point of cell
    (ci, cj) lies at least (|o| - 1/2) * pitch from the centre of cell (ci + oi, cj + oj) on each axis. The exact
    cover test runs on the samples that wide only; the ones left out fail it by more than 1e-6 mm, so the result
    does not change."""
    return [(oi, oj, pitch * math.hypot(max(abs(oi) - 0.5, 0.0), max(abs(oj) - 0.5, 0.0)) - 1e-6)
            for oi in range(-k, k + 1) for oj in range(-k, k + 1)]


def _offsets(r, k, pitch):
    """_reach with the samples, in their order, wide enough for each offset (None: every sample)."""
    for oi, oj, need in _reach(k, pitch):
        yield oi, oj, (None if need <= 0 else np.flatnonzero(r >= need))


def _visible(xy, z, r, origin, shape, pitch, tie, cells=None):
    best = np.full(shape[0] * shape[1], -np.inf)
    near = np.full(shape[0] * shape[1], np.inf)
    win = np.full(shape[0] * shape[1], -1, np.int64)
    active = np.flatnonzero(np.isfinite(z))
    if not len(active):
        return win
    xy, z, r = xy[active], z[active], r[active]
    k = int(math.ceil(float(r.max()) / pitch)) + 1
    ci = np.floor((xy[:, 0] - origin[0]) / pitch).astype(np.int64)
    cj = np.floor((xy[:, 1] - origin[1]) / pitch).astype(np.int64)
    hits = []
    for oi, oj, c in _offsets(r, k, pitch):
        if c is not None and not len(c):
            continue
        c = np.arange(len(z)) if c is None else c
        ii, jj = ci[c] + oi, cj[c] + oj
        ok = (ii >= 0) & (jj >= 0) & (ii < shape[0]) & (jj < shape[1])
        d2 = (origin[0] + (ii + 0.5) * pitch - xy[c, 0]) ** 2 + (origin[1] + (jj + 0.5) * pitch - xy[c, 1]) ** 2
        f = np.flatnonzero(ok & (d2 <= r[c] * r[c]))
        s, ii, jj, d2 = c[f], ii[f], jj[f], d2[f]
        key = ii * shape[1] + jj
        if cells is not None:
            keep = cells[key]
            s, key, d2 = s[keep], key[keep], d2[keep]
        np.maximum.at(best, key, z[s])
        hits.append((key, s, d2))
    for key, s, d2 in hits:
        top = z[s] >= best[key] - tie
        np.minimum.at(near, key[top], d2[top])
    for key, s, d2 in hits:
        top = (z[s] >= best[key] - tie) & (d2 <= near[key])
        win[key[top]] = active[s[top]]
    return win


def visible(xy, z, r, origin, shape, pitch, tie=0.01):
    """Per cell, the sample whose bead stands highest over the cell centre (-1: none). A road is the union of
    the discs of its half-width around its centre samples. Of beads within `tie` of the highest, the nearest
    centre takes the cell: the floor's road beside a raised letter, not a bead of the same layer under the letter,
    whose centre the letter covers. A cell no bead covers takes the beads reaching it within BEAD_SPREAD: melt
    closes such pinholes, but the spread takes no cell from a bead that covers it."""
    win = _visible(xy, z, r, origin, shape, pitch, tie)
    bare = win < 0
    if bare.any():
        spread = _visible(xy, z, r + BEAD_SPREAD, origin, shape, pitch, tie, bare)
        win = np.where(bare, spread, win)
    return win.reshape(shape)


ROAD_SHARE = 0.5


def _support_candidates(p, which, radius):
    """Points that can reach a fill query's height range in a nearby coarse XY cell."""
    from scipy import ndimage
    pitch = max(radius, 0.5)
    lo = p[which, :2].min(0) - 2 * pitch
    shape = tuple(np.ceil((p[which, :2].max(0) - lo) / pitch).astype(int) + 3)
    floor = np.full(shape, np.inf)
    cell = np.floor((p[which, :2] - lo) / pitch).astype(np.int64)


    np.minimum.at(floor, (cell[:, 0], cell[:, 1]), p[which, 2] - BRIDGE_GAP - 0.010001)
    floor = ndimage.minimum_filter(floor, size=5, mode="constant", cval=np.inf)
    keep = []
    for start in range(0, len(p), 65536):
        q = p[start:start + 65536]
        ij = np.floor((q[:, :2] - lo) / pitch).astype(np.int64)
        inside = (ij[:, 0] >= 0) & (ij[:, 1] >= 0) & (ij[:, 0] < shape[0]) & (ij[:, 1] < shape[1])
        take = np.flatnonzero(inside)
        take = take[q[take, 2] >= floor[ij[take, 0], ij[take, 1]]]
        keep.append(start + take)
    return np.concatenate(keep)


def fill_balance(p, mid, r, h, which, az=None):
    """Bead height from flow minus the gap from the nozzle to the material laid before it under the bead's centre
    (an earlier bead within that bead's half-width, at most BRIDGE_GAP lower), at the samples `which`: negative
    where the bead cannot fill the gap. 0 over air or a bridge: that bead hangs from the nozzle."""
    from scipy.spatial import cKDTree
    out = np.zeros(len(which))
    if not len(which):
        return out
    rmax = float(r.max())
    s = rmax / (BRIDGE_GAP / 2 + 0.01)
    candidates = _support_candidates(p, which, rmax) if len(p) > 100000 else np.arange(len(p))
    tree = cKDTree(np.column_stack([p[candidates, 0], p[candidates, 1], p[candidates, 2] * s]))
    q = np.column_stack([p[which, 0], p[which, 1], (p[which, 2] - BRIDGE_GAP / 2) * s])
    for c0 in range(0, len(which), 128):
        sl = slice(c0, c0 + 128)


        pairs = cKDTree(q[sl]).sparse_distance_matrix(tree, rmax, p=np.inf, output_type="ndarray")
        if not len(pairs):
            continue
        i, j = pairs["i"].astype(np.int64) + c0, candidates[pairs["j"].astype(np.int64)]
        del pairs
        w = which[i]
        ok = ((mid[j] < mid[w]) & (p[j, 2] <= p[w, 2] + 1e-6) &
              ((p[j, 0] - p[w, 0]) ** 2 + (p[j, 1] - p[w, 1]) ** 2 <= r[j] ** 2))
        sub = np.full(len(q[sl]), -np.inf)
        np.maximum.at(sub, i[ok] - c0, p[j[ok], 2])
        ws = which[sl]

        gap = (p[ws, 2] - sub) * (1.0 if az is None else az[ws])
        out[sl] = np.where(np.isfinite(sub) & (gap <= BRIDGE_GAP), h[mid[ws]] - gap, 0.0)
    return out


def _stack_level(p, mid, r, ci, cj, lo, shape, pitch, k, which=None, cells=None):
    """Highest bead mid-height over each cell centre a sample's disc of radius r covers, over the samples
    `which` (all), written only to `cells` (all) when given."""
    from concurrent.futures import ThreadPoolExecutor
    fin = np.isfinite(mid)
    if which is not None:
        member = np.zeros(len(mid), bool)
        member[which] = True
        fin &= member
    offsets = _reach(k, pitch)
    candidates = np.flatnonzero(fin)
    level = np.full(shape[0] * shape[1], -np.inf)


    for start in range(0, len(candidates), 65536):
        c = candidates[start:start + 65536]
        ii, jj = ci[c], cj[c]
        ok = ((ii >= 0) & (jj >= 0) & (ii < shape[0]) & (jj < shape[1]) &
              ((lo[0] + (ii + 0.5) * pitch - p[c, 0]) ** 2 +
               (lo[1] + (jj + 0.5) * pitch - p[c, 1]) ** 2 <= r[c] * r[c]))
        key, val = ii[ok] * shape[1] + jj[ok], mid[c[ok]]
        if cells is not None:
            keep = cells[key]
            key, val = key[keep], val[keep]
        np.maximum.at(level, key, val)



    from scipy import ndimage
    bounds = level if cells is None else np.where(cells, level, np.inf)
    floor = ndimage.minimum_filter(bounds.reshape(shape), size=2 * k + 1, mode="constant", cval=-np.inf).ravel()
    active = []
    for start in range(0, len(candidates), 65536):
        c = candidates[start:start + 65536]
        own = np.clip(ci[c] * shape[1] + cj[c], 0, len(level) - 1)
        active.append(c[mid[c] > floor[own]])
    candidates = np.concatenate(active) if active else candidates
    n = threads()
    with ThreadPoolExecutor(n) as ex:
        parts = list(ex.map(lambda t: _stack_part(p, mid, r, ci, cj, lo, shape, pitch, k, candidates, cells,
                                                offsets[t::n], level.copy()), range(n)))
    return np.maximum.reduce(parts) if len(parts) > 1 else parts[0]


def _stack_part(p, mid, r, ci, cj, lo, shape, pitch, k, candidates, cells, offsets, level):
    from scipy import ndimage

    def stamp(active):
        for start in range(0, len(active), 65536):
            batch = active[start:start + 65536]
            for oi, oj, need in offsets:
                c = batch if need <= 0 else batch[r[batch] >= need]
                if not len(c):
                    continue
                ii, jj = ci[c] + oi, cj[c] + oj
                inside = (ii >= 0) & (jj >= 0) & (ii < shape[0]) & (jj < shape[1])
                c, ii, jj = c[inside], ii[inside], jj[inside]
                key = ii * shape[1] + jj
                higher = mid[c] > level[key]
                if cells is not None:
                    higher &= cells[key]
                c, ii, jj = c[higher], ii[higher], jj[higher]
                if not len(c):
                    continue
                if pitch * math.hypot(abs(oi) + 0.5, abs(oj) + 0.5) < float(r[c].min()) - 1e-6:
                    ok = slice(None)
                else:
                    ok = ((lo[0] + (ii + 0.5) * pitch - p[c, 0]) ** 2 +
                          (lo[1] + (jj + 0.5) * pitch - p[c, 1]) ** 2 <= r[c] * r[c])
                key, val = ii[ok] * shape[1] + jj[ok], mid[c][ok]
                if cells is not None:
                    keep = cells[key]
                    key, val = key[keep], val[keep]
                np.maximum.at(level, key, val)

    for start in range(0, len(candidates), 65536):
        c = candidates[start:start + 65536]
        own = np.clip(ci[c] * shape[1] + cj[c], 0, len(level) - 1)
        seed = mid[c] >= level[own]
        if cells is not None:
            seed &= cells[own]
        stamp(c[seed])


    bounds = level if cells is None else np.where(cells, level, np.inf)
    floor = ndimage.minimum_filter(bounds.reshape(shape), size=2 * k + 1, mode="constant", cval=-np.inf).ravel()
    for start in range(0, len(candidates), 65536):
        c = candidates[start:start + 65536]
        own = np.clip(ci[c] * shape[1] + cj[c], 0, len(level) - 1)
        stamp(c[mid[c] > floor[own]])
    return level


def covered(p, z, r, hs, pitch=0.05, az=None, state=None):
    """Samples whose own centre lies under a bead stacked on them: within that bead's half-width, its mid-height
    above their top. A road below the top layer, or a road of the floor running under a raised letter's edge; not
    the next sample up a sloped road beside it. The stack level (highest bead mid-height over each `pitch` cell,
    beads as discs of their half-width) is read at each sample's cell. Returns (covered, state); given the state of
    a call on the same samples, only the cells the discs of samples whose top changed reach are recomputed, from
    the samples near them: the result is the same as a full pass."""
    from scipy import ndimage
    fin = np.isfinite(z)
    mid = np.where(fin, z - 0.5 * hs / (1.0 if az is None else np.maximum(az, 0.2)), -np.inf)
    if state is None:
        lo = p[fin, :2].min(0) - 1.0
        shape = tuple(np.ceil((p[fin, :2].max(0) + 1.0 - lo) / pitch).astype(int))
        ci = np.floor((p[:, 0] - lo[0]) / pitch).astype(np.int64)
        cj = np.floor((p[:, 1] - lo[1]) / pitch).astype(np.int64)
        k = int(math.ceil(float(r[fin].max()) / pitch))
        level = None
    else:
        lo, shape, ci, cj, k, level, mid_before = state
        changed = np.flatnonzero(~((mid == mid_before) | (np.isnan(mid) & np.isnan(mid_before))))


    inside = (ci >= 0) & (cj >= 0) & (ci < shape[0]) & (cj < shape[1])
    queries = np.zeros(shape[0] * shape[1], bool)
    for start in range(0, len(ci), 65536):
        sl = slice(start, start + 65536)
        keys = ci[sl] * shape[1] + cj[sl]
        queries[keys[inside[sl]]] = True

    if level is None or k * pitch > 1.0 or not np.isfinite(mid_before).all() or not fin.all() \
            or len(changed) > 0.2 * len(mid):
        level = _stack_level(p, mid, r, ci, cj, lo, shape, pitch, k, cells=queries)
    elif len(changed):

        cells = np.isfinite(_stack_level(p, np.zeros(len(mid)), r, ci, cj, lo, shape, pitch, k, which=changed, cells=queries))
        near = ndimage.binary_dilation(cells.reshape(shape), np.ones((2 * k + 1, 2 * k + 1), bool)).ravel()
        cand = np.flatnonzero(near[ci * shape[1] + cj])
        part = _stack_level(p, mid, r, ci, cj, lo, shape, pitch, k, which=cand, cells=cells)
        level = level.copy()
        level[cells] = part[cells]
    inside = (ci >= 0) & (cj >= 0) & (ci < shape[0]) & (cj < shape[1])
    out = fin & inside & (level[np.clip(ci * shape[1] + cj, 0, len(level) - 1)] > z)
    return out, (lo, shape, ci, cj, k, level, mid)


def surface_tops(p, mid, r, h, az=None):
    """Bead tops at the centre samples p, and which samples are on the printed surface (not covered at their own
    centre by a higher bead). A top is the nozzle, lowered where the surface's roads lack the flow to fill the gap
    under the nozzle: each surface sample's fill balance (fill_balance) is averaged over the surface samples
    within ROAD_SHARE at a similar height, since surplus melt of a road spreads into its neighbour's shortfall.
    ZAA charges each segment for its end point's height, short on a downhill road and over on the uphill one
    beside it, which balances where they meet but not along the road; a curved shell clamped thinner than its gap,
    or a top too thin for its layer, does not balance. Lowered beads can uncover others: three rounds. Nothing
    here depends on a sampling grid. `az`, the z component of each sample's tool axis (5-axis), turns heights
    and gaps along the axis into vertical ones."""
    from scipy.spatial import cKDTree
    z = p[:, 2].copy()
    bal = np.full(len(z), np.nan)
    cov, state = covered(p, z, r, h[mid], az=az)
    top = ~cov
    for _ in range(3):
        new = np.flatnonzero(top & np.isnan(bal))
        if not len(new):
            break
        bal[new] = fill_balance(p, mid, r, h, new, az)
        vis = np.flatnonzero(top)
        scale = ROAD_SHARE / 0.1
        tree = cKDTree(np.column_stack([p[vis, 0], p[vis, 1], p[vis, 2] * scale]))
        pairs = tree.query_pairs(ROAD_SHARE, output_type="ndarray")
        total, count = bal[vis].copy(), np.ones(len(vis))
        np.add.at(total, pairs[:, 0], bal[vis][pairs[:, 1]])
        np.add.at(total, pairs[:, 1], bal[vis][pairs[:, 0]])
        np.add.at(count, pairs.ravel(), 1)
        z = p[:, 2].copy()
        z[vis] += np.minimum(0.0, total / count) / (1.0 if az is None else np.maximum(az[vis], 0.2))
        was = top
        cov, state = covered(p, z, r, h[mid], az=az, state=state)
        top = ~cov
        if (top == was).all():
            break
    return z, top


def fine_surface(face, x, y):
    """Mesh top, its gradient, the error domain and the distance to the nearest feature edge at part-frame
    points: from the face's fine grid (gate_bench.model_part's `fine`), else its analytic surface and domain
    (no feature edges)."""
    fine = face.get("fine")
    if fine:
        spec = {"kind": "grid", "npz": fine["npz"]}
        z = g.grid_sample(spec, "z", x, y)
        gx, gy = g.grid_sample(spec, "gx", x, y), g.grid_sample(spec, "gy", x, y)
        dom = ((g.grid_sample(spec, "dist", x, y, nearest=True) >= face.get("eval_inset", 2.0)) &
               (g.grid_sample(spec, "ok", x, y, nearest=True) > 0.5))
        grid = g._grid(spec)
        if "edge_pts" in grid:
            from scipy.spatial import cKDTree
            pts = grid["edge_pts"]
            edge = cKDTree(pts).query(np.column_stack([np.ravel(x), np.ravel(y)]), distance_upper_bound=5.0)[0] \
                .reshape(np.shape(x)) if len(pts) else g.grid_sample(spec, "edge", x, y, nearest=True)
        else:
            edge = g.grid_sample(spec, "edge", x, y, nearest=True)
        return z, gx, gy, dom & np.isfinite(z), edge
    z = g.surface_z(face["surface"], x, y)
    gx, gy = g.surface_grad(face["surface"], x, y)
    dom = g.domain_mask(face["domain"], x, y, face.get("eval_inset", 2.0)) & np.isfinite(z)
    return z, gx, gy, dom, np.full(np.shape(z), np.inf)


def reference_excluded(face, x, y, fine=False):
    """Reference queries excluded at discontinuous interpolation squares; analytic faces have none."""
    spec = face.get("fine") if fine else face["surface"]
    if spec and "npz" in spec:
        return g.grid_discontinuous(spec, x, y)
    return np.zeros(np.broadcast(x, y).shape, bool)


def config_width(config, key):
    """A line width setting in mm: absolute, a percentage of the nozzle, or 0 (the default line width)."""
    nozzle = float((config.get("nozzle_diameter") or "0.4").split(",")[0])
    for k in (key, "line_width"):
        v = (config.get(k) or "0").strip()
        w = float(v[:-1]) * nozzle / 100 if v.endswith("%") else float(v)
        if w > 0:
            return w
    return 1.05 * nozzle


def top_metric(moves, sel, face, off, offset_z, pitch=TOP_PITCH):
    """The top metric over cells of `pitch`, area-weighted (1 / n_z per cell): each cell takes the bead standing
    highest over it, and its error is that bead's top at its centre sample against the mesh at the same point,
    along the surface normal. A bead's edge over lower ground is not the surface, and a raised letter no longer
    reads its height beside it. Cells within half the top beads' width of a feature edge (a step, a letter, a
    hole or a pocket in the top) form the edge metric instead: vertical error of the bead over the cell itself."""
    lo, hi = domain_bbox(face["domain"]) if not face.get("fine") else g.grid_bbox({"npz": face["fine"]["npz"]})
    lo, hi = lo + off, hi + off
    shape = tuple(np.ceil((hi - lo) / pitch).astype(int))
    X, Y = np.meshgrid(lo[0] + (np.arange(shape[0]) + 0.5) * pitch, lo[1] + (np.arange(shape[1]) + 0.5) * pitch,
                       indexing="ij")
    with np.errstate(invalid="ignore"):
        zc, gxc, gyc, dom, edge = fine_surface(face, X - off[0], Y - off[1])
    p, mid = bead_samples(moves, sel)
    r = 0.5 * moves.width[mid]
    h = bead_heights(moves, sel)
    az = moves.axis[mid, 2] if (moves.axis[sel, 2] < 1 - 1e-9).any() else None
    z, on = surface_tops(p, mid, r, h, az)
    win = visible(p[:, :2], np.where(on, z, -np.inf), r, lo, shape, pitch)
    cover = (win >= 0) & dom
    s = win[cover]
    with np.errstate(invalid="ignore"):
        zs, gxs, gys, _, edge_s = fine_surface(face, p[s, 0] - off[0], p[s, 1] - off[1])
    nz_s = 1 / np.sqrt(1 + gxs ** 2 + gys ** 2)
    e_top = np.full(shape, np.nan)
    e_top[cover] = (z[s] - zs - offset_z) * nz_s
    e_cell = np.full(shape, np.nan)
    e_cell[cover] = z[s] - zc[cover] - offset_z
    area = np.sqrt(1 + gxc ** 2 + gyc ** 2)
    area = np.where(np.isfinite(area), area, 1.0)

    band = 0.5 * config_width(moves.config, "top_surface_line_width")


    near = edge < band
    near[cover] |= edge_s < 0.5 * band
    top = dom & ~near
    conf = np.zeros(shape, bool)
    conf[cover] = moves.shell[mid[s]] >= 0
    res = {"top_error": stats(np.where(top, e_top, np.nan), area),
           "edge_error": stats(np.where(dom & near, e_cell, np.nan), area),
           "edge_band_mm": band, "eval_cells": int(top.sum()), "uncovered_cells": int((top & ~cover).sum()),

           "gap_cells": int(round((top & (e_cell < -1.0)).sum() * (pitch / PITCH) ** 2)),
           "gap_area_mm2": float((top & (e_cell < -1.0)).sum() * pitch ** 2),
           "bead_samples": int(len(z)), "bead_tops_lowered": int((z < p[:, 2] - 1e-4).sum())}
    step_cells = reference_excluded(face, X - off[0], Y - off[1], fine=bool(face.get("fine")))
    step_beads = reference_excluded(face, p[s, 0] - off[0], p[s, 1] - off[1], fine=bool(face.get("fine")))
    excluded = step_cells.copy()
    excluded[cover] |= step_beads
    res.update(reference_step_excluded_cells=int(excluded.sum()),
               reference_step_excluded_area_mm2=float(excluded.sum() * pitch ** 2))
    if np.isfinite(e_top[top]).any():
        lowered = np.zeros(shape, bool)
        lowered[cover] = z[s] < p[s, 2] - 1e-4
        res["bead_top_lowered_share"] = float((lowered & top).sum() / max(1, (top & cover).sum()))
    return res, {"error": np.where(top, e_top, np.nan), "error_edge": np.where(dom & near, e_cell, np.nan),
                 "mesh": np.where(dom, zc + offset_z, np.nan), "conformal": conf & top, "area": area,
                 "origin": lo, "pitch": pitch}


def head_printable(head, gx, gy, gap, n=72):
    """True where a plane with this gradient is within the head's slope map at every azimuth."""
    tan_a = np.tan(np.array(head.slope_map(n, gap)))
    psi = 2 * np.pi * np.arange(n) / n
    ok = np.ones(gx.shape, bool)
    for t, ps in zip(tan_a, psi):
        ok &= gx * math.cos(ps) + gy * math.sin(ps) <= t + 1e-9
    return ok


def motion(moves):
    d = moves.b - moves.a
    L = np.linalg.norm(d, axis=1)
    v = moves.f / 60.0
    ok = L > 1e-9
    vz = np.where(ok, np.abs(d[:, 2]) / np.where(ok, L, 1) * v, 0)
    extruding = (moves.e > 1e-9) & ok
    out = {"max_vz": float(vz.max()) if len(vz) else 0.0,
           "max_vz_extruding": float(vz[extruding].max()) if extruding.any() else 0.0}

    if len(vz):
        out["max_vz_line"] = int(moves.line[int(np.argmax(vz))])
    return out


def coverage_v2(grids, flat_deg=1.0, eligible_deg=math.degrees(math.atan(0.2 / 0.3)) - 1.0):
    """Conformal share of the curved top by area, outside the edge band: of every cell sloped at least
    `flat_deg`, and of those the band schedule's shells can cover (gate_bench.ELIGIBLE_SLOPE_DEG)."""
    err, mesh, conf, area, pitch = grids["error"], grids["mesh"], grids["conformal"], grids["area"], grids["pitch"]
    with np.errstate(invalid="ignore"):
        slope = np.degrees(np.arctan(np.sqrt(np.maximum(area ** 2 - 1, 0))))
    curved = np.isfinite(err) & (slope >= flat_deg)
    eligible = curved & (slope <= eligible_deg)
    share = lambda m: float((area * (conf & m)).sum() / (area * m).sum()) if m.any() else None
    return {"curved": share(curved), "eligible": share(eligible), "top": share(np.isfinite(err)),
            "curved_area_mm2": float((area * curved).sum() * pitch ** 2)}





BLOB_PITCH = 0.04
BLOB_SMOOTH = 0.5
BLOB_RATIO = 1.25
BLOB_DESIGN = 1.15
BLOB_TURN = (0.5, 60.0)
BLOB_CLASSES = ("seam", "road_end", "thick_by_design", "turn", "overlap")


def laid_filament(moves):
    """Filament each move lays: positive E first refills what retractions (a wipe's included) drew back."""
    top = np.maximum.accumulate(np.concatenate([[0.0], np.cumsum(moves.e)]))
    return np.maximum(np.diff(top), 0.0)


def road_seams(p, spacing):
    """Seams of one road (n x 2 points), CurvedSeams' loop detector: where it steps from one ring to the next, a
    pair of opposite heading kinks of at least 6 degrees beyond the local curvature, 0.15 to 4 mm apart, that shift
    the road sideways by 0.4 to 1.7 spacings (a stepped link or a sheared hand-over). A loop runs from the road's
    start or a step through 0.6 to 1.15 turns and at least 2 mm. The road is read every 0.05 mm, averaged over
    0.3 mm: a staircase's steps are not kinks. Returns the loops' starts and whether the road's start and end are
    a loop's."""
    arc = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(p, axis=0), axis=1))])
    if arc[-1] < 2.0:
        return [], False, False
    s = np.linspace(0, arc[-1], int(arc[-1] / 0.05) + 1)
    k = np.ones(7) / 7
    norm = np.convolve(np.ones(len(s)), k, "same")
    p = np.column_stack([np.convolve(np.interp(s, arc, p[:, i]), k, "same") / norm for i in (0, 1)])
    n = len(p)
    d = np.diff(p, axis=0)
    arc = np.concatenate([[0.0], np.cumsum(np.linalg.norm(d, axis=1))])
    turn = np.zeros(n)
    turn[1:-1] = (np.diff(np.arctan2(d[:, 1], d[:, 0])) + np.pi) % (2 * np.pi) - np.pi
    cum = np.cumsum(turn)
    lo = np.searchsorted(arc, arc - 1.5)
    hi = np.minimum(np.searchsorted(arc, arc + 1.5), n - 1)
    span = arc[hi] - arc[lo]
    with np.errstate(divide="ignore", invalid="ignore"):
        kappa = np.where(span > 0, (cum[hi] - cum[lo]) / span, 0.0)
        ds = 0.5 * (arc[np.minimum(np.arange(n) + 1, n - 1)] - arc[np.maximum(np.arange(n) - 1, 0)])
        excess = turn - np.where(span > 0, (cum[hi] - cum[lo] - turn) / span * ds, 0.0)
    excess[[0, -1]] = 0.0

    def sideways(a, b):
        k0 = min(np.searchsorted(arc, arc[a] - 0.3), a - 1)
        t = p[a] - p[k0]
        if np.linalg.norm(t) < 1e-6:
            return False
        t /= np.linalg.norm(t)
        length = arc[b] - arc[a]
        dev = float((p[b] - p[a]) @ [-t[1], t[0]]) - (0.5 * kappa[a] * length * length if length > 0.6 else 0.0)
        return 0.4 * spacing <= abs(dev) <= 1.7 * spacing

    big = np.flatnonzero(np.abs(excess) >= math.radians(6))
    starts, used = [0], -1
    for a in big:
        if a <= used:
            continue
        for b in big[np.searchsorted(big, a, side="right"):]:
            if arc[b] - arc[a] > 4.0:
                break
            if arc[b] - arc[a] >= 0.15 and np.sign(excess[b]) != np.sign(excess[a]) and \
                    abs(excess[a] + excess[b]) <= 0.5 * max(abs(excess[a]), abs(excess[b])) and sideways(a, b):
                starts.append(int(b))
                used = b
                break
    loops = [(i, j) for i, j in zip(starts, starts[1:] + [n - 1])
             if j - i >= 3 and 1.2 * math.pi <= abs(cum[j - 1] - cum[i]) <= 2.3 * math.pi and arc[j] - arc[i] >= 2.0]
    return [p[i] for i, _ in loops], bool(loops) and loops[0][0] == 0, bool(loops) and loops[-1][1] == n - 1


def road_turns(p):
    """Points of a road (n x 2) where its heading over the BLOB_TURN span before differs from the heading after by
    more than the angle: corners and U-turns, not a staircase's steps or a gentle ring."""
    arc = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(p, axis=0), axis=1))])
    span, deg = BLOB_TURN
    at = lambda s: np.column_stack([np.interp(s, arc, p[:, 0]), np.interp(s, arc, p[:, 1])])
    back, ahead = p - at(arc - span), at(arc + span) - p
    nb, na = np.linalg.norm(back, axis=1), np.linalg.norm(ahead, axis=1)
    ok = (nb > span / 2) & (na > span / 2)
    cos = np.einsum("ij,ij->i", back, ahead) / np.maximum(nb * na, 1e-12)
    return p[ok & (cos < math.cos(math.radians(deg)))]


def _bead_profile(dist, w, h):
    """Orca's bead section across a road: full height over its flat middle, a semicircle at either side."""
    c, r = (w - h) / 2, h / 2
    return np.where(dist <= c, h, 2 * np.sqrt(np.maximum(r * r - (dist - c) ** 2, 0.0)))


def _paint(a, b, w, h, cap0, cap1, lo, shape, pitch):
    """Cells of each segment's swept bead: (segment, flat cell index, section thickness). A segment covers its
    rectangle, half-open towards its end so a joint is laid once; a road's first and last segments add a round
    cap. Segments in chunks of half a million cells."""
    r = w / 2
    i0 = np.floor((np.minimum(a[:, 0], b[:, 0]) - r - lo[0]) / pitch).astype(np.int64)
    i1 = np.floor((np.maximum(a[:, 0], b[:, 0]) + r - lo[0]) / pitch).astype(np.int64) + 1
    j0 = np.floor((np.minimum(a[:, 1], b[:, 1]) - r - lo[1]) / pitch).astype(np.int64)
    j1 = np.floor((np.maximum(a[:, 1], b[:, 1]) + r - lo[1]) / pitch).astype(np.int64) + 1
    i0, j0 = np.maximum(i0, 0), np.maximum(j0, 0)
    i1, j1 = np.minimum(i1, shape[0]), np.minimum(j1, shape[1])
    ny = np.maximum(j1 - j0, 0)
    count = np.maximum(i1 - i0, 0) * ny
    ends = np.cumsum(count)
    out = []
    s0 = 0
    while s0 < len(a):
        s1 = max(s0 + 1, int(np.searchsorted(ends, ends[s0] - count[s0] + 500_000, side="right")))
        n = count[s0:s1]
        seg = np.repeat(np.arange(s0, s1), n)
        k = np.arange(len(seg)) - np.repeat(np.cumsum(n) - n, n)
        ci, cj = i0[seg] + k // ny[seg], j0[seg] + k % ny[seg]
        x, y = lo[0] + (ci + 0.5) * pitch, lo[1] + (cj + 0.5) * pitch
        dx, dy = b[seg, 0] - a[seg, 0], b[seg, 1] - a[seg, 1]
        u = ((x - a[seg, 0]) * dx + (y - a[seg, 1]) * dy) / np.maximum(dx * dx + dy * dy, 1e-18)
        along = ((u >= 0) | cap0[seg]) & ((u < 1) | cap1[seg])
        uc = np.clip(u, 0, 1)
        v = _bead_profile(np.hypot(x - a[seg, 0] - uc * dx, y - a[seg, 1] - uc * dy), w[seg], h[seg]) * along
        keep = v > 0
        out.append((seg[keep], (ci * shape[1] + cj)[keep], v[keep]))
        s0 = s1
    return [np.concatenate(c) for c in zip(*out)] if out else [np.zeros(0, np.int64), np.zeros(0, np.int64), np.zeros(0)]


def _pass_blobs(a, b, w, vol, first, last, seams, ends, turns, pitch=BLOB_PITCH):
    """One curved pass's thickness map and blobs. a, b: segment ends (XY) in print order; vol: mm^3 each lays;
    first/last: a road starts/ends there; seams, ends, turns: XY sites. Each segment's volume is spread over exactly
    the cells its bead covers, shaped by its section (its height solved from vol/length at width w)."""
    from scipy import ndimage
    from scipy.spatial import cKDTree
    length = np.linalg.norm(b - a, axis=1)
    h = np.minimum(bead_height_for_area(w, vol / np.maximum(length, 1e-9)), w)
    lo = np.minimum(a, b).min(0) - w.max()
    shape = tuple(int(s) for s in np.ceil((np.maximum(a, b).max(0) + w.max() - lo) / pitch).astype(int) + 1)
    seg, cell, v = _paint(a, b, w, h, first, last, lo, shape, pitch)
    painted = np.bincount(seg, v, len(a))

    bare = np.flatnonzero(painted <= 0)
    if len(bare):
        s2, c2, v2 = _paint(a[bare], b[bare], w[bare], h[bare], np.ones(len(bare), bool), np.ones(len(bare), bool),
                            lo, shape, pitch)
        seg, cell, v = np.concatenate([seg, bare[s2]]), np.concatenate([cell, c2]), np.concatenate([v, v2])
        painted = np.bincount(seg, v, len(a))
    scale = np.divide(vol, painted * pitch * pitch, out=np.zeros(len(a)), where=painted > 0)
    size = shape[0] * shape[1]
    thick = np.bincount(cell, v * scale[seg], size).reshape(shape).astype(np.float32)

    design = np.bincount(cell, v * scale[seg] * h[seg], size).reshape(shape).astype(np.float32)
    k = 2 * int(round(BLOB_SMOOTH / pitch / 2)) + 1
    box = lambda g: ndimage.uniform_filter(g, k)


    area = ndimage.binary_closing(thick > 0, iterations=max(1, int(round(float(np.median(w)) / 2 / pitch))))
    inner = box(area.astype(np.float32)) > 1 - 1e-4
    out = {"footprint_mm2": float(inner.sum() * pitch * pitch), "seams": [], "classes": {}}
    if inner.sum() < 50:
        return out
    sm = box(thick)
    own = box(design) / np.maximum(sm, 1e-12)



    med = float(np.median(own[inner]))
    blob = inner & (sm > BLOB_RATIO * med)
    own = own[blob]
    bi, bj = np.nonzero(blob)
    xy = np.column_stack([lo[0] + (bi + 0.5) * pitch, lo[1] + (bj + 0.5) * pitch])
    reach = float(np.median(w)) / 2 + BLOB_SMOOTH / 2
    near = lambda sites: (cKDTree(np.asarray(sites)).query(xy, distance_upper_bound=reach)[0] <= reach
                          if len(sites) and len(xy) else np.zeros(len(xy), bool))
    left = np.ones(len(xy), bool)
    excess = sm[blob] - med


    for name, m in (("seam", near(seams)), ("road_end", near(ends)), ("thick_by_design", own > BLOB_DESIGN * med),
                    ("turn", near(turns)), ("overlap", left)):
        m = left & m
        out["classes"][name] = (float(m.sum() * pitch * pitch), float(excess[m].sum() * pitch * pitch))
        left &= ~m

    span = int(math.ceil(1.5 / pitch))
    for s in seams:
        ci, cj = np.floor((np.asarray(s) - lo) / pitch).astype(int)
        sub = thick[max(ci - span, 0):ci + span + 1, max(cj - span, 0):cj + span + 1]
        x = (np.arange(max(ci - span, 0), max(ci - span, 0) + sub.shape[0]) + 0.5) * pitch + lo[0] - s[0]
        y = (np.arange(max(cj - span, 0), max(cj - span, 0) + sub.shape[1]) + 0.5) * pitch + lo[1] - s[1]
        rr = np.hypot(x[:, None], y[None, :])
        at, ring = (rr <= 0.5) & (sub > 0), (rr > 0.9) & (rr <= 1.5) & (sub > 0)
        if at.sum() >= 3 and ring.sum() >= 3:
            out["seams"].append(float(sub[at].mean() - np.median(sub[ring])))
    out["blob_mm2"] = float(blob.sum() * pitch * pitch)
    out["excess_mm3"] = float(excess.sum() * pitch * pitch)
    return out


def blob_summary(moves):
    """The `blob` record of a run (SEAMS.md "Measurement"): every curved pass (patch, shell; ironing left out) as a
    volume-conserving bead thickness map; blob cells, averaged over 0.5 mm, stand over 1.25x the pass's median bead
    section height.
    Each blob cell goes to the first class it fits: a seam (a loop's start: a step between rings inside a road,
    or the ends of a road that closes on itself), another road start or end, a bead thick by design (its own
    section stands over the pass's), a turn of the road (road_turns), or overlap: roads laid closer than their
    spacing. Areas in mm^2, excess volume over that height in mm^3; `outer` repeats the areas for each patch's
    last shell, the visible top."""
    fd = float(moves.config.get("filament_diameter", "1.75").split(",")[0])
    laid = laid_filament(moves) * math.pi * fd * fd / 4
    sel = moves.extruding & (moves.shell >= 0) & ~moves.ironing & ~moves.role_mask(EXCLUDE_ROLES) & (moves.width > 0)
    total = {"pitch_mm": BLOB_PITCH, "passes": 0, "footprint_mm2": 0.0, "blob_mm2": 0.0, "excess_mm3": 0.0,
             "classes": {c: {"mm2": 0.0, "mm3": 0.0} for c in BLOB_CLASSES},
             "outer": {"footprint_mm2": 0.0, "blob_mm2": 0.0, "classes": {c: 0.0 for c in BLOB_CLASSES}}}
    steps = []
    passes = sorted(set(zip(moves.patch[sel].tolist(), moves.shell[sel].tolist())))

    outer = {(patch, shell) for patch, shell in passes if (patch, shell + 1) not in passes}
    for patch, shell in passes:
        idx = np.flatnonzero(sel & (moves.patch == patch) & (moves.shell == shell))
        a, b = moves.a[idx, :2], moves.b[idx, :2]


        joined = (np.diff(idx) == 1) & (np.linalg.norm(a[1:] - b[:-1], axis=1) < 1e-3)
        first, last = np.r_[True, ~joined], np.r_[~joined, True]
        w = moves.width[idx]
        seams, ends, turns = [], [], []
        for r0, r1 in zip(np.flatnonzero(first), np.flatnonzero(last)):
            p = np.vstack([a[r0], b[r0:r1 + 1]])
            turns += list(road_turns(p))
            starts, opens, closes = road_seams(p, float(np.median(w[r0:r1 + 1])))
            seams += starts
            if not opens:
                ends.append(p[0])
            (seams if closes else ends).append(p[-1])
        res = _pass_blobs(a, b, w, laid[idx], first, last, seams, ends, turns)
        total["passes"] += 1
        total["footprint_mm2"] += res["footprint_mm2"]
        total["blob_mm2"] += res.get("blob_mm2", 0.0)
        total["excess_mm3"] += res.get("excess_mm3", 0.0)
        for c, (area, excess) in res["classes"].items():
            total["classes"][c]["mm2"] += area
            total["classes"][c]["mm3"] += excess
            if (patch, shell) in outer:
                total["outer"]["classes"][c] += area
        if (patch, shell) in outer:
            total["outer"]["footprint_mm2"] += res["footprint_mm2"]
            total["outer"]["blob_mm2"] += res.get("blob_mm2", 0.0)
        steps += res["seams"]
    total["seam_sites"] = len(steps)
    total["seam_step_mm"] = float(np.mean(np.abs(steps))) if steps else None
    total["seam_excess_mm"] = float(np.mean(steps)) if steps else None
    return total


def compute(gcode_path, part_name, parts, head=None, gap=None, run_clearance=False, grid_out=None, ignore_ironing=True,
            metric="v2"):
    result = _compute(gcode_path, part_name, parts, head, gap, run_clearance, grid_out, ignore_ironing, metric)
    _env.release_memory()
    return result


def _compute(gcode_path, part_name, parts, head=None, gap=None, run_clearance=False, grid_out=None, ignore_ironing=True,
            metric="v2"):
    """Ironing passes are left out of the deposited top, the error and the coverage (`ignore_ironing`): a pass
    carries ~10 % flow over a top already laid and adds no height, but as a bead it would stand a full nozzle
    land at nozzle height, over the downhill cells of a slope. They are reported apart (`ironing`).
    `top_error` is the bead-centre metric (`top_metric`, `top_error_v2`) or, with metric="legacy", the cell
    metric before it (`top_error_legacy`, the deposited capsule tops against the mesh at each cell); both are
    always reported."""
    moves = gcode.read(gcode_path) if isinstance(gcode_path, (str, os.PathLike)) else gcode_path
    part = parts["parts"][part_name]
    if part.get("mirror_y"):
        moves.a[:, 1] *= -1
        moves.b[:, 1] *= -1
    extruding = moves.extruding
    res = {"gcode": str(gcode_path), "part": part_name, "moves": len(moves)}
    fd = float(moves.config.get("filament_diameter", "1.75").split(",")[0])
    material = extruding
    e_vol = float(moves.e[material].sum()) * math.pi * fd**2 / 4
    res["extruded_volume_mm3"] = e_vol
    res["mesh_volume_mm3"] = part.get("volume_mm3")
    if part.get("volume_mm3"):
        res["extrusion_ratio"] = e_vol / part["volume_mm3"]
    res["time_s"] = moves.times.get("normal")
    res.update(motion(moves))
    zmax_cfg = moves.config.get("machine_max_speed_z")
    if zmax_cfg:
        res["machine_max_speed_z"] = float(zmax_cfg.split(",")[0])
    ironing = moves.ironing
    conformal = (moves.shell >= 0) & ~(ironing if ignore_ironing else np.zeros(len(moves), bool))
    res["conformal_moves"] = int((conformal & extruding).sum())
    res["conformal_ironing_moves"] = int((conformal & extruding & moves.role_mask({"Ironing"})).sum())
    curved_moves = conformal & extruding
    d = moves.b[curved_moves] - moves.a[curved_moves]
    if len(d):
        res["conformal_path_max_slope_deg"] = float(np.degrees(np.arctan2(np.abs(d[:, 2]), np.linalg.norm(d[:, :2], axis=1))).max())
        res["max_vz_conformal_extruding"] = float(
            (np.abs(d[:, 2]) / np.maximum(np.linalg.norm(d, axis=1), 1e-9) * moves.f[curved_moves] / 60).max())
    res["conformal_tagged"] = bool(conformal.any())
    res["metric_warnings"] = []
    if (moves.width[material & ~moves.role_mask(EXCLUDE_ROLES)] <= 0).any():
        res["metric_warnings"].append("Object extrusion moves without width tags; their surface footprint is unavailable.")
    off = part_offset(moves, part)
    res["offset_xy"] = off.round(3).tolist()
    if gap is None:
        hs = moves.height[conformal & extruding]
        gap = float(np.median(hs)) if len(hs) else float(moves.config.get("layer_height", "0.2"))
    face = part.get("top")
    if face:
        lo, hi = domain_bbox(face["domain"])
        lo, hi = lo + off, hi + off
        shape = tuple(np.ceil((hi - lo) / PITCH).astype(int))
        cx = lo[0] + (np.arange(shape[0]) + 0.5) * PITCH
        cy = lo[1] + (np.arange(shape[1]) + 0.5) * PITCH
        X, Y = np.meshgrid(cx, cy, indexing="ij")
        PX, PY = X - off[0], Y - off[1]
        with np.errstate(invalid="ignore"):
            mesh = g.surface_z(face["surface"], PX, PY) + part.get("offset_z", 0.0)
            gx, gy = g.surface_grad(face["surface"], PX, PY)
        evalm = g.domain_mask(face["domain"], PX, PY, face.get("eval_inset", 2.0)) & np.isfinite(mesh)
        part_sel = moves.extruding & ~moves.role_mask(EXCLUDE_ROLES) & (~ironing if ignore_ironing else True)
        top_sel = part_sel & (moves.role_mask(TOP_ROLES) | conformal)
        dep, dep_top, dep_conf = rasterize(moves, [part_sel, top_sel, part_sel & conformal], lo, shape)
        err = np.where(evalm, dep - mesh, np.nan)
        err_top = np.where(evalm, dep_top - mesh, np.nan)
        res["top_error_legacy"] = stats(err)
        res["top_error_top_role"] = stats(err_top)
        v2, grids = top_metric(moves, part_sel, face, off, part.get("offset_z", 0.0))
        res["top_error_v2"] = v2.pop("top_error")
        res["top_error"] = res["top_error_legacy"] if metric == "legacy" else res["top_error_v2"]
        res["top_v2"] = v2
        res["edge_error"] = v2.pop("edge_error")
        res["coverage_v2"] = coverage_v2(grids)
        excluded = reference_excluded(face, PX, PY)
        res["reference_step_excluded_cells"] = int(excluded.sum())
        res["reference_step_excluded_area_mm2"] = float(excluded.sum() * PITCH ** 2)
        res["eval_cells"] = int(evalm.sum())
        res["uncovered_cells"] = int((evalm & ~np.isfinite(dep)).sum())

        is_conf_top = evalm & np.isfinite(dep_conf) & (np.abs(dep_conf - dep) < 1e-6)

        iron_sel = extruding & ironing
        ironed = evalm & np.isfinite(rasterize(moves, iron_sel, lo, shape))
        res["ironing"] = {"ironed_share": float(ironed.sum() / max(1, evalm.sum())),
                          "length_mm": float(np.linalg.norm(moves.b[iron_sel, :2] - moves.a[iron_sel, :2], axis=1).sum()),
                          "conformal_length_mm": float(np.linalg.norm(moves.b[iron_sel & (moves.shell >= 0), :2] -
                                                                      moves.a[iron_sel & (moves.shell >= 0), :2], axis=1).sum())}
        if head is not None:
            printable = evalm & head_printable(head, np.nan_to_num(gx), np.nan_to_num(gy), gap)
            res["printable_cells"] = int(printable.sum())
            res["coverage"] = float((is_conf_top & printable).sum() / max(1, printable.sum()))
            res["head"] = head.id
            res["head_gap"] = gap
            res["head_slope_map_deg"] = [round(math.degrees(a), 2) for a in head.slope_map(24, gap)]
        res["conformal_area_mm2"] = float(is_conf_top.sum() * PITCH**2)
        res["conformal_top_error"] = stats(np.where(is_conf_top, dep - mesh, np.nan))

        if is_conf_top.sum() > 9:
            zc = np.where(is_conf_top, dep_conf, np.nan)
            sx, sy = np.gradient(zc, PITCH)
            ok = np.isfinite(sx) & np.isfinite(sy)
            mag = np.degrees(np.arctan(np.hypot(sx[ok], sy[ok])))
            az = (np.degrees(np.arctan2(sy[ok], sx[ok])) + 360) % 360
            bins = (az // 15).astype(int)
            res["conformal_slope_by_azimuth_deg"] = {
                str(15 * k): {"max": float(mag[bins == k].max()), "p99": float(np.percentile(mag[bins == k], 99))}
                for k in range(24) if (bins == k).any()}
        if grid_out:
            np.savez_compressed(grid_out, error=err, error_top_role=err_top, mesh=mesh, deposited=dep,
                                conformal=is_conf_top, origin=lo, pitch=PITCH,
                                **{k + "_v2": v for k, v in grids.items()})
    bface = part.get("bottom")
    if bface:
        lo, hi = domain_bbox(bface["domain"])
        lo, hi = lo + off, hi + off
        shape = tuple(np.ceil((hi - lo) / PITCH).astype(int))
        X, Y = np.meshgrid(lo[0] + (np.arange(shape[0]) + 0.5) * PITCH,
                           lo[1] + (np.arange(shape[1]) + 0.5) * PITCH, indexing="ij")
        with np.errstate(invalid="ignore"):
            mesh_b = g.surface_z(bface["surface"], X - off[0], Y - off[1])
        evalb = g.domain_mask(bface["domain"], X - off[0], Y - off[1], bface.get("eval_inset", 2.0)) & np.isfinite(mesh_b)

        sel = extruding & ~moves.role_mask(EXCLUDE_ROLES) & (np.minimum(moves.a[:, 2], moves.b[:, 2]) > 0.5)
        dep_b = rasterize(moves, sel, lo, shape, value="bottom")
        res["bottom_error"] = stats(np.where(evalb, dep_b - mesh_b, np.nan))
    if run_clearance and head is not None:
        t0 = time.monotonic()
        try:
            rep = clearance.sweep(moves, head)
        except RuntimeError as e:
            res["clearance"] = {"error": str(e), "wall_time_s": time.monotonic() - t0}
            return res
        res["clearance"] = {k: rep[k] for k in ("hit_lines", "component_hits", "min_clearance", "samples", "head",
                                                 "omissions", "first_hits", "unmodelled_extrusion_moves")}
        res["clearance"]["wall_time_s"] = time.monotonic() - t0
        res["clearance"]["hit_moves"] = len(rep["hit_moves"])
        res["clearance"]["hit_gcode_lines"] = len(rep["hit_lines"])
        res["clearance"]["hit_lines"] = rep["hit_lines"][:256]
    return res


compute.__doc__ = _compute.__doc__

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("gcode")
    ap.add_argument("--parts", required=True, help="parts.json from make_parts.py")
    ap.add_argument("--part", required=True)
    ap.add_argument("--head", default="", help="head JSON path or library id (default generic-conservative)")
    ap.add_argument("--gap", type=float, help="shell height for the slope limit (default from the G-code)")
    ap.add_argument("--clearance", action="store_true", help="run the independent clearance sweep")
    ap.add_argument("--json", help="write the metrics here")
    ap.add_argument("--grid-out", help="write the error grid (.npz) for report.py")
    a = ap.parse_args()
    parts = json.loads(Path(a.parts).read_text())
    head = heads.find_head(a.head, HEAD_DIRS)
    res = compute(a.gcode, a.part, parts, head, a.gap, a.clearance, a.grid_out)
    text = json.dumps(res, indent=1, default=float)
    if a.json:
        Path(a.json).write_text(text + "\n")
    else:
        print(text)


if __name__ == "__main__":
    main()
