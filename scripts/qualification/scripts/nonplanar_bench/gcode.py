"""Minimal G-code reader for the benchmark. Reads only the G-code text: no slicer internals.

Understands G0/G1/G2/G3 (arcs discretised), G90/G91, G93/G94, M82/M83, G92,
BC- and AC-table forward kinematics (Prodcan3, FR3 cell), and both of Orca's tag
dialects: `;TYPE:` `;WIDTH:` `;HEIGHT:` `;LAYER_CHANGE` `;Z:` and the BBL `; FEATURE:`
`; LINE_WIDTH:` `; LAYER_HEIGHT:` `; CHANGE_LAYER` `; Z_HEIGHT:`, plus `;_NONPLANAR_BEGIN/END`.
"""
from array import array
import math
import re
from dataclasses import dataclass

import _env
import numpy as np

WORD = re.compile(r"([A-Za-z])\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)")
TOP_ROLES = ("Top surface",)
IRONING_ROLES = {"Ironing", "Support ironing"}


@dataclass
class Moves:
    """One row per linear segment. Arcs become several rows sharing a line number."""
    a: np.ndarray
    b: np.ndarray
    e: np.ndarray
    f: np.ndarray
    line: np.ndarray
    role: np.ndarray
    width: np.ndarray
    height: np.ndarray
    layer: np.ndarray
    shell: np.ndarray
    patch: np.ndarray
    roles: list
    config: dict
    times: dict
    joint_a: np.ndarray = None
    joint_b: np.ndarray = None
    axis: np.ndarray = None
    vertical_height: np.ndarray = None
    duration: np.ndarray = None
    layer_z: np.ndarray = None

    def __len__(self):
        return len(self.line)

    @property
    def extruding(self):

        return (self.e > 1e-9) & (np.linalg.norm(self.b - self.a, axis=1) > 1e-9)

    def role_mask(self, names):
        ids = [i for i, r in enumerate(self.roles) if r in names]
        return np.isin(self.role, ids)

    @property
    def ironing(self):
        """Ironing passes (`;TYPE:Ironing` / `; FEATURE: Ironing`, planar or conformal): ~10 % flow over a
        laid top, no bead of their own."""
        return self.role_mask(IRONING_ROLES)


def parse_time(text):
    total = 0.0
    for v, u in re.findall(r"(\d+(?:\.\d+)?)\s*([dhms])", text):
        total += float(v) * {"d": 86400, "h": 3600, "m": 60, "s": 1}[u]
    return total


def read(path_or_lines, arc_step=0.2):
    if isinstance(path_or_lines, list):
        moves = _read(path_or_lines, arc_step)
    else:
        with open(path_or_lines, errors="replace") as source:
            moves = _read(source, arc_step)
    _env.release_memory()
    return moves


