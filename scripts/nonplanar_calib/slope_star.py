#!/usr/bin/env python3
"""Emit a supported, three-shell slope-star print without the slicer's slope clipping.

Eight rectangular radial arms rise through five steps bracketing each head prediction by
+/-10 degrees (clamped to 0..80). JSON manifest records the exact arm/step mapping.
Uses stock 0.4 mm nozzle / 1.75 mm filament and manually loaded PLA. Start/end files may
replace the basic Marlin/Bambu startup with already-expanded machine-specific G-code.
"""
import argparse
import datetime
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PRESETS = {
    "mini": ("prusa-mini", 180.0, 180.0),
    "a1": ("bambu-a1", 256.0, 256.0),
    "a1-mini": ("bambu-a1", 180.0, 180.0),
    "p1s": ("bambu-x1-p1", 256.0, 256.0),
}
R0, R1, BASE, WIDTH, PITCH = 6.0, 30.0, 2.0, 4.0, 0.4


def max_slope(head, phi, gap):
    """HeadEnvelope::max_slope for canonical box JSON, including nozzle/margin rules."""
    ux, uy = math.cos(phi), math.sin(phi)
    limit = math.inf
    for box in head["boxes"]:
        if box.get("infinite_x") or box.get("infinite_y"):
            continue
        grow = 0.0 if box.get("nozzle") else head.get("lateral_margin", 0.3)
        lo = [box["min"][i] - grow for i in range(2)]
        hi = [box["max"][i] + grow for i in range(2)]
        projections = [x * ux + y * uy for x in (lo[0], hi[0]) for y in (lo[1], hi[1])]
        pmin, pmax = min(projections), max(projections)
        if pmax <= 0:
            continue
        num = gap + box["min"][2] - (0 if box.get("nozzle") else head.get("margin", 0.3))
        limit = min(limit, num / pmax if num >= 0 else num / pmin if pmin > 0 else -math.inf)
    return math.atan(max(0.0, limit))


def make_arms(head, height):
    arms = []
    for k in range(8):
        phi = k * math.pi / 4
        predicted = math.degrees(max_slope(head, phi, height))
        lo, hi = max(0.0, predicted - 10), min(80.0, predicted + 10)
        if lo > hi:
            raise ValueError("predicted limit too steep for this 80-degree calibration print")
        slopes = [lo + (hi - lo) * j / 4 for j in range(5)]
        arms.append({"azimuth_deg": k * 45, "predicted_max_slope_deg": predicted,
                     "slopes_deg": slopes})
    return arms


def surface(arm, r):
    step = (R1 - R0) / 5
    return arm.get("surface_base", BASE) + sum(max(0.0, min(step, r - R0 - j * step)) * math.tan(math.radians(s))
                      for j, s in enumerate(arm["slopes_deg"]))


def support_start(arm, z, height):
    """First radius where three conformal shells fit above this planar support layer."""
    threshold = z + 3 * height
    if surface(arm, R1) + 1e-9 < threshold:
        return None
    lo, hi = R0, R1
    for _ in range(40):
        mid = (lo + hi) / 2
        if surface(arm, mid) < threshold:
            lo = mid
        else:
            hi = mid
    return hi


class Writer:
    def __init__(self, bed, height, speed, z_speed):
        self.lines = []
        self.center = (bed[0] / 2, bed[1] / 2)
        self.height, self.speed, self.z_speed = height, speed, z_speed
        self.filament_area = math.pi * (1.75 / 2) ** 2
        self.travel_z = 0.0

    def add(self, text):
        self.lines.append(text)

    def stroke(self, points, bead_heights=None):
        if len(points) < 2:
            return
        self.add("G1 E-0.6 F1800")
        self.add(f"G1 Z{self.travel_z:.4f} F{self.z_speed * 60:.1f}")
        x, y, z = points[0]
        self.add(f"G1 X{x + self.center[0]:.4f} Y{y + self.center[1]:.4f} F3000")
        self.add(f"G1 Z{z:.4f} F{self.z_speed * 60:.1f}")
        self.add("G1 E0.6 F1800")
        for i, (x, y, z) in enumerate(points[1:], 1):
            px, py, pz = points[i - 1]
            xy = math.hypot(x - px, y - py)
            length = math.hypot(xy, z - pz)
            h = self.height if bead_heights is None else (bead_heights[i - 1] + bead_heights[i]) / 2
            e = xy * PITCH * h / self.filament_area
            duration = max(length / self.speed, abs(z - pz) / (self.z_speed * 0.99))
            feed = length / duration * 60 if duration else self.speed * 60
            feed = math.floor(feed * 10) / 10
            self.add(f"G1 X{x + self.center[0]:.4f} Y{y + self.center[1]:.4f} Z{z:.4f} E{e:.6f} F{feed:.1f}")


