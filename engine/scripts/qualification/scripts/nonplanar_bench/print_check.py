#!/usr/bin/env python3
"""Pre-print safety report for one G-code file (docs/nonplanar/FIRST_PRINT_MINI.md).

    python3 -I scripts/nonplanar_bench/print_check.py part.gcode [--parts parts.json --part dome] \\
        [--head prusa-mini] [--package part.gcode.3mf] [--planar planar.gcode] [--end-hop 2] [--json report.json]

Runs metrics.py (with the independent clearance sweep for Cartesian printers; rotary printers
require --tool-out with the native pose verifier result) and adds the checks a first print needs:

- max |v_z| as GCodeProcessor plans it (the firmware clamps each axis to its limit) against
  machine_max_speed_z, from nonplanar_slice_tool's output (--tool-out). Commanded values are
  reported too; the conformal emitter limits them itself.
- The C++ export verifier's hit count, from the same output, next to the independent sweep.
- Conformal flow: per conformal extrusion, E x filament area / (bead area x projected length),
  which must equal the flow ratio (vertical-nozzle bead model, PLAN principle 3). Generated rotary
  shells publish their vertical slab thickness separately from the along-tool-axis HEIGHT tag.
- Extruded volume against the mesh, and against a planar slice of the same part when given.
- max_layer_z: the value the end G-code was rendered with (`; max_layer_z = ...`) must be at least
  the highest extrusion Z, or the end G-code lift is computed from too low a height.
- End G-code: after the last `;TYPE:Custom` tag, every XY move must be at least `--end-hop` above
  the highest extrusion Z, and nothing may extrude.
- Firmware sanity (docs/nonplanar/PRINT_PACKAGE.md): no extrusion below the first layer, axis speeds
  within machine_max_speed_*, no move outside the bed, retraction depth within retraction_length,
  M73 monotonic, no extruding G2/G3 with Z outside the conformal shells and none inside them where the
  firmware takes no helix (Klipper, rotary), conformal G-code moves/s within nonplanar_segment_rate.
- Layer markers: total layer number, max_z_height, `; Z_HEIGHT:`/`;Z:` against the extrusion.
- With --rests, the deposition order (rests.py): every road rests on printed material or the bed.
- Header: Bambu's HEADER_BLOCK and CONFIG_BLOCK, or the PrusaSlicer-style header, thumbnails, M862.3
  and footer (M862.3 applies to Prusa); with --package, the .gcode.3mf entries, MD5, slice_info.config and plate_1.json.

Exit status 0 when every check passes, 1 otherwise.
"""
import argparse
import json
import math
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import _env  # noqa: E402,F401
import numpy as np  # noqa: E402

import gcode  # noqa: E402
import heads  # noqa: E402
import metrics  # noqa: E402
import rests  # noqa: E402
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "nonplanar"))
import firmware_check  # noqa: E402
import firmware_sim  # noqa: E402
import slice_json  # noqa: E402

FLOW_TOL = 0.01
PATH_FLOW_LOW = 0.03
PATH_FLOW_TOL = 0.05
TAPER = 1.0
PRESSURE_RETRACT_MAX = 1.25
PRIME = 1.0
BUFFER = 32
SPEED_TOL = 0.02
FLOW_TOL = 0.01


FLOW_TAG_TOL = 0.05
FLAVOUR_TIME_TOL = 0.02


def bead_area(w, h):

    return h * (w - h * (1 - math.pi / 4))


