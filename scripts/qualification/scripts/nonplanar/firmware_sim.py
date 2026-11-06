#!/usr/bin/env python3
"""Re-simulate G-code motion at a firmware's own planner rules (docs/nonplanar/FIRMWARES.md).

Standard library only (python3 -I). Independent of GCodeProcessor: it reads the limits the file runs
with (the preset's machine limits from the config block, then every M201/M203/M204/M205/M566 and
SET_VELOCITY_LIMIT the file sends) and plans the moves the way each firmware does:

- klipper: toolhead.py's lookahead. Junctions from the square corner velocity (junction deviation
  scv^2 (sqrt 2 - 1) / accel) and the approximated centripetal limit of both moves, the extruder's
  instantaneous corner velocity, smoothed acceleration from minimum_cruise_ratio (or ACCEL_TO_DECEL),
  and the Cartesian/CoreXY Z limit: max_z_velocity and max_z_accel scaled by move length / |dz|.
- marlin2 (and Bambu, Marlin-derived): planner.cpp. Per-axis feedrate and acceleration clamps,
  junction deviation with JD_HANDLE_SMALL_SEGMENTS when M205 J > 0, classic jerk otherwise, a
  BLOCK_BUFFER_SIZE-block lookahead that must be able to stop at its last block.
- reprapfirmware: per-axis instantaneous speed change (M566) at junctions, per-axis M201/M203.

Inside the non-planar shells the host feeds blocks at the rate tagged on ;_NONPLANAR_BEGIN into a
buffer, as on the firmware; Klipper's host-side planner is not starved by line rate.

    python3 -I scripts/nonplanar/firmware_sim.py out.gcode [--flavor klipper] [--json out.json]
"""
import argparse
from collections import deque
import json
import math
import re
from pathlib import Path

WORD = re.compile(r'([A-Z])\s*([-+]?(?:\d+(?:\.\d*)?|\.\d+))', re.I)
KLIPPER_PARAM = re.compile(r'([A-Z_]+)\s*=\s*([-+]?(?:\d+(?:\.\d*)?|\.\d+))', re.I)
MARLIN_BUFFER = 16
HOST_BUFFER = 32
MIN_PLANNER_SPEED = 0.05
KLIPPER_CORNER_E = 1.0
KLIPPER_MIN_CRUISE = 0.5
ARC_SEGMENT = 1.0


def arc_segments(radius, sweep, feed, flavor):
    """Firmware-generated planner blocks, before applying acceleration and junction limits.

    Buddy's plan_arc uses 0.02 mm deviation, at least 50 segments/s and 0.1--2 mm chords.
    Bambu's controller model in GCodeProcessor uses 0.0125 mm chord deviation. Fixed 1 mm
    chords can erase the jerk slowdown of a fast arc, even when its length is unchanged.
    """
    if flavor == 'buddy':
        segment = min(2., max(.1, min(math.sqrt(8. * radius * .02), feed / 50.)))
        return max(1, int(abs(sweep) * radius / segment + .8))
    if flavor == 'bambu':
        if radius <= .0125:
            return 1 if abs(sweep) < math.pi or radius * (1. - math.cos(.5 * sweep)) < .0125 else 2
        return max(1, math.ceil(abs(sweep) / (2. * math.acos(1. - .0125 / radius))))
    return max(1, int(abs(sweep) * radius / ARC_SEGMENT))


def first(cfg, key, default=0.):
    try:
        return float(cfg.get(key, '').split(',')[0])
    except ValueError:
        return default


def flavor_of(cfg):
    if cfg.get('printer_model', '').startswith('Bambu Lab') or 'Bambu Lab' in cfg.get('printer_settings_id', ''):
        return 'bambu'
    return cfg.get('gcode_flavor', 'marlin2')


class Move:
    __slots__ = ('d', 'u', 'e', 'v', 'a', 'a_set', 'shell', 'line', 'kin', 'jd', 'scv', 'smooth', 'jerk', 'erate', 'wait',
                 'max_start_v2', 'max_smoothed_v2', 'delta_v2', 'smooth_delta_v2', 'cruise_v2', 't', 'feed')