def generate(head, printer, height=0.2, speed=12.0, z_speed=2.0, start=None, end=None):
    """Return (complete G-code, manifest); callable without invoking a CLI or subprocess."""
    if not (0.05 <= height <= 0.3 and math.isfinite(speed) and speed >= 0.1 and math.isfinite(z_speed) and z_speed >= 0.1):
        raise ValueError("use layer height 0.05..0.3 mm and finite speeds of at least 0.1 mm/s")
    if head.get("schema") != "orca.head_envelope.v1" or not head.get("boxes") or head.get("side_profile"):
        raise ValueError("save the head in canonical box format with save_head_envelope first")
    for key in ("margin", "lateral_margin"):
        if not math.isfinite(head.get(key, 0.3)) or head.get(key, 0.3) < 0:
            raise ValueError("head margins must be finite and nonnegative")
    for box in head["boxes"]:
        if not all(math.isfinite(v) for v in box["min"] + box["max"]):
            raise ValueError("head box coordinates must be finite")
    _, bx, by = PRESETS[printer]
    arms = make_arms(head, height)
    for arm in arms:
        arm["surface_base"] = BASE + 3 * height
    reach = max(math.hypot(max(abs(b["min"][0]), abs(b["max"][0])) + (0 if b.get("nozzle") else head.get("lateral_margin", 0.3)),
                           max(abs(b["min"][1]), abs(b["max"][1])) + (0 if b.get("nozzle") else head.get("lateral_margin", 0.3)))
                for b in head["boxes"] if not b.get("infinite_x") and not b.get("infinite_y"))
    pillar_radius = max(34.0, reach + 2.0)
    if pillar_radius + 1.0 > min(bx, by) / 2 - 2:
        raise ValueError("head-reach pillars do not fit this bed with a 2 mm edge clearance")
    max_top = max(surface(a, R1) for a in arms)
    pillar_top = math.ceil((max_top + 1.5) / height) * height
    w = Writer((bx, by), height, speed, z_speed)
    w.travel_z = pillar_top + 3.0
    w.add(f"; slope star: base {head['id']}, printer {printer}, layer height {height}")
    w.add("; SACRIFICIAL CALIBRATION: steps deliberately exceed the predicted head clearance")
    for arm in arms:
        w.add(f"; arm {arm['azimuth_deg']} deg: steps " + ", ".join(f"{s:.4f}" for s in arm["slopes_deg"]))
    if start is None:
        w.add("M17\nM220 S100\nM221 S100\nM140 S60\nM104 S150\nM190 S60\nG28\nM109 S210")
    else:
        w.add(start.rstrip())
    w.add("G21\nG90\nM83\nG92 E0\nM106 S0")
    pillars = [(pillar_radius * math.cos(k * math.pi / 4), pillar_radius * math.sin(k * math.pi / 4)) for k in range(8)]
    base_layers = math.ceil(BASE / height)
    base_top = base_layers * height
    shift = base_top - BASE
    w.travel_z += shift
    for layer in range(1, math.ceil((pillar_top + shift) / height) + 1):
        z = layer * height
        w.add(f"; planar layer {layer}")
        if layer == 3:
            w.add("M106 S255")
        if layer <= base_layers:
            for j in range(-74, 75):
                y = j * PITCH
                x = math.sqrt((R1 - PITCH / 2) ** 2 - y * y)
                w.stroke([(-x, y, z), (x, y, z)])
        else:
            for arm in arms:
                r = support_start(arm, z - shift, height)
                if r is None or r >= R1 - 1e-7:
                    continue
                phi = math.radians(arm["azimuth_deg"])
                ux, uy = math.cos(phi), math.sin(phi)
                for j in range(10):
                    offset = -WIDTH / 2 + PITCH / 2 + j * PITCH
                    w.stroke([(r * ux - offset * uy, r * uy + offset * ux, z),
                              (R1 * ux - offset * uy, R1 * uy + offset * ux, z)])
        for cx, cy in pillars:
            points = [(cx + 0.3 * math.cos(j * math.pi / 8), cy + 0.3 * math.sin(j * math.pi / 8), z) for j in range(17)]
            w.stroke(points)
    for shell in range(3):
        for k, arm in enumerate(arms):
            phi = math.radians(arm["azimuth_deg"])
            ux, uy = math.cos(phi), math.sin(phi)
            w.add(f";_NONPLANAR_BEGIN patch={k} shell={shell}")
            for j in range(10):
                offset = -WIDTH / 2 + PITCH / 2 + j * PITCH
                radii = [R0 + (R1 - R0) * q / 60 for q in range(61)]
                points, gaps = [], []
                for r in radii:
                    z = surface(arm, r) + shift - (2 - shell) * height
                    points.append((r * ux - offset * uy, r * uy + offset * ux, z))
                    support_z = math.floor((surface(arm, r) + shift - 3 * height + 1e-9) / height) * height
                    gaps.append(z - max(base_top, support_z) if shell == 0 else height)
                w.stroke(points, gaps)
            w.add(";_NONPLANAR_END")
    w.add(f"G1 E-0.6 F1800\nG1 Z{w.travel_z:.4f} F{z_speed * 60:.1f}")
    w.add("M400\nM104 S0\nM140 S0\nM106 S0\nM84" if end is None else end.rstrip())
    manifest = {"schema": "orca.slope_star_print.v1", "base_head": head["id"], "printer": printer,
                "layer_height": height, "r0": R0, "r1": R1, "base_height": base_top,
                "pillar_radius": pillar_radius, "pillar_height": pillar_top + shift, "arms": arms}
    return "\n".join(w.lines) + "\n", manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--printer", choices=PRESETS, required=True)
    parser.add_argument("--head", type=Path, help="canonical saved head JSON; defaults to bundled printer head")
    parser.add_argument("--output", type=Path, required=True, help="G-code filename; companion manifest and blank reading are also written")
    parser.add_argument("--layer-height", type=float, default=0.2)
    parser.add_argument("--speed", type=float, default=12.0, help="tip speed mm/s")
    parser.add_argument("--z-speed", type=float, default=2.0, help="maximum Z speed mm/s")
    parser.add_argument("--start-gcode", type=Path, help="already-expanded startup G-code, replacing basic heat/home sequence")
    parser.add_argument("--end-gcode", type=Path)
    args = parser.parse_args()
    head_path = args.head or ROOT / "resources/nonplanar/heads" / (PRESETS[args.printer][0] + ".json")
    try:
        gcode, manifest = generate(json.loads(head_path.read_text()), args.printer, args.layer_height,
                                   args.speed, args.z_speed,
                                   args.start_gcode.read_text() if args.start_gcode else None,
                                   args.end_gcode.read_text() if args.end_gcode else None)
    except (ValueError, KeyError, OSError) as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(gcode)
    args.output.with_suffix(".manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    reading = {"schema": "orca.slope_star_reading.v1", "date": datetime.date.today().isoformat(),
               "arms": [{"azimuth_deg": a["azimuth_deg"], "max_clean_slope_deg": None} for a in manifest["arms"]]}
    args.output.with_suffix(".reading.json").write_text(json.dumps(reading, indent=2) + "\n")
    print(args.output)


if __name__ == "__main__":
    main()