def conformal_flow(moves, lines=None):
    """Ratio of extruded to modelled volume per conformal path (extrusion run) at least 2 mm long.
    The model uses the ;WIDTH/;HEIGHT tags, which round to 0.01 mm and change only past 0.01 mm,
    so single segments scatter by a few percent; whole paths average that out."""
    fd = float(moves.config.get("filament_diameter", "1.75").split(",")[0])
    xy = np.linalg.norm(moves.b[:, :2] - moves.a[:, :2], axis=1)


    sel = (moves.shell >= 0) & moves.extruding & (xy > 1e-6) & ~moves.role_mask({"Ironing"})
    if not sel.any():
        return None
    vol = moves.e * math.pi * fd**2 / 4
    height = np.where(moves.vertical_height > 0, moves.vertical_height, moves.height)
    d = moves.b - moves.a
    along = np.einsum("ij,ij->i", d, moves.axis)
    normal_length = np.sqrt(np.maximum(0, np.einsum("ij,ij->i", d, d) - along * along))
    projected = np.where(moves.vertical_height > 0, xy, normal_length)
    model = bead_area(moves.width, height) * projected


    finish = float(moves.config.get("nonplanar_top_finish_flow", "1").split(",")[0])
    model = np.where(moves.role_mask({"Top surface"}) & (moves.shell >= 0), model * finish, model)


    other = moves.config.get("set_other_flow_ratios", "0") == "1"
    for role, key, gated in (("Top surface", "top_solid_infill_flow_ratio", False),
                             ("Bottom surface", "bottom_solid_infill_flow_ratio", False),
                             ("Internal Bridge", "internal_bridge_flow", False),
                             ("Outer wall", "outer_wall_flow_ratio", True), ("Inner wall", "inner_wall_flow_ratio", True),
                             ("Sparse infill", "sparse_infill_flow_ratio", True),
                             ("Internal solid infill", "internal_solid_infill_flow_ratio", True),
                             ("Gap infill", "gap_fill_flow_ratio", True)):
        if other or not gated:
            model = np.where(moves.role_mask({role}), model * float(moves.config.get(key, "1").split(",")[0]), model)
    runs_all = np.cumsum(~moves.extruding)
    if lines is not None:

        tags = np.array([i for i, l in enumerate(lines, 1) if l.startswith(";_NONPLANAR_PATH")], np.int64)
        if len(tags):
            runs_all = runs_all * (len(tags) + 1) + np.searchsorted(tags, moves.line)

    seg = np.linalg.norm(moves.b - moves.a, axis=1)
    run_ids, inv = np.unique(runs_all[sel], return_inverse=True)
    run_len = np.bincount(inv, seg[sel])
    to_end = np.zeros(len(moves))
    order = np.where(sel)[0]
    rem = run_len.copy()
    for j, r in zip(order, inv):
        to_end[j] = rem[r]
        rem[r] -= seg[j]
    taper = np.full(len(order), TAPER)


    keep = to_end[order] - seg[order] >= taper - 1e-4


    glide = (moves.shell >= 0) & (np.abs(moves.e) < 1e-9) & (seg > 1e-9)
    first = np.r_[True, runs_all[order][1:] != runs_all[order][:-1]]
    after_glide = np.r_[False, glide[:-1]][order]
    keep &= ~(first & after_glide)


    from_start = np.zeros(len(order))
    done = {}
    for n, (j, r) in enumerate(zip(order, inv)):
        from_start[n] = done.get(r, 0.0)
        done[r] = from_start[n] + seg[j]
    keep &= from_start >= PRIME - 1e-4
    sel = sel.copy()
    sel[order[~keep]] = False
    if not sel.any():
        return None
    runs = runs_all[sel]
    _, idx = np.unique(runs, return_inverse=True)
    v, mdl, length = (np.bincount(idx, w[sel]) for w in (vol, model, xy))
    r = v[length >= 2.0] / mdl[length >= 2.0]
    if not len(r):
        return None


    seg = sel & (xy >= 0.2)
    rs = vol[seg] / np.maximum(model[seg], 1e-12)
    return {"paths": int(len(r)), "total": float(v.sum() / mdl.sum()), "median": float(np.median(r)),
            "p01": float(np.percentile(r, 1)), "p99": float(np.percentile(r, 99)),
            "segment_median": float(np.median(rs)) if len(rs) else None,
            "tapered_share": float((rs < 0.95 * np.median(rs)).mean()) if len(rs) else None}


def end_gcode(moves, lines, hop):
    tag = max((i for i, l in enumerate(lines, 1) if l.startswith((";TYPE:Custom", "; FEATURE: Custom"))), default=None)
    ext = moves.extruding

    own = ext & ~moves.role_mask({"Custom"})
    top = float(np.maximum(moves.a[own, 2], moves.b[own, 2]).max())
    if tag is None:
        res = {"max_extrusion_z": top}
        res.update(ok=False, max_layer_z_ok=False, error="no ;TYPE:Custom tag before the end G-code")
        return res

    m = next((re.match(r"; max_layer_z = ([-\d.]+)", l) for l in lines[tag:] if l.startswith("; max_layer_z = ")), None)
    res = {"max_extrusion_z": top, "max_layer_z": float(m.group(1)) if m else None, "hop": hop}
    if m is None:

        z = next((float(z.group(1)) for l in lines[tag:] for z in [re.match(r"G1 Z([-\d.]+)", l)] if z), None)
        res["first_end_z"] = z
        res["max_layer_z_ok"] = z is not None and z >= top + hop - 1e-3
    else:
        res["max_layer_z_ok"] = res["max_layer_z"] >= top - 1e-3
    after = moves.line > tag
    xy = after & (np.linalg.norm(moves.b[:, :2] - moves.a[:, :2], axis=1) > 1e-6)



    idx = np.nonzero(xy)[0]
    if len(idx):
        rest = moves.a[idx[0], :2]
        for i in idx:
            if moves.b[i, 2] > moves.a[i, 2] + 1e-6 and np.linalg.norm(moves.b[i, :2] - rest) <= 2.5:
                xy[i] = False
            else:
                break
    zmin = float(np.minimum(moves.a[xy, 2], moves.b[xy, 2]).min()) if xy.any() else math.inf


    m = next((re.match(r"; max_layer_z = ([-\d.]+)", l) for l in lines[tag:] if l.startswith("; max_layer_z = ")), None)
    res = {"max_extrusion_z": top, "max_layer_z": float(m.group(1)) if m else None, "hop": hop}
    res["max_layer_z_ok"] = (res["max_layer_z"] >= top - 1e-3) if m else bool(zmin >= top + hop - 1e-3)
    if m is None and not is_bambu(moves):



        res["max_layer_z_ok"] = bool(zmin >= top + hop - 1e-3 or not xy.any())
    res.update(start_line=tag, xy_moves=int(xy.sum()), min_xy_z=zmin, extrudes=bool((after & ext).any()))
    res["ok"] = bool((zmin >= top + hop - 1e-3 or not xy.any()) and not res["extrudes"] and res["max_layer_z_ok"])
    return res


def _cfg(moves, key, default=None, index=0):
    v = moves.config.get(key)
    if v is None:
        return default
    try:
        return float(v.split(",")[index])
    except (ValueError, IndexError):
        return default


def planner_segment_rate(moves):
    """nonplanar_segment_rate, or the slicer's default for the flavour when it is 0."""
    v = _cfg(moves, "nonplanar_segment_rate", 0.0)
    if v > 0:
        return v
    flavor = moves.config.get("gcode_flavor", "")
    if flavor == "klipper":
        return 400.0
    if flavor == "reprapfirmware":
        return 250.0
    if is_bambu(moves):
        return 200.0
    return 120.0 if flavor == "marlin2" else 100.0