def parse(lines, cfg, flavor):
    """Moves with their limits as the firmware holds them when it reads each line."""
    vmax = [first(cfg, 'machine_max_speed_' + a) for a in 'xyze']
    amax = [first(cfg, 'machine_max_acceleration_' + a) for a in 'xyze']
    jerk = [first(cfg, 'machine_max_jerk_' + a) for a in 'xyze']
    jd = first(cfg, 'machine_max_junction_deviation') if flavor == 'marlin2' else 0.
    a_print = first(cfg, 'machine_max_acceleration_extruding') or 1000.
    a_travel = first(cfg, 'machine_max_acceleration_travel') or a_print
    a_retract = first(cfg, 'machine_max_acceleration_retracting') or a_print
    arc_flavor = 'buddy' if flavor == 'marlin2' and cfg.get('printer_model', '').startswith('Prusa ') else flavor
    if flavor == 'klipper':

        k_accel = min(x for x in (a_print, amax[0] or a_print, amax[1] or a_print))
        k_velocity = max(vmax[0], vmax[1]) or 500.
        k_scv = jerk[0] or 5.
        k_mcr = KLIPPER_MIN_CRUISE
    rotary = cfg.get('nonplanar_kinematics', 'none') == 'table_bc'
    if rotary:
        vmax += [float(v) for v in cfg.get('rotary_max_speed', '0,0').split(',')]
        amax += [0., 0.]
        jerk += [0., 0.]
    pos = [0.] * (6 if rotary else 4)
    inverse = False
    absolute, rel_e = True, False
    feed = 0.
    shell, rate = False, 0.
    moves = []

    def add(delta, de, line, rotation=(0., 0.)):
        d = math.sqrt(delta[0] ** 2 + delta[1] ** 2 + delta[2] ** 2)
        if rotary and d < 1e-7 and any(rotation):
            d = math.hypot(*rotation)
        m = Move()
        m.wait = 0.
        m.line, m.shell, m.feed = line, shell, rate
        m.e = de
        if d < 1e-7:
            if abs(de) < 1e-9:
                return

            m.kin, m.d = False, abs(de)
            m.u = (0.,) * (5 if rotary else 3)
            m.erate = 1. if de > 0 else -1.
            m.jd, m.jerk = jd, jerk[:]

            v = abs(de) * feed if rotary and inverse else feed
            m.v = min(v, vmax[3] or v)
            m.a = amax[3] if de < 0 and not amax[3] else (a_retract if flavor != 'klipper' else (amax[3] or a_retract))
            m.a = m.a or 1000.
            moves.append(m)
            return
        m.kin, m.d = True, d
        m.u = tuple(c / d for c in (*delta, *rotation)) if rotary else tuple(c / d for c in delta)
        m.erate = de / d
        v = d * feed if rotary and inverse else feed
        if flavor == 'klipper':
            v = min(v, k_velocity)
            a = k_accel
            m.scv = k_scv
            m.jd = k_scv * k_scv * (math.sqrt(2.) - 1.) / k_accel
            a2d = k_accel * (1. - k_mcr)
            if abs(delta[2]) > 1e-9:

                ratio = d / abs(delta[2])
                if vmax[2] > 0: v = min(v, vmax[2] * ratio)
                if amax[2] > 0: a = min(a, amax[2] * ratio)
            m.smooth = min(a2d, a)
        else:
            a = a_print if abs(de) > 1e-9 else a_travel
            m.a_set = a_print
            comp = (*delta, de, *rotation) if rotary else (*delta, de)
            for i in range(len(comp)):
                if comp[i] and vmax[i] > 0:
                    v = min(v, vmax[i] * d / abs(comp[i]))
                if comp[i] and amax[i] > 0:
                    a = min(a, amax[i] * d / abs(comp[i]))
            m.jd = jd
            m.jerk = jerk[:]
        m.v, m.a = max(v, 1e-3), max(a, 1e-3)
        moves.append(m)

    for n, raw in enumerate(lines, 1):
        tag = raw.strip()
        if tag.startswith(';_NONPLANAR_BEGIN'):
            shell = True
            r = re.search(r'\brate=(\d+)', tag)
            rate = float(r[1]) if r else 0.
            continue
        if tag.startswith(';_NONPLANAR_END'):
            shell = False
            continue
        code = raw.split(';', 1)[0].strip()
        if not code:
            continue
        head = code.split(None, 1)[0].upper()
        if head == 'SET_VELOCITY_LIMIT' and flavor == 'klipper':
            p = {k.upper(): float(v) for k, v in KLIPPER_PARAM.findall(code)}
            if 'VELOCITY' in p: k_velocity = p['VELOCITY']
            if 'ACCEL' in p: k_accel = p['ACCEL']
            if 'SQUARE_CORNER_VELOCITY' in p: k_scv = p['SQUARE_CORNER_VELOCITY']

            if 'ACCEL_TO_DECEL' in p: k_mcr = min(max(1. - p['ACCEL_TO_DECEL'] / k_accel, 0.), .99)
            if 'MINIMUM_CRUISE_RATIO' in p: k_mcr = p['MINIMUM_CRUISE_RATIO']
            continue
        m = re.match(r'^([GM])(\d+)(?:\.\d+)?', code, re.I)
        if not m:
            continue
        cmd = m[1].upper() + m[2]
        w = {k.upper(): float(v) for k, v in WORD.findall(code[m.end():])}
        if cmd in ('G4', 'M400'):
            if moves:
                moves[-1].wait += w.get('S', 0.) + w.get('P', 0.) / 1000.
        elif cmd == 'M204':
            if flavor == 'klipper':
                s = w.get('S', min(w.get('P', 1e9), w.get('T', 1e9)))
                if s < 1e9: k_accel = s
            else:
                if 'S' in w: a_print = a_travel = w['S']
                if 'P' in w: a_print = w['P']
                if 'T' in w: a_travel = w['T']
                if 'R' in w: a_retract = w['R']
        elif cmd in ('M201', 'M203', 'M205', 'M566') and flavor != 'klipper':

            scale = 1 / 60. if flavor == 'reprapfirmware' and cmd in ('M203', 'M566') else 1.
            target = {'M201': amax, 'M203': vmax, 'M205': jerk, 'M566': jerk}[cmd]
            for i, ax in enumerate('XYZEBC' if rotary else 'XYZE'):
                if ax in w: target[i] = w[ax] * scale
            if cmd == 'M205' and 'J' in w and flavor == 'marlin2': jd = w['J']
        elif cmd == 'G93': inverse = True
        elif cmd == 'G94': inverse = False
        elif cmd == 'G90': absolute = True
        elif cmd == 'G91': absolute = False
        elif cmd == 'M82': rel_e = False
        elif cmd == 'M83': rel_e = True
        elif cmd == 'G92':
            for i, ax in enumerate('XYZEBC' if rotary else 'XYZE'):
                if ax in w: pos[i] = w[ax]
        elif cmd in ('G0', 'G1', 'G2', 'G3'):
            if 'F' in w and w['F'] > 0: feed = w['F'] / 60.
            new = pos[:]
            for i, ax in enumerate('XYZ'):
                if ax in w: new[i] = w[ax] if absolute else pos[i] + w[ax]
            if rotary:
                for i, ax in enumerate('BC', 4):
                    if ax in w: new[i] = w[ax] if absolute else pos[i] + w[ax]
            if 'E' in w: new[3] = pos[3] + w['E'] if rel_e or not absolute else w['E']
            de = new[3] - pos[3]
            if cmd in ('G2', 'G3') and ('I' in w or 'J' in w):
                cx, cy = pos[0] + w.get('I', 0.), pos[1] + w.get('J', 0.)
                a0 = math.atan2(pos[1] - cy, pos[0] - cx)
                a1 = math.atan2(new[1] - cy, new[0] - cx)
                r = math.hypot(pos[0] - cx, pos[1] - cy)
                sweep = a1 - a0
                if cmd == 'G2' and sweep >= 0: sweep -= 2 * math.pi
                if cmd == 'G3' and sweep <= 0: sweep += 2 * math.pi
                k = arc_segments(r, sweep, feed, arc_flavor)
                last = pos[:]
                for s in range(1, k + 1):
                    f = s / k
                    q = [cx + r * math.cos(a0 + sweep * f), cy + r * math.sin(a0 + sweep * f),
                         pos[2] + (new[2] - pos[2]) * f, pos[3] + de * f]
                    if s == k: q = new[:]
                    add([q[i] - last[i] for i in range(3)], q[3] - last[3], n)
                    last = q
            else:
                add([new[i] - pos[i] for i in range(3)], de, n, tuple(new[i] - pos[i] for i in (4, 5)) if rotary else (0., 0.))
            pos = new
    return moves


