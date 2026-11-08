#!/usr/bin/env python3
"""Deposition-order rule: every road rests on printed material or the bed.

    python3 -I scripts/nonplanar_bench/rests.py part.gcode [--json out.json]

The clearance verifiers check that the head misses what is printed. This checks the other half of a print
order: that each bead is laid onto something. A curved-layer field ordered by its layers can lay the first
layers of a hanging arm in mid-air, because a layer may start where nothing lies under it (ORIENTATION_FIELD.md
"Print order").

Beads are resampled every 0.25 mm in print order, in the work frame (gcode.py applies the BC table's forward
kinematics), each with its tool axis a, width w and height h. A sample p is supported when material laid before
it lies under p along a, from 0.01 mm to 2 h + 0.1 mm deep (a support interface sits under a 0.2 mm gap) and
within 0.5 w + h sideways (the 45-degree overhang a layer may step out), or when p is on the bed (within 0.75 h
of the lowest bead). On a print with support the deep bound also reaches the support interface under its top gap,
0.5 h + support_top_z_distance + 0.5 h_support + 0.1 mm, and over an interface bead a bead also rests where the
two footprints touch (rests v1.6.10). The shallow bound is for thin curved shells: 0.05 mm beads lie on the shell
under them with their centres closer than half a bead. The previous 2 mm of the same extrusion run do not count,
so a road cannot rest on itself, while a spiral rests on its previous turn.

Support beads count as material and are checked too. Sparse infill, bridges and ironing span by design and
are not checked, nor are internal solid infill and support interfaces, which span the lines of the sparse
infill or support under them. All of them still count as material. A top or bottom surface laid straight onto
sparse infill (a thin floor, where the first solid layer over the infill is the surface itself) spans it the
same way: such a sample rests when sparse infill lies under it on both sides (two infill samples within one
line spacing sideways, in opposite directions), i.e. over the part's interior. At a part's edge the infill is
on one side only, so a surface stepping out over the air still floats.

Floating samples are grouped into clusters (samples within 1 mm). The rule fails when a cluster carries more
than 5 mm of road: a few stray samples are a seam or a bead end, a cluster is a road laid into the air. A cluster
reports its road by each sample's own role (road_mm_by_role, part_mm, support_mm): part and support samples can
share one, and its majority role alone misattributes the rest. The rule
is AtomizedNonplanar's order_check.py made a gate, with the bounds above.
"""
import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import _env  # noqa: E402,F401
import numpy as np  # noqa: E402

import gcode  # noqa: E402

METER = "rests v1.6.10"
STEP = 0.25
CLUSTER_R = 1.0
CLUSTER_ROAD = 5.0
LATERAL_SLACK = 0.05
SPANNING = re.compile(r"sparse|internal solid|support interface|bridge|ironing|skirt|brim|custom|prime|wipe|undefined", re.I)
SKIP = re.compile(r"ironing|skirt|brim|custom|prime|wipe|undefined", re.I)
SURFACE = re.compile(r"top surface|bottom surface", re.I)
SPARSE = re.compile(r"sparse", re.I)


def samples(moves):
    """Bead centres every <= STEP mm in print order, with axis, width, height and a checked flag."""
    keep = moves.extruding & np.array([not SKIP.search(moves.roles[r]) for r in moves.role], bool)
    spans = np.array([bool(SPANNING.search(moves.roles[r])) for r in moves.role], bool)
    idx = np.flatnonzero(keep)
    ax = moves.axis[idx] / np.maximum(np.linalg.norm(moves.axis[idx], axis=1), 1e-12)[:, None]
    h = np.where(moves.height[idx] > 0, moves.height[idx], 0.2)
    w = np.where(moves.width[idx] > 0, moves.width[idx], 0.45)

    a = moves.a[idx] - ax * (h / 2)[:, None]
    b = moves.b[idx] - ax * (h / 2)[:, None]

    run = np.cumsum(np.r_[True, np.diff(idx) > 1])
    k = np.maximum(1, np.ceil(np.linalg.norm(b - a, axis=1) / STEP).astype(int))
    i = np.repeat(np.arange(len(a)), k)
    t = (np.arange(len(i)) - np.repeat(np.cumsum(k) - k, k) + 0.5) / np.repeat(k, k)
    seg_len = np.linalg.norm(b - a, axis=1) / k
    return (a[i] + (b[i] - a[i]) * t[:, None], ax[i], w[i], h[i], ~spans[idx][i], seg_len[i],
            moves.line[idx][i], np.array(moves.roles)[moves.role[idx][i]], run[i])


def infill_spacing(path, default=6.0):
    """Sparse infill line spacing from the G-code's config block: 2 line widths / density (a grid lays half the
    lines each way); `default` mm without one."""
    cfg = {}
    with open(path, errors="replace") as f:
        for l in f:
            m = re.match(r"; (sparse_infill_density|sparse_infill_line_width) = ([0-9.]+)", l)
            if m:
                cfg[m.group(1)] = float(m.group(2))
    rho = cfg.get("sparse_infill_density", 0) / 100
    return 2 * cfg.get("sparse_infill_line_width", 0.45) / rho if rho > 0 else default