def firmware_sanity(moves, lines):
    """What the firmware would refuse or do wrong, from the G-code alone (PRINT_PACKAGE.md).

    The object section is everything from the first layer change to the end G-code (the last custom-G-code
    tag); start and end G-code purge and park outside the bed and below the first layer, by design."""
    res = {}
    layer_tags = [i for i, l in enumerate(lines, 1) if l.strip() in (";LAYER_CHANGE", "; CHANGE_LAYER")]
    end_tag = max((i for i, l in enumerate(lines, 1)
                   if l.startswith(";TYPE:Custom") or l.startswith("; FEATURE: Custom")), default=len(lines))
    if layer_tags and end_tag < layer_tags[-1]:
        end_tag = len(lines)
    first = layer_tags[0] if layer_tags else 1
    body = (moves.line > first) & (moves.line < end_tag)



    toolchange = np.zeros(len(moves), bool)
    start = None
    for i, l in enumerate(lines, 1):
        if l.startswith("; CP TOOLCHANGE START") or (start is None and l.startswith("M620 S")):
            start = i
        elif (l.startswith("; CP TOOLCHANGE END") or l.startswith("M621 S")) and start is not None:
            toolchange |= (moves.line >= start) & (moves.line <= i)
            start = None
    res["toolchange_moves"] = int(toolchange.sum())
    body = body & ~toolchange
    custom = moves.role_mask({"Custom"})
    xy_len = np.linalg.norm(moves.b[:, :2] - moves.a[:, :2], axis=1)
    length = np.linalg.norm(moves.b - moves.a, axis=1)
    ext = moves.extruding & body & ~custom



    flh = _cfg(moves, "initial_layer_print_height", 0.2)
    zoff = _cfg(moves, "z_offset", 0.0)
    zlo = np.minimum(moves.a[:, 2], moves.b[:, 2]) - zoff
    low = ext & (zlo < flh - 0.01)
    res["first_layer_z"] = flh
    res["min_extrusion_z"] = float(zlo[ext].min()) if ext.any() else None
    res["extrusions_below_first_layer"] = int(low.sum())
    res["below_first_layer_lines"] = moves.line[low][:20].tolist()






    lim = {k: _cfg(moves, "machine_max_speed_" + k) for k in "xyze"}
    f = moves.f / 60.0
    with np.errstate(invalid="ignore", divide="ignore"):
        comp = {k: np.abs(moves.joint_b[:, i] - moves.joint_a[:, i]) / moves.duration for i, k in enumerate("xyz")}
        comp["e"] = np.abs(moves.e) / np.where(moves.duration > 0, moves.duration, np.nan)
    over = {}
    for k, v in comp.items():
        if not lim[k]:
            continue


        sel = body & (length > 0.05) & (np.nan_to_num(v) > lim[k] * (1 + SPEED_TOL))




        bad = sel & ((moves.e > 1e-9) | ((moves.shell >= 0) & ((k == "z") | (np.abs(moves.e) > 1e-9))))
        over[k] = {"limit": lim[k], "max": float(np.nanmax(np.where(body & (length > 1e-9), v, np.nan))) if body.any() else 0.0,
                   "moves": int(bad.sum()), "planar_travel_moves": int((sel & ~bad).sum()), "lines": moves.line[bad][:10].tolist()}
    res["axis_speed"] = over




    area = moves.config.get("printable_area", "")
    pts = [tuple(float(c) for c in p.split("x")) for p in area.split(",") if "x" in p]
    if pts:
        lo, hi = np.min(pts, 0) - 0.5, np.max(pts, 0) + 0.5
        template = set()
        for key in ("layer_change_gcode", "before_layer_change_gcode", "time_lapse_gcode", "change_filament_gcode",
                    "machine_start_gcode", "machine_end_gcode", "machine_pause_gcode", "template_custom_gcode"):
            template.update(t.split(";", 1)[0].strip() for t in moves.config.get(key, "").split("\\n"))
        template.discard("")
        out = body & (xy_len > 1e-9) & ((moves.b[:, :2] < lo) | (moves.b[:, :2] > hi)).any(1)
        from_template = np.array([lines[ln - 1].split(";", 1)[0].strip() in template for ln in moves.line[out]], bool)
        res["bed"] = [lo.tolist(), hi.tolist()]
        res["moves_outside_bed"] = int((~from_template).sum())
        res["outside_bed_lines"] = moves.line[out][~from_template][:10].tolist()
        res["template_moves_outside_bed"] = int(from_template.sum())
    ph = _cfg(moves, "printable_height")
    if ph:
        res["printable_height"] = ph
        res["moves_above_printable_height"] = int((np.maximum(moves.a[body, 2], moves.b[body, 2]) - zoff > ph + 1e-3).sum())






    rl = _cfg(moves, "filament_retraction_length", None)
    rl = (rl if rl is not None else _cfg(moves, "retraction_length", 0.8)) * PRESSURE_RETRACT_MAX


    change_retract_lines = firmware_check.toolchange_retraction_lines(lines)
    change_retract_limit = firmware_check.toolchange_retract_limit(moves.config)
    depth, worst, worst_line, deep, started = 0.0, 0.0, 0, 0, False



    reloads = iter(i for i, line in enumerate(lines, 1)
                   if re.match(r"M600(?:\s|$)", line.split(";", 1)[0].strip()))
    next_reload = next(reloads, None)
    for e, ln, inb, tc in zip(moves.e, moves.line, body, toolchange):
        while next_reload is not None and ln > next_reload:
            depth = 0.0
            next_reload = next(reloads, None)
        if inb and not started:
            depth, started = 0.0, True
        if tc:

            depth = 0.0
            continue
        depth = depth - e if e < 0 else max(0.0, depth - e)
        if inb and depth > worst:
            worst, worst_line = depth, int(ln)
        limit = change_retract_limit if ln in change_retract_lines else rl
        if inb and depth > limit + 0.01:
            deep += 1
    res["retraction_length"] = rl
    res["max_retraction_depth"] = worst
    res["max_retraction_depth_line"] = worst_line
    res["over_retractions"] = deep



    m73 = {"P": [], "R": [], "Q": [], "S": [], "L": []}
    arcs_z, arcs_conformal, shell = 0, 0, -1


    helix = (is_bambu(moves) or moves.config.get("gcode_flavor") == "marlin2") and \
        moves.config.get("nonplanar_kinematics", "three_axis") in ("three_axis", "none", "")
    for l in lines:
        s = l.split(";", 1)[0].strip()
        c = l.strip()
        if c.startswith(";_NONPLANAR_BEGIN"):
            shell = 0
        elif c.startswith(";_NONPLANAR_END"):
            shell = -1
        if s.startswith("M73 "):
            for k, v in re.findall(r"([PRQSL])\s*(-?\d+(?:\.\d+)?)", s):
                m73[k].append(float(v))
        elif s.startswith(("G2 ", "G3 ", "G2\t", "G3\t")):


            arcs_z += "Z" in s and "E" in s and shell < 0
            arcs_conformal += shell >= 0 and not helix
    mono = lambda xs, up: all(b >= a for a, b in zip(xs, xs[1:])) if up else all(b <= a for a, b in zip(xs, xs[1:]))
    res["m73_count"] = len(m73["P"])
    res["m73_monotonic"] = mono(m73["P"], True) and mono(m73["R"], False) and mono(m73["Q"], True) and mono(m73["S"], False)
    res["m73_layers"] = int(len(m73["L"]))
    res["m73_layers_sequential"] = m73["L"] == [float(i) for i in range(1, len(m73["L"]) + 1)] if m73["L"] else None
    res["arcs_with_z"] = arcs_z
    res["arcs_in_conformal"] = arcs_conformal








    tagged = [int(m.group(1)) for l in lines if l.startswith(";_NONPLANAR_BEGIN") for m in [re.search(r"\brate=(\d+)", l)] if m]
    rate_limit = float(min(tagged)) if tagged else planner_segment_rate(moves)
    res["segment_rate_limit"] = rate_limit
    res["segment_rate_from_tag"] = bool(tagged)
    idx = np.where(ext & (moves.shell < 0))[0]
    t = length[idx] / np.maximum(f[idx], 1e-9)
    res["max_segments_per_s"] = res["max_segments_per_s_conformal"] = 0.0
    if len(t) > 50:
        c = np.concatenate([[0.0], np.cumsum(t)])
        res["max_segments_per_s"] = float((50.0 / np.maximum(c[50:] - c[:-50], 1e-9)).max())
    cidx = np.where(body & (moves.shell >= 0) & (length > 1e-6))[0]
    stall, worst_stall, stalls = 0.0, 0.0, 0
    res["conformal_gcode_moves"] = 0
    if len(cidx):
        tsec = length[cidx] / np.maximum(f[cidx], 1e-9)

        first = np.r_[True, moves.line[cidx][1:] != moves.line[cidx][:-1]]
        pos = np.flatnonzero(first)
        tsec = np.add.reduceat(tsec, pos)
        cidx = cidx[first]
        res["conformal_gcode_moves"] = int(len(cidx))
        key = moves.patch[cidx] * 1000 + moves.shell[cidx]
        start = np.r_[True, key[1:] != key[:-1]]
        k, t_end = 0, 0.0
        for n in range(len(cidx)):
            if start[n]:
                k, t_end = 0, 0.0
            arrival = max(0, k - BUFFER + 1) / rate_limit
            wait = arrival - t_end
            if wait > 1e-4:
                stall += wait
                stalls += 1
                worst_stall = max(worst_stall, wait)
            t_end = max(t_end, arrival) + tsec[n]
            k += 1
        if len(cidx) > 10:
            c = np.concatenate([[0.0], np.cumsum(tsec)])
            rate = 10.0 / np.maximum(c[10:] - c[:-10], 1e-9)
            same = (key[9:] == key[:-9]) & (cidx[9:] - cidx[:-9] == pos[9:] - pos[:-9])
            res["max_segments_per_s_conformal"] = float(rate[same].max()) if same.any() else 0.0
    res["conformal_stalls"] = stalls
    res["conformal_stall_s"] = stall
    res["conformal_worst_stall_s"] = worst_stall
    res["shortest_extrusion_segment_mm"] = float(xy_len[ext].min()) if ext.any() else None

    checks = {
        "no_extrusion_below_first_layer": res["extrusions_below_first_layer"] == 0,
        "axis_speeds_within_machine_max": all(v["moves"] == 0 for v in over.values()),
        "moves_inside_bed": res.get("moves_outside_bed", 0) == 0 and res.get("moves_above_printable_height", 0) == 0,
        "retraction_within_length": res["over_retractions"] == 0,

        "m73_monotonic": (res["m73_count"] > 0 or moves.config.get("disable_m73") == "1") and res["m73_monotonic"],
        "no_arcs_with_z": arcs_z == 0 and arcs_conformal == 0,
        "conformal_feed_without_stalls": res["conformal_stalls"] == 0,
    }
    if res["m73_layers"]:
        checks["m73_layers_sequential"] = bool(res["m73_layers_sequential"])
    res["checks"] = checks
    return res