def trapezoid(d, a, v0, vc, v1):
    """Time of one block from entry v0 through cruise vc to exit v1 (vc may be unreachable)."""

    v0 = min(v0, vc)
    v1 = min(v1, vc)
    da = (vc * vc - v0 * v0) / (2 * a)
    dd = (vc * vc - v1 * v1) / (2 * a)
    if da + dd > d:

        vp2 = (2 * a * d + v0 * v0 + v1 * v1) / 2
        vp = math.sqrt(max(vp2, max(v0, v1) ** 2))
        return (vp - v0) / a + (vp - v1) / a
    return (vc - v0) / a + (vc - v1) / a + (d - da - dd) / vc


def plan_klipper(moves):
    for m in moves:
        if not m.kin:
            continue
        m.cruise_v2 = m.v * m.v
        m.delta_v2 = 2 * m.d * m.a
        m.smooth_delta_v2 = 2 * m.d * m.smooth
        m.max_start_v2 = 0.
        m.max_smoothed_v2 = 0.
    prev = None
    for m in moves:
        if not m.kin:
            prev = None
            continue
        if prev is not None:
            ms = min(m.cruise_v2, prev.cruise_v2, prev.max_start_v2 + prev.delta_v2)
            diff_r = m.erate - prev.erate
            if abs(diff_r) > 1e-12:
                ms = min(ms, (KLIPPER_CORNER_E / abs(diff_r)) ** 2)
            cos_t = -(m.u[0] * prev.u[0] + m.u[1] * prev.u[1] + m.u[2] * prev.u[2])
            sin_d2 = math.sqrt(max(.5 * (1. - cos_t), 0.))
            cos_d2 = math.sqrt(max(.5 * (1. + cos_t), 0.))
            one_minus = 1. - sin_d2
            if one_minus > 0. and cos_d2 > 0.:
                r_jd = sin_d2 / one_minus
                q = .25 * sin_d2 / cos_d2
                ms = min(ms, r_jd * m.jd * m.a, r_jd * prev.jd * prev.a, m.delta_v2 * q, prev.delta_v2 * q)
            m.max_start_v2 = ms
            m.max_smoothed_v2 = min(ms, prev.max_smoothed_v2 + prev.smooth_delta_v2)
        prev = m

    runs, cur = [], []
    for m in moves:
        if m.kin:
            cur.append(m)
        else:
            if cur: runs.append(cur)
            cur = []
            m.t = trapezoid(m.d, m.a, 0., m.v, 0.)
    if cur: runs.append(cur)
    for q in runs:
        next_end_v2 = next_smoothed_v2 = peak_cruise_v2 = 0.
        delayed = []
        junctions = {}
        for i in range(len(q) - 1, -1, -1):
            m = q[i]
            reachable_start_v2 = next_end_v2 + m.delta_v2
            start_v2 = min(m.max_start_v2, reachable_start_v2)
            reachable_smoothed_v2 = next_smoothed_v2 + m.smooth_delta_v2
            smoothed_v2 = min(m.max_smoothed_v2, reachable_smoothed_v2)
            if smoothed_v2 < reachable_smoothed_v2:
                if smoothed_v2 + m.smooth_delta_v2 > next_smoothed_v2 or delayed:
                    peak_cruise_v2 = min(m.cruise_v2, (smoothed_v2 + reachable_smoothed_v2) * .5)
                    if delayed:
                        mc_v2 = peak_cruise_v2
                        for dm, ms_v2, me_v2 in reversed(delayed):
                            mc_v2 = min(mc_v2, ms_v2)
                            junctions[id(dm)] = (dm, min(ms_v2, mc_v2), mc_v2, min(me_v2, mc_v2))
                        delayed = []
                cruise_v2 = min((start_v2 + reachable_start_v2) * .5, m.cruise_v2, peak_cruise_v2)
                junctions[id(m)] = (m, min(start_v2, cruise_v2), cruise_v2, min(next_end_v2, cruise_v2))
            else:
                delayed.append((m, start_v2, next_end_v2))
            next_end_v2 = start_v2
            next_smoothed_v2 = smoothed_v2
        for dm, s2, c2, e2 in delayed:
            junctions[id(dm)] = (dm, min(s2, c2), c2, min(e2, c2))
        for dm, s2, c2, e2 in junctions.values():
            dm.t = trapezoid(dm.d, dm.a, math.sqrt(s2), math.sqrt(c2), math.sqrt(e2))