def check(path):
    from scipy.spatial import cKDTree
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import connected_components
    moves = gcode.read(str(path))
    c, ax, w, h, checked, road, line, role, run = samples(moves)





    hv = np.maximum(h, float(moves.config.get("layer_height", "0").split(",")[0] or 0))









    win = 2. * hv + 0.1
    on_iface = role == "Support interface"
    sup_beads = moves.extruding & moves.role_mask({r for r in moves.roles if r.lower().startswith("support")}) & (moves.height > 0)
    if sup_beads.any():
        iface = sup_beads & moves.role_mask({"Support interface"})
        h_sup = float(moves.height[iface if iface.any() else sup_beads].max())
        gap = float(moves.config.get("support_top_z_distance", "0").split(",")[0] or 0)
        win = np.maximum(win, 0.5 * h + gap + 0.5 * h_sup + 0.1)
    spacing = infill_spacing(path)
    out = {"meter": METER, "samples": int(len(c))}
    if not len(c):
        out.update({"floating": 0, "floating_mm": 0., "clusters": [], "ok": True})
        return out
    tree = cKDTree(c)



    i0 = int(np.argmin(c[:, 2]))
    bed = c[:, 2] <= c[i0, 2] + 0.5 * h[i0] + 0.005
    floating = np.zeros(len(c), bool)
    todo = np.flatnonzero(checked & ~bed)
    reach = float(win.max() + max(0.5 * w.max() + h.max(), w.max() + LATERAL_SLACK))


    for s0 in range(0, len(todo), 2000):
        part = todo[s0:s0 + 2000]
        for i, nb in zip(part, tree.query_ball_point(c[part], reach)):
            nb = np.asarray(nb, int)
            nb = nb[(nb < i) & ((run[nb] != run[i]) | (nb < i - int(2. / STEP)))]
            if not len(nb):
                floating[i] = True
                continue
            v = c[nb] - c[i]
            along = v @ ax[i]
            side = np.linalg.norm(v - along[:, None] * ax[i], axis=1)




            lateral = np.where(bed[nb], np.maximum(0.5 * w[i] + h[i], 0.5 * (w[i] + w[nb]) + LATERAL_SLACK), 0.5 * w[i] + h[i])
            lateral = np.where(on_iface[nb], np.maximum(lateral, 0.5 * (w[i] + w[nb]) + LATERAL_SLACK), lateral)
            floating[i] = not np.any((side <= lateral) & (along <= -0.01) & (along >= -win[i]))

    surf = np.array([bool(SURFACE.search(x)) for x in role], bool)
    sparse = np.array([bool(SPARSE.search(x)) for x in role], bool)
    for i in np.flatnonzero(floating & surf):
        nb = np.asarray(tree.query_ball_point(c[i], spacing + 2. * h[i] + 0.1), int)
        nb = nb[(nb < i) & sparse[nb]]
        if len(nb) < 2:
            continue
        v = c[nb] - c[i]
        along = v @ ax[i]
        u = v - along[:, None] * ax[i]
        side = np.linalg.norm(u, axis=1)
        k = (along <= -0.01) & (along >= -2. * h[i] - 0.1) & (side <= spacing) & (side > 1e-6)
        if k.sum() >= 2:
            u = u[k] / side[k, None]
            u = u[::max(1, len(u) // 300)]
            floating[i] = not ((u @ u.T).min() < 0)
    f = np.flatnonzero(floating)
    out["floating"] = int(len(f))
    out["floating_mm"] = round(float(road[f].sum()), 1)
    clusters = []
    if len(f):
        pairs = cKDTree(c[f]).query_pairs(CLUSTER_R, output_type="ndarray")
        g = coo_matrix((np.ones(len(pairs)), (pairs[:, 0], pairs[:, 1])), shape=(len(f), len(f)))
        n, label = connected_components(g, directed=False)
        for j in range(n):
            m = f[label == j]
            mm = float(road[m].sum())
            if mm <= CLUSTER_ROAD:
                continue
            roles, counts = np.unique(role[m], return_counts=True)

            by_role = {str(r): round(float(road[m][role[m] == r].sum()), 3) for r in roles}
            support = sum(v for r, v in by_role.items() if r.startswith("Support"))
            clusters.append({"road_mm": round(mm, 1), "lo": c[m].min(0).round(2).tolist(), "hi": c[m].max(0).round(2).tolist(),
                             "first_line": int(line[m].min()), "role": str(roles[np.argmax(counts)]), "road_mm_by_role": by_role,
                             "part_mm": round(mm - support, 1), "support_mm": round(support, 1)})
        clusters.sort(key=lambda x: -x["road_mm"])
    out["clusters"] = clusters
    out["ok"] = not clusters
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("gcode")
    ap.add_argument("--json")
    a = ap.parse_args()
    res = check(a.gcode)
    text = json.dumps(res, indent=1)
    if a.json:
        Path(a.json).write_text(text + "\n")
    print(json.dumps({k: v for k, v in res.items() if k != "clusters"} | {"clusters": len(res["clusters"])}))
    for cl in res["clusters"][:10]:
        print("floating cluster %7.1f mm of %s, z %.2f..%.2f, from line %d" % (cl["road_mm"], cl["role"], cl["lo"][2], cl["hi"][2],
                                                                            cl["first_line"]))
    sys.exit(0 if res["ok"] else 1)


if __name__ == "__main__":
    main()