def layer_markers(moves, lines):
    """Layer-change markers against the material actually printed in each layer: the header's total layer
    number and max_z_height, `; Z_HEIGHT:`/`;Z:` monotone, and the first layer flat. Ordinary extrusion stays
    below the next marker; explicitly marked waves use nominal host planes and bounded field offsets."""
    res = {}
    tags = [(i, l.strip()) for i, l in enumerate(lines, 1) if l.strip() in (";LAYER_CHANGE", "; CHANGE_LAYER")]
    zs = []
    for l in lines:
        m = re.match(r";\s?(?:Z_HEIGHT: ?|Z:)([-\d.]+)", l)
        if m:
            zs.append(float(m.group(1)))
    total = next((int(m.group(1)) for l in lines[:400] for m in [re.match(r"; total layer number: (\d+)", l)] if m), None)
    maxz = next((float(m.group(1)) for l in lines[:400] for m in [re.match(r"; max_z_height: ([-\d.]+)", l)] if m), None)
    zoff = _cfg(moves, "z_offset", 0.0)
    custom = moves.role_mask({"Custom"})
    ext = moves.extruding & ~custom & (moves.layer >= 0)
    top = float(np.maximum(moves.a[ext, 2], moves.b[ext, 2]).max()) - zoff if ext.any() else 0.0
    res.update(layers=len(tags), z_markers=len(zs), header_total_layers=total, header_max_z_height=maxz, max_extrusion_z=top)

    sequential = moves.config.get("print_sequence") == "by object"
    objects = [None] * len(tags)
    if sequential:
        k = -1
        for line in lines:
            if line.strip() in (";LAYER_CHANGE", "; CHANGE_LAYER"):
                k += 1
            elif k >= 0 and line.startswith("; printing object "):
                objects[k] = line

        for k in range(len(objects) - 2, -1, -1):
            if objects[k] is None:
                objects[k] = objects[k + 1]
    labelled = any(obj is not None for obj in objects)
    resets = {k for k in range(1, min(len(zs), len(tags))) if sequential and
              ((labelled and objects[k] is not None and objects[k - 1] is not None and objects[k] != objects[k - 1]) or
               (not labelled and zs[k] < zs[k - 1] - 1e-6 and abs(zs[k] - zs[0]) < 1e-6))}
    res["z_monotonic"] = all(k in resets or zs[k] >= zs[k - 1] - 1e-6 for k in range(1, len(zs)))


    printed = [set() for _ in tags]
    k = -1
    for line in lines:
        if line.strip() in (";LAYER_CHANGE", "; CHANGE_LAYER"):
            k += 1
        elif k >= 0:
            m = re.match(r"; start printing object, unique label id: (\d+)|; printing object (.*?) id:\S+ copy", line)
            if m:
                printed[k].add(m.group(1) or m.group(2))

    def next_marker(k):
        for j in range(k + 1, len(tags)):
            if j in resets:
                return math.inf
            if not printed[k] or not printed[j] or printed[k] & printed[j]:
                return lz[j]
        return math.inf



    wave_amplitudes = {}
    for line in lines[:400]:
        if line.startswith("; nonplanar_wave_result = "):
            try:
                result = json.loads(line.split("=", 1)[1])
                amplitude = float(result["effective_amplitude"])
                if result.get("status") == "applied" and result.get("effective_mode") in {"top", "whole", "infill"} and math.isfinite(amplitude) and amplitude > 0:
                    wave_amplitudes[int(result["object"])] = amplitude
            except (ValueError, TypeError, KeyError):
                pass
    line_amplitude = np.zeros(len(lines) + 1)
    stack, active_amplitude = [], 0.0
    for n, line in enumerate(lines, 1):
        if line.startswith(";_NONPLANAR_BEGIN"):
            stack.append(active_amplitude)
            wave = re.search(r"\bwave=(\d+)\b", line)
            active_amplitude = wave_amplitudes.get(int(wave.group(1)), 0.0) if wave else 0.0
        elif line.startswith(";_NONPLANAR_END"):
            active_amplitude = stack.pop() if stack else 0.0
        line_amplitude[n] = active_amplitude
    amplitude = line_amplitude[moves.line]
    wave_rows = ext & (amplitude > 0)
    wave_bounded = wave_rows & (np.abs(moves.a[:, 2] - moves.layer_z) <= amplitude + .002) & (np.abs(moves.b[:, 2] - moves.layer_z) <= amplitude + .002)
    if wave_rows.any():
        res["wave_extrusion_moves"] = int(wave_rows.sum())
        res["wave_moves_outside_reported_amplitude"] = int((wave_rows & ~wave_bounded).sum())
        res["wave_layer_marker_semantics"] = "nominal host planes; explicit displaced roads remain within the reported effective amplitude"

    excess, excess_layer = 0.0, None
    if zs and ext.any():
        lz = np.array(zs[: len(tags)] + [math.inf])
        hi = np.maximum(moves.a[:, 2], moves.b[:, 2]) - zoff
        for k in range(len(tags)):
            sel = ext & (moves.layer == k)
            if sel.any():
                over = float(hi[sel].max() - lz[k])
                if over > excess:
                    excess, excess_layer = over, k
                nxt = next_marker(k)
                strict = sel & ~wave_bounded
                if strict.any() and hi[strict].max() > nxt + 1e-3:
                    res.setdefault("layers_above_next_marker", []).append(k)
                if (sel & wave_bounded).any() and hi[sel & wave_bounded].max() > nxt + 1e-3:
                    res.setdefault("wave_layers_above_next_nominal_marker", []).append(k)
        first_ranges = []
        for k in [0, *sorted(resets)]:
            first = ext & (moves.layer == k)
            if first.any():


                z1 = np.minimum(moves.a[first, 2], moves.b[first, 2]) - zoff
                stacked = np.abs(z1 - moves.height[first] - z1.min()) < 1e-3
                if k == 0:
                    res["first_layer_stacked_moves"] = int(stacked.sum())
                first[np.flatnonzero(first)[stacked]] = False
                z0 = np.concatenate([moves.a[first, 2], moves.b[first, 2]]) - zoff
                first_ranges.append([float(z0.min()), float(z0.max())])
        if first_ranges:
            res["first_layer_z_range"] = first_ranges[0]
            if resets:
                res["object_first_layer_z_ranges"] = first_ranges
    res["max_extrusion_above_own_marker"] = excess
    res["max_extrusion_above_own_marker_layer"] = excess_layer
    checks = {"z_markers_monotonic": res["z_monotonic"],
              "max_z_height_covers_print": maxz is None or maxz >= top - 0.005,
              "extrusion_below_next_layer_marker": not res.get("layers_above_next_marker")}
    if wave_rows.any():
        checks["wave_extrusion_within_reported_amplitude"] = not res["wave_moves_outside_reported_amplitude"]
    if total is not None:
        checks["total_layer_number_matches"] = total == len(tags)
    if "first_layer_z_range" in res:
        checks["first_layer_flat"] = all(hi - lo < 1e-3 for lo, hi in
                                         res.get("object_first_layer_z_ranges", [res["first_layer_z_range"]]))
    res["checks"] = checks
    return res