def safe_speed(m):
    """planner.cpp: the speed a block may start from or stop at without exceeding any axis jerk."""
    v = m.v
    for ax in range(len(m.jerk)):
        c = abs(m.erate if ax == 3 else m.u[ax if ax < 3 else ax - 1]) * m.v
        if m.jerk[ax] > 0 and c > m.jerk[ax]:
            v = min(v, m.v * m.jerk[ax] / c)
    return v


def unit4(m):
    """Marlin's junction vector: XYZE over the block's length, E included whenever the block extrudes."""
    v = (m.u[0] * m.d, m.u[1] * m.d, m.u[2] * m.d, m.erate * m.d)
    n = math.sqrt(sum(c * c for c in v))
    return tuple(c / n for c in v) if n > 0 else (0., 0., 0., 0.)


def plan_marlin(moves, flavor):
    """Entry speed limits per junction, then the reverse/forward passes. Retracts and primes are blocks like
    any other (planner.cpp plans E-only moves at the retract acceleration, joined by the same rules)."""
    n = len(moves)
    classic = [not (flavor == 'marlin2' and m.jd > 0) for m in moves]
    safe = [safe_speed(m) if c else 0. for m, c in zip(moves, classic)]
    vmax_entry = [0.] * n
    exit_cap = [m.v for m in moves]
    if n:
        exit_cap[-1] = safe[-1]
    prev = None
    for i, m in enumerate(moves):
        if prev is None:
            vmax_entry[i] = safe[i]
        elif not classic[i]:
            ua, ub = unit4(m), unit4(prev)
            cos_t = -sum(a * b for a, b in zip(ua, ub))
            if cos_t > 0.999999:
                v2 = MIN_PLANNER_SPEED ** 2
            else:
                cos_t = max(cos_t, -0.999999)
                sin_d2 = math.sqrt(.5 * (1. - cos_t))
                v2 = m.a * m.jd * sin_d2 / (1. - sin_d2)
                if m.d < 1.:
                    theta = math.acos(-cos_t)
                    if theta > 0:
                        v2 = min(v2, m.d * m.a / theta)
            vmax_entry[i] = math.sqrt(min(v2, m.v * m.v, prev.v * prev.v))
        elif flavor == 'reprapfirmware':

            vj = min(m.v, prev.v)
            factor = 1.
            for ax in range(len(m.jerk)):
                jump = abs((m.erate if ax == 3 else m.u[ax if ax < 3 else ax - 1]) - (prev.erate if ax == 3 else prev.u[ax if ax < 3 else ax - 1])) * vj
                if m.jerk[ax] > 0 and jump * factor > m.jerk[ax]:
                    factor = m.jerk[ax] / jump
            vmax_entry[i] = vj * factor
        else:


            if m.v < prev.v:
                vj, smaller = m.v, m.v / prev.v
            else:
                vj, smaller = prev.v, 1.
            factor, limited = 1., False
            for ax in range(len(m.jerk)):
                v_exit = (prev.erate if ax == 3 else prev.u[ax if ax < 3 else ax - 1]) * prev.v * smaller
                v_entry = (m.erate if ax == 3 else m.u[ax if ax < 3 else ax - 1]) * m.v
                if limited:
                    v_exit *= factor
                    v_entry *= factor
                if flavor == 'bambu' and ax < 3:


                    jerk = abs(v_entry - v_exit)
                elif v_exit > v_entry:
                    jerk = (v_exit - v_entry) if (v_entry > 0 or v_exit < 0) else max(v_exit, -v_entry)
                else:
                    jerk = (v_entry - v_exit) if (v_entry < 0 or v_exit > 0) else max(-v_exit, v_entry)
                if m.jerk[ax] > 0 and jerk > m.jerk[ax]:
                    factor *= m.jerk[ax] / jerk
                    limited = True
            vj *= factor
            if safe[i - 1] > vj * .99 and safe[i] > vj * .99:
                vj = safe[i]
            vmax_entry[i] = vj
        prev = m

    entry = vmax_entry[:]
    for i in range(n - 1, -1, -1):
        m = moves[i]
        nxt = exit_cap[i] if i + 1 >= n else entry[i + 1]
        entry[i] = min(entry[i], math.sqrt(nxt * nxt + 2 * m.a * m.d))
    for i, m in enumerate(moves):
        nk = i + 1 < n
        bound = entry[i + 1] if nk else exit_cap[i]
        exit_v = min(bound, math.sqrt(entry[i] ** 2 + 2 * m.a * m.d))
        if nk:
            entry[i + 1] = exit_v
        m.t = trapezoid(m.d, m.a, entry[i], m.v, exit_v)