def _read(lines, arc_step):
    pos = [0.0, 0.0, 0.0]
    e_abs = 0.0
    absolute, e_relative = True, False
    feed = 0.0
    inverse_time = False
    angles = [0.0, 0.0]
    values, identifiers, move_lines = array("d"), array("i"), array("q")
    inverse = bytearray()
    vertical_height = 0.0
    role, width, height, layer, shell, patch = 0, 0.0, 0.0, -1, -1, -1
    layer_z = math.nan
    roles = ["Undefined"]
    role_id = {"Undefined": 0}
    config, times = {}, {}
    in_config = False

    def set_role(name):
        nonlocal role
        name = name.strip()
        if name not in role_id:
            role_id[name] = len(roles)
            roles.append(name)
        role = role_id[name]

    def emit(p0, p1, de, ln):
        values.extend((p0[0], p0[1], p0[2], p1[0], p1[1], p1[2], de, feed, width, height,
                       angles[0], angles[1], target_angles[0], target_angles[1], vertical_height, layer_z))
        identifiers.extend((role, layer, shell, patch))
        move_lines.append(ln)
        inverse.append(inverse_time and cmd != "G0")

    for ln, raw in enumerate(lines, 1):
        code, _, comment = raw.partition(";")
        c = comment
        if c:
            s = c.strip()
            if c.startswith("TYPE:"):
                set_role(c[5:])
            elif s.startswith("FEATURE:"):
                set_role(s[8:])
            elif c.startswith("WIDTH:") or s.startswith("LINE_WIDTH:"):
                width = float(s.split(":", 1)[1])
            elif c.startswith("HEIGHT:") or s.startswith("LAYER_HEIGHT:"):
                height = float(s.split(":", 1)[1])
            elif s in ("LAYER_CHANGE", "CHANGE_LAYER"):
                layer += 1
            elif c.startswith("Z:") or s.startswith("Z_HEIGHT:"):
                layer_z = float(s.split(":", 1)[1])
            elif s.startswith("_NONPLANAR_BEGIN"):
                kv = dict(t.split("=", 1) for t in s.split()[1:] if "=" in t)
                shell, patch = int(kv.get("shell", 0)), int(kv.get("patch", 0))
            elif s.startswith("_NONPLANAR_END"):
                shell, patch = -1, -1
                vertical_height = 0.0
            elif s.startswith("_NONPLANAR_PATH"):
                vertical_height = 0.0
            elif s.startswith("_NONPLANAR_VERTICAL_HEIGHT:"):
                vertical_height = float(s.split(":", 1)[1])
            elif s.startswith("CONFIG_BLOCK_START"):
                in_config = True
            elif s.startswith("CONFIG_BLOCK_END"):
                in_config = False
            elif s.startswith("estimated printing time"):
                m = re.match(r"estimated printing time \((\w+) mode\)\s*=\s*(.*)", s)
                if m:
                    times[m.group(1)] = parse_time(m.group(2))
            elif s.startswith("model printing time:"):
                m = re.match(r"model printing time:\s*([^;]*);\s*total estimated time:\s*(.*)", s)
                if m:
                    times["normal"] = parse_time(m.group(2))
                    times["model"] = parse_time(m.group(1))
            elif " = " in s and (in_config or not code.strip()):
                k, _, v = s.partition(" = ")
                if re.fullmatch(r"[a-z0-9_]+", k):
                    config[k] = v
        code = code.strip()
        if not code:
            continue
        words = WORD.findall(code)
        if not words:
            continue
        cmd = words[0][0].upper() + str(int(float(words[0][1])))
        args = {k.upper(): float(v) for k, v in words[1:]}
        if cmd in ("G0", "G1", "G2", "G3"):
            if "F" in args:
                feed = args["F"]
            target_angles = angles.copy()

            for i, letters in enumerate(("AB", "C")):
                for k in letters:
                    if k in args:
                        target_angles[i] = args[k] if absolute else angles[i] + args[k]
            target = pos.copy()
            for i, k in enumerate("XYZ"):
                if k in args:
                    target[i] = args[k] if absolute else pos[i] + args[k]
            de = 0.0
            if "E" in args:
                de = args["E"] if e_relative else args["E"] - e_abs
                e_abs = e_abs + args["E"] if e_relative else args["E"]
            if cmd in ("G0", "G1"):
                if target[0] != pos[0] or target[1] != pos[1] or target[2] != pos[2] or de:
                    emit(pos, target, de, ln)
            else:
                pts = _arc(np.asarray(pos), np.asarray(target), args, cmd == "G2", arc_step)
                total = sum(np.linalg.norm(q[:2] - p[:2]) for p, q in zip(pts[:-1], pts[1:])) or 1.0
                for p, q in zip(pts[:-1], pts[1:]):
                    emit(p, q, de * np.linalg.norm(q[:2] - p[:2]) / total, ln)
            pos = target
            angles = target_angles
        elif cmd == "G93":
            inverse_time = True
        elif cmd == "G94":
            inverse_time = False
        elif cmd == "G90":
            absolute = True
        elif cmd == "G91":
            absolute = False
        elif cmd == "M82":
            e_relative = False
        elif cmd == "M83":
            e_relative = True
        elif cmd == "G92":
            for i, k in enumerate("XYZ"):
                if k in args:
                    pos[i] = args[k]
            if "E" in args:
                e_abs = args["E"]
        elif cmd == "G20":
            raise ValueError("inch units are not supported")
    n = len(move_lines)
    data = np.frombuffer(values, dtype=np.float64).reshape(n, 16)
    ids = np.frombuffer(identifiers, dtype=np.int32).reshape(n, 4)
    moves = Moves(data[:, :3].copy(), data[:, 3:6].copy(), data[:, 6].copy(), data[:, 7].copy(),
                  np.array(move_lines, dtype=np.int64), ids[:, 0].copy(), data[:, 8].copy(), data[:, 9].copy(),
                  ids[:, 1].copy(), ids[:, 2].copy(), ids[:, 3].copy(), roles, config, times)
    moves.axis = np.tile([0., 0., 1.], (n, 1))
    moves.vertical_height = data[:, 14].copy()
    moves.layer_z = data[:, 15].copy()
    moves.joint_a, moves.joint_b = moves.a.copy(), moves.b.copy()
    angles_a, angles_b = data[:, 10:12], data[:, 12:14]
    joint_length = np.linalg.norm(moves.b - moves.a, axis=1)
    inv = np.frombuffer(inverse, dtype=np.uint8).astype(bool)
    moves.duration = np.divide(60.0 * joint_length, moves.f, out=np.zeros(n), where=moves.f > 0)
    moves.duration[inv] = np.divide(60.0, moves.f[inv], out=np.zeros(int(inv.sum())), where=moves.f[inv] > 0)
    layout = config.get("nonplanar_kinematics", "three_axis")
    if layout == "table_bc":


        def vector(key, default):
            return np.array([float(v) for v in config.get(key, default).split(",")])
        pivot = vector("rotary_pivot", "0,0,0")
        mount = vector("rotary_tool_mount", "0,0,0")
        mount[2] -= float(config.get("rotary_tool_length", "0"))
        signs = 1 - 2 * vector("rotary_linear_moves_table", "0,0,0")
        home = vector("rotary_axis_home", "0,0")
        reverse = 1 - 2 * vector("rotary_axis_reversed", "0,0")
        axes = []
        for rot in (angles_a, angles_b):
            b, c = np.radians((np.asarray(rot).reshape(-1, 2) - home) * reverse).T
            axes.append(np.column_stack([-np.sin(b) * np.cos(c), np.sin(b) * np.sin(c), np.cos(b)]))
        moves.axis = axes[0] + axes[1]
        moves.axis /= np.maximum(np.linalg.norm(moves.axis, axis=1)[:, None], 1e-12)
        if config.get("rotary_firmware_rtcp", "0") != "1":
            for pts, rot in ((moves.a, angles_a), (moves.b, angles_b)):
                b, c = np.radians((np.asarray(rot).reshape(-1, 2) - home) * reverse).T
                r = pts * signs - pivot + mount
                x = np.cos(b) * r[:, 0] - np.sin(b) * r[:, 2]
                pts[:, 0] = np.cos(c) * x + np.sin(c) * r[:, 1]
                pts[:, 1] = -np.sin(c) * x + np.cos(c) * r[:, 1]
                pts[:, 2] = np.sin(b) * r[:, 0] + np.cos(b) * r[:, 2] - float(config.get("rotary_table_height", "0"))
    elif layout == "table_ac":

        def vector(key, default):
            return np.array([float(v) for v in config.get(key, default).split(",")])
        pivot = vector("rotary_pivot", "0,0,0")
        mount = vector("rotary_tool_mount", "0,0,0")
        mount[2] -= float(config.get("rotary_tool_length", "0"))
        signs = 1 - 2 * vector("rotary_linear_moves_table", "0,0,0")
        home = vector("rotary_axis_home", "0,0")
        reverse = 1 - 2 * vector("rotary_axis_reversed", "0,0")
        axes = []
        for rot in (angles_a, angles_b):
            a, c = np.radians((np.asarray(rot).reshape(-1, 2) - home) * reverse).T
            axes.append(np.column_stack([np.sin(a) * np.sin(c), np.sin(a) * np.cos(c), np.cos(a)]))
        moves.axis = axes[0] + axes[1]
        moves.axis /= np.maximum(np.linalg.norm(moves.axis, axis=1)[:, None], 1e-12)
        if config.get("rotary_firmware_rtcp", "0") != "1":
            for pts, rot in ((moves.a, angles_a), (moves.b, angles_b)):
                a, c = np.radians((np.asarray(rot).reshape(-1, 2) - home) * reverse).T
                r = pts * signs - pivot + mount
                y = np.cos(a) * r[:, 1] + np.sin(a) * r[:, 2]
                z = -np.sin(a) * r[:, 1] + np.cos(a) * r[:, 2]
                pts[:, 0] = np.cos(c) * r[:, 0] + np.sin(c) * y
                pts[:, 1] = -np.sin(c) * r[:, 0] + np.cos(c) * y
                pts[:, 2] = z - float(config.get("rotary_table_height", "0"))
    elif layout not in ("three_axis", "none", ""):
        raise ValueError("benchmark reader does not support rotary layout " + layout)

    length = np.linalg.norm(moves.b - moves.a, axis=1)
    moves.f = np.divide(60 * length, moves.duration, out=moves.f.copy(), where=moves.duration > 0)
    return moves


def _arc(p0, p1, args, cw, step):
    c = p0[:2] + np.array([args.get("I", 0.0), args.get("J", 0.0)])
    if "R" in args:
        raise ValueError("R-form arcs are not supported")
    r = np.linalg.norm(p0[:2] - c)
    a0 = math.atan2(p0[1] - c[1], p0[0] - c[0])
    a1 = math.atan2(p1[1] - c[1], p1[0] - c[0])
    sweep = a1 - a0
    if cw:
        sweep = sweep if sweep < 0 else sweep - 2 * math.pi
    else:
        sweep = sweep if sweep > 0 else sweep + 2 * math.pi
    if np.allclose(p0[:2], p1[:2]):
        sweep = -2 * math.pi if cw else 2 * math.pi
    n = max(2, int(math.ceil(abs(sweep) * r / step)) + 1)
    t = np.linspace(0, 1, n)
    ang = a0 + sweep * t
    pts = np.column_stack([c[0] + r * np.cos(ang), c[1] + r * np.sin(ang), p0[2] + (p1[2] - p0[2]) * t])
    pts[-1] = p1
    return list(pts)