def is_bambu(moves):
    return moves.config.get("printer_model", "").startswith("Bambu Lab")


def header_block(lines, bbl):
    """The header Bambu firmware and PrusaSlicer-style viewers read, and the per-flavour printer checks."""
    head = lines[:600]
    text = "\n".join(head)
    res = {"bambu": bbl}
    checks = {}
    if bbl:
        hb = text.split("; HEADER_BLOCK_START", 1)[1].split("; HEADER_BLOCK_END", 1)[0]
        for key in ("model printing time", "total layer number", "max_z_height", "filament:", "filament_density",
                    "filament_diameter"):
            checks["header_" + key.rstrip(":").replace(" ", "_")] = key in hb
        checks["header_placeholders_filled"] = "_PLACEHOLDER" not in hb
        checks["config_block"] = "; CONFIG_BLOCK_START" in "\n".join(lines) and "; CONFIG_BLOCK_END" in "\n".join(lines)
        whole = "\n".join(lines)
        checks["m624_m625_balanced"] = whole.count("\nM624 ") == whole.count("\nM625")
        checks["m1002_markers"] = "M1002 gcode_claim_action" in whole or "M1002 judge_flag" in whole
        checks["m981_spaghetti_on_off"] = "M981 S1" in whole and "M981 S0" in whole
        res["filament_weight_lines"] = [l for l in lines if l.startswith("; total filament weight")][:1]
    else:
        whole = "\n".join(lines)
        checks["generated_by"] = any(l.startswith("; generated by") for l in head)
        thumbs = re.findall(r"; thumbnail(?:_(QOI|JPG))? begin (\d+)x(\d+) (\d+)", whole)
        res["thumbnails"] = [f"{w}x{h} {fmt or 'PNG'}" for fmt, w, h, _ in thumbs]

        cfg_thumbs = re.search(r"^; thumbnails = (.*)$", whole, re.M)
        wanted = cfg_thumbs is None or cfg_thumbs.group(1).strip() not in ("", '""')
        checks["thumbnails"] = (len(thumbs) > 0 and _thumbnails_decode(lines)) if wanted else not thumbs
        m = re.search(r'M862\.3 P\s*"([^"]+)"', whole)
        res["m862_3"] = m.group(1) if m else None
        model = re.search(r"^; printer_model = (.*)$", whole, re.M)
        res["printer_model"] = model.group(1) if model else None



        if model is not None and model.group(1).startswith("Prusa "):
            expected = {"Prusa MINI IS": "MINI"}.get(model.group(1), model.group(1).replace("Prusa ", ""))
            checks["m862_3_matches_printer_model"] = m is not None and expected == m.group(1)
        checks["footer_estimated_time"] = "; estimated printing time (normal mode)" in whole
        checks["footer_filament_used"] = "; filament used [mm]" in whole and "; filament used [g]" in whole
    res["checks"] = checks
    return res