def bambu_centripetal(moves):
    """Bambu's MC caps a move's cruise by the centripetal acceleration over the circle through two XY
    directions that turn by less than ~29 degrees (BambuStudio GCodeProcessor, "same with MC side")."""
    prev = None
    for m in moves:
        if m.kin and prev is not None and prev.kin:
            n1, n2 = math.hypot(prev.u[0], prev.u[1]), math.hypot(m.u[0], m.u[1])
            if n1 > 1e-9 and n2 > 1e-9 and m.d * n2 > 1e-4:
                v1 = (prev.u[0] / n1, prev.u[1] / n1)
                v2 = (m.u[0] / n2, m.u[1] / n2)
                if 1e-5 < math.hypot(v2[0] - v1[0], v2[1] - v1[1]) < .5:
                    angle = math.atan2(v1[0] * v2[1] - v1[1] * v2[0], v1[0] * v2[0] + v1[1] * v2[1])
                    r = m.d * n2 * .5 / math.sqrt((1 - math.cos(angle)) * .5)
                    m.v = min(m.v, math.sqrt(m.a_set * r))
        prev = m


def stopping_cap(moves, flavor, buffer=MARLIN_BUFFER):
    """Marlin plans its newest block to stop: at any time the speed allows stopping within the blocks queued."""
    if flavor == 'klipper' or flavor == 'reprapfirmware':
        return

    n = len(moves)
    ahead = 0.
    window = []
    for i in range(n - 1, -1, -1):
        m = moves[i]
        if not m.kin:
            window, ahead = [], 0.
            continue
        window.append(m.d)
        ahead += m.d
        if len(window) > buffer:
            ahead -= window.pop(0)
        cap = math.sqrt(2 * m.a * ahead)
        if cap < m.v:
            m.v = max(cap, 1e-3)