def _thumbnails_decode(lines):
    import base64
    blocks, cur, fmt = [], None, None
    for l in lines:
        m = re.match(r"; thumbnail(?:_(QOI|JPG))? begin", l)
        if m:
            cur, fmt = [], m.group(1) or "PNG"
        elif cur is not None and re.match(r"; thumbnail(?:_(QOI|JPG))? end", l):
            blocks.append((fmt, base64.b64decode("".join(cur))))
            cur = None
        elif cur is not None:
            cur.append(l[2:].strip())
    magic = {"PNG": b"\x89PNG", "QOI": b"qoif", "JPG": b"\xff\xd8"}
    return bool(blocks) and all(data.startswith(magic[fmt]) for fmt, data in blocks)


def check_package(path):
    """A Bambu .gcode.3mf as the printer and Bambu Handy read it."""
    import hashlib
    import xml.etree.ElementTree as ET
    import zipfile
    z = zipfile.ZipFile(path)
    names = set(z.namelist())
    res = {"entries": sorted(names)}
    need = ["[Content_Types].xml", "_rels/.rels", "Metadata/plate_1.gcode", "Metadata/plate_1.gcode.md5",
            "Metadata/slice_info.config", "Metadata/plate_1.json", "Metadata/plate_1.png", "Metadata/plate_no_light_1.png",
            "Metadata/top_1.png", "Metadata/pick_1.png", "Metadata/project_settings.config", "Metadata/model_settings.config"]
    checks = {"entry " + n: n in names for n in need}
    if "Metadata/plate_1.gcode" in names:
        g = z.read("Metadata/plate_1.gcode")
        if "Metadata/plate_1.gcode.md5" in names:
            checks["gcode_md5"] = z.read("Metadata/plate_1.gcode.md5").decode().strip().upper() == hashlib.md5(g).hexdigest().upper()
    if "Metadata/slice_info.config" in names:
        root = ET.fromstring(z.read("Metadata/slice_info.config"))
        plate = root.find("plate")
        meta = {m.get("key"): m.get("value") for m in plate.findall("metadata")} if plate is not None else {}
        res["slice_info"] = meta
        res["slice_info_objects"] = [o.attrib for o in plate.findall("object")] if plate is not None else []
        res["slice_info_filaments"] = [f.attrib for f in plate.findall("filament")] if plate is not None else []
        checks["slice_info_prediction"] = int(meta.get("prediction", "0") or 0) > 0
        checks["slice_info_weight"] = float(meta.get("weight", "0") or 0) > 0
        checks["slice_info_printer_model_id"] = bool(meta.get("printer_model_id"))
        checks["slice_info_not_outside"] = meta.get("outside", "false") == "false"
        checks["slice_info_label_object"] = meta.get("label_object_enabled") == "true"
        checks["slice_info_filament_id"] = all(f.get("tray_info_idx", "").startswith("GF") for f in res["slice_info_filaments"]) \
            and bool(res["slice_info_filaments"])
        if "Metadata/plate_1.gcode" in names:
            ids = set(re.findall(rb"; start printing object, unique label id: (\d+)", g))
            checks["slice_info_identify_ids_match_gcode"] = {o.get("identify_id", "").encode() for o in res["slice_info_objects"]} == ids
    if "Metadata/plate_1.json" in names:
        pj = json.loads(z.read("Metadata/plate_1.json"))
        res["plate_json"] = {k: pj.get(k) for k in ("bbox_all", "bed_type", "first_layer_time", "nozzle_diameter")}
        checks["plate_json_bbox"] = bool(pj.get("bbox_objects")) and all(o.get("layer_height", 0) > 0 for o in pj["bbox_objects"])
    for n in ("Metadata/plate_1.png", "Metadata/top_1.png"):
        if n in names:
            checks["png " + n] = z.read(n).startswith(b"\x89PNG")
    res["checks"] = checks
    return res


def flavour_row(lines, tool):
    """The firmware family's row: its rules (firmware_check) and the curved passes re-planned the way the
    firmware plans them (firmware_sim), against GCodeProcessor's time for the same moves."""
    sim = firmware_sim.simulate(lines)
    row = {"flavour": sim["flavor"], "sim_shell_s": sim["shell_s"], "sim_total_s": sim["total_s"],
           "sim_host_stall_s": sim["host_stall_s"], "checks": {}}
    if tool and "nonplanar_extrude_time_s" in tool:
        est = float(tool["nonplanar_extrude_time_s"]) + float(tool.get("nonplanar_other_time_s", 0))
        row["estimated_shell_s"] = est
        if est > 0:
            row["shell_time_error"] = sim["shell_s"] / est - 1
            row["checks"]["curved_time_within_2pct"] = abs(row["shell_time_error"]) <= FLAVOUR_TIME_TOL
    return row


def tool_summary(path):
    """key -> value lines printed by nonplanar_slice_tool (repeated keys keep the first)."""
    path = Path(path)
    structured = slice_json.read(path if path.suffix == ".json" else path.with_suffix(".json"))
    if structured is not None:
        return slice_json.summary(structured)
    out = {}
    for line in path.read_text().splitlines():
        k, _, v = line.partition(" ")
        out.setdefault(k, v)
    return out