def host_feed(moves):
    """Shell blocks arrive at the tagged rate into a HOST_BUFFER-block queue; a block waits for its line."""
    stall = 0.
    now = 0.
    starts = deque(maxlen=HOST_BUFFER)
    last = -1e9
    previous_line = None
    for m in moves:
        if m.feed <= 0:
            now += m.t
            continue

        interval = 0. if m.line == previous_line else 1. / m.feed
        arrival = max(last + interval, starts[0] if len(starts) == HOST_BUFFER else -1e9)
        start = now
        if m.shell and arrival > now:
            m.t += arrival - now
            stall += arrival - now
            start = arrival
        else:
            arrival = min(arrival, now)
        last = arrival
        previous_line = m.line
        starts.append(start)
        now += m.t
    return stall


def simulate(lines, flavor=None):
    cfg = {mm[1]: mm[2] for line in lines if (mm := re.match(r'^; ([a-z][a-z0-9_]*) = (.*)$', line))}
    flavor = flavor or flavor_of(cfg)
    moves = parse(lines, cfg, flavor)
    if flavor == 'klipper':
        plan_klipper(moves)
        stall = 0.
    else:
        if flavor == 'bambu':
            bambu_centripetal(moves)
        buddy = flavor == 'marlin2' and cfg.get('printer_model', '').startswith('Prusa ')
        buffer = 32 if flavor == 'bambu' or buddy else MARLIN_BUFFER
        stopping_cap(moves, flavor, buffer)
        plan_marlin(moves, flavor)
        stall = 0.
    for m in moves:
        m.t += m.wait
    if flavor != 'klipper':
        stall = host_feed(moves)
    shell = sum(m.t for m in moves if m.shell)

    kinds = {'extrude_s': 0., 'rotary_travel_s': 0., 'travel_s': 0., 'e_only_s': 0.}
    for m in moves:
        kind = 'e_only_s' if not m.kin else 'extrude_s' if m.e > 1e-9 else \
               'rotary_travel_s' if len(m.u) > 3 and any(m.u[3:]) else 'travel_s'
        kinds[kind] += m.t
    return {'flavor': flavor, 'moves': len(moves), 'total_s': sum(m.t for m in moves), **kinds,
            'shell_s': shell, 'shell_moves': sum(1 for m in moves if m.shell), 'host_stall_s': stall}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('gcode')
    ap.add_argument('--flavor', choices=['klipper', 'marlin2', 'reprapfirmware', 'bambu'])
    ap.add_argument('--json')
    args = ap.parse_args()
    res = simulate(Path(args.gcode).read_text().splitlines(), args.flavor)
    if args.json:
        Path(args.json).write_text(json.dumps(res, indent=2) + '\n')
    print(json.dumps(res))


if __name__ == '__main__':
    main()