def check(path, parts, part, head, planar=None, hop=None, tool=None, package=None, rests_check=False):
    lines = open(path, errors="replace").read().splitlines()
    if parts is None or part not in parts.get("parts", {}):

        part = part or Path(path).stem
        parts = {"parts": {part: {"bbox": [[0, 0, 0], [0, 0, 0]]}}}
    moves = gcode.read(lines)
    rotary = moves.config.get("nonplanar_kinematics", "three_axis") not in ("three_axis", "none", "")
    res = metrics.compute(path, part, parts, head, run_clearance=not rotary)
    if rotary:


        verified = tool is not None and tool.get("clearance_checked") == "1"
        res["clearance"] = {"scope": "native rotary pose sweep", "ran": verified,
                            "hit_moves": int(tool["clearance_hit_moves"]) if verified else None,
                            "min_clearance": float(tool["clearance_min_mm"]) if verified else None}
    res["conformal_flow"] = conformal_flow(moves, lines)
    res["flow_ratio"] = float(moves.config.get("filament_flow_ratio", "1").split(",")[0]) * \
        float(moves.config.get("print_flow_ratio", "1"))



    model = moves.config.get("printer_model", "")
    default_hop = 0.4 if is_bambu(moves) else 2.0 if "MINI" in model else 0.1
    res["end_gcode"] = end_gcode(moves, lines, hop if hop is not None else default_hop)
    if planar:
        base = metrics.compute(planar, part, parts, None)
        res["planar_extrusion_ratio"] = base.get("extrusion_ratio")
        res["planar_time_s"] = base.get("time_s")
    xy = np.linalg.norm(moves.b[:, :2] - moves.a[:, :2], axis=1)
    conf = (moves.shell >= 0) & moves.extruding & (xy > 1e-6)
    if conf.any():
        d = moves.b[conf] - moves.a[conf]
        res["max_vz_conformal"] = float((np.abs(d[:, 2]) / np.linalg.norm(d, axis=1) * moves.f[conf] / 60).max())
        res["max_bead_height_conformal"] = float(moves.height[conf].max())
    cf = res["conformal_flow"]


    limit = res.get("machine_max_speed_z", math.inf) * (1 + 1e-3)
    tolerance = float(moves.config.get("nonplanar_collision_tolerance", "0") or 0)
    min_clearance = res["clearance"].get("min_clearance")
    res["clearance"]["collision_tolerance"] = tolerance
    native_clear = tool is not None and tool.get("clearance_checked") == "1" and int(tool["clearance_hit_moves"]) == 0
    res["clearance"]["native_exact_depth_clear"] = native_clear
    checks = {"clearance_0_hits": res["clearance"]["hit_moves"] == 0 or
              (tolerance > 0 and ((min_clearance is not None and min_clearance >= -tolerance) or native_clear))}
    if tool:
        res["tool"] = tool
        res["max_vz_planned"] = float(tool["max_vz_planned"])
        checks["vz_within_limit"] = res["max_vz_planned"] <= limit
        if tool.get("clearance_checked") == "1":
            checks["cpp_verifier_0_hits"] = int(tool["clearance_hit_moves"]) == 0
    checks.update({
        "conformal_flow_matches_bead_model": cf is None or (abs(cf["total"] / res["flow_ratio"] - 1) <= FLOW_TOL and
                                                            cf["p01"] / res["flow_ratio"] >= 1 - PATH_FLOW_LOW and
                                                            cf["p99"] / res["flow_ratio"] <= 1 + PATH_FLOW_TOL and
                                                            (cf["segment_median"] is None or
                                                             abs(cf["segment_median"] / res["flow_ratio"] - 1) <= FLOW_TOL)),
        "max_layer_z_covers_print": res["end_gcode"]["max_layer_z_ok"],
        "end_gcode_above_print": res["end_gcode"]["ok"],
    })
    for name, sub in (("firmware", firmware_sanity(moves, lines)), ("layers", layer_markers(moves, lines)),
                      ("header", header_block(lines, is_bambu(moves)))) + ((("package", check_package(package)),) if package else ()):
        res[name] = sub
        checks.update({name + ":" + k: v for k, v in sub["checks"].items()})
    res["firmware_rules"] = firmware_check.check_lines(lines, package=package)
    checks.update({"firmware_rules:" + k: v for k, v in res["firmware_rules"]["checks"].items()})
    res["flavour"] = flavour_row(lines, tool)
    checks.update({"flavour:" + k: v for k, v in res["flavour"]["checks"].items()})
    if rests_check:
        res["rests"] = rests.check(path)
        checks["roads_rest_on_material"] = res["rests"]["ok"]
    res["checks"] = checks
    res["ok"] = all(checks.values())
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("gcode")
    ap.add_argument("--parts", help="parts.json from make_parts.py, for the surface error")
    ap.add_argument("--part", help="part name in --parts (default: the G-code's file stem)")
    ap.add_argument("--head", help="head JSON path or library id (default: the G-code's nonplanar_head)")
    ap.add_argument("--package", help="the .gcode.3mf holding this G-code, to check as Bambu firmware reads it")
    ap.add_argument("--planar", help="planar slice of the same part, for the volume comparison")
    ap.add_argument("--end-hop", type=float, help="lift the end G-code must keep above the print (default 0.4 on Bambu, 2)")
    ap.add_argument("--tool-out", help="slice JSON or stdout for this G-code (a sibling JSON is preferred)")
    ap.add_argument("--rests", action="store_true", help="check that every road rests on printed material or the bed")
    ap.add_argument("--json")
    a = ap.parse_args()
    parts = json.loads(Path(a.parts).read_text()) if a.parts else None
    head_id = a.head or gcode.read(a.gcode).config.get("nonplanar_head", "")
    head = heads.find_head(head_id, metrics.HEAD_DIRS)
    res = check(a.gcode, parts, a.part, head, a.planar, a.end_hop, tool_summary(a.tool_out) if a.tool_out else None,
                a.package, a.rests)
    text = json.dumps(res, indent=1, default=float)
    if a.json:
        Path(a.json).write_text(text + "\n")
    for k, v in res["checks"].items():
        print("%-36s %s" % (k, "PASS" if v else "FAIL"))
    sys.exit(0 if res["ok"] else 1)


if __name__ == "__main__":
    main()
