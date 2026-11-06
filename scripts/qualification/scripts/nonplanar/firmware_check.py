#!/usr/bin/env python3
"""Offline firmware contract checks for focus-printer G-code and Bambu plate packages.

Uses only the standard library, including under python3 -I. Counts violations per
rule and retains the first eight line numbers so gallery reports stay small.
"""
import argparse
import os
import base64
import hashlib
import json
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET
import zipfile

RESOURCES = Path(os.environ.get("NONPLANAR_RESOURCES_DIR", Path(__file__).resolve().parents[2] / "resources"))

BUDDY = 'https://github.com/prusa3d/Prusa-Firmware-Buddy/blob/1ce23f33ed3b94e26aa33a44557d6c4a4be11eb6/'


BBL = 'https://github.com/bambulab/BambuStudio/blob/47a13eeb1d9af65ef2371e361c856e21d4f6ca72/'
KLIPPER = 'https://github.com/Klipper3d/klipper/blob/v0.13.0/'
MARLIN = 'https://github.com/MarlinFirmware/Marlin/blob/2.1.2.5/'
SOURCES = {
    'commands': BBL + 'resources/profiles/BBL/machine',
    'tags': BBL + 'src/libslic3r/GCode.cpp',
    'package': BBL + 'src/libslic3r/Format/bbs_3mf.cpp',
    'model': BUDDY + 'src/common/gcode/gcode_info.cpp',
    'line': BUDDY + 'include/marlin/Configuration_MINI_adv.h',
    'buddy_commands': BUDDY + 'lib/Marlin/Marlin/src/gcode/gcode.cpp',
    'motion': BUDDY + 'include/marlin/Configuration_MINI.h',
    'segments': BUDDY + 'lib/Marlin/Marlin/src/module/planner.cpp',

    'arcs': BUDDY + 'lib/Marlin/Marlin/src/gcode/motion/G2_G3.cpp',
    'bbl_arcs': BBL + 'src/libslic3r/GCodeWriter.cpp',



    'feed': 'src/libslic3r/GCode.cpp; src/libslic3r/GCodeWriter.cpp; src/libslic3r/GCode/ConformalEmit.cpp',
    'rotary': 'src/libslic3r/GCode/ToolKinematics.cpp; resources/nonplanar/printers/Prodcan3 5-axis BC table.json',
    'inverse_time': 'https://linuxcnc.org/docs/stable/html/gcode/g-code.html#gcode:g93-g94-g95',


    'klipper_kinematics': KLIPPER + 'klippy/kinematics/cartesian.py',
    'klipper_toolhead': KLIPPER + 'klippy/toolhead.py',
    'klipper_commands': KLIPPER + 'docs/G-Codes.md',
    'klipper_arcs': KLIPPER + 'klippy/extras/gcode_arcs.py',
    'marlin_planner': MARLIN + 'Marlin/src/module/planner.cpp',
    'marlin_config': MARLIN + 'Marlin/Configuration_adv.h',
    'marlin_arcs': MARLIN + 'Marlin/src/gcode/motion/G2_G3.cpp',
    'rrf_commands': 'https://docs.duet3d.com/User_manual/Reference/Gcodes',
    'rrf_moves': 'https://github.com/Duet3D/RepRapFirmware/blob/3.5-dev/src/Movement/DDARing.cpp',
    'emitter': 'src/libslic3r/GCode/ConformalEmit.cpp; src/libslic3r/GCode.cpp',
    'retraction': 'docs/nonplanar/TRAVEL.md; src/libslic3r/GCode.cpp (conformal_retract, the prime ramp)',
}
LIMITS = {'a1': (30., 1500., 3.), 'a1mini': (30., 1500., 5.),
          'p1s': (20., 500., 3.), 'mini': (12., 400., 2.), 'prodcan3': (20., 0., 0.)}
MODEL_IDS = {'a1': 'N2S', 'a1mini': 'N1', 'p1s': 'C12'}

FLAVOURS = {'klipper': 'klipper', 'marlin2': 'marlin2', 'marlin': 'marlin2', 'reprapfirmware': 'reprapfirmware'}


BUFFERS = {'marlin2': 16, 'reprapfirmware': 40, 'klipper': 100}

KLIPPER_MCODES = {'M17', 'M18', 'M73', 'M82', 'M83', 'M84', 'M104', 'M105', 'M106', 'M107', 'M109', 'M110',
                  'M112', 'M114', 'M115', 'M117', 'M118', 'M140', 'M141', 'M190', 'M191', 'M204', 'M220', 'M221',
                  'M400', 'M486'}

PRESSURE_RETRACT_MAX = 1.25
ARC_TOLERANCE = .02



HELIX = {'a1', 'a1mini', 'p1s', 'mini', 'marlin2'}
MIN_SEGMENT_S = .001
MIN_SEGMENT_MM = .005
COMMAND = re.compile(r'^([GMT]\d+(?:\.\d+)?)(?=\s|[A-Z]|$)', re.I)
WORDS = re.compile(r'([XYZEB CFSPIJR])\s*([-+]?(?:\d+(?:\.\d*)?|\.\d+))'.replace(' ', ''), re.I)


def configuration(lines):
    return {m[1]: m[2] for line in lines
            if (m := re.match(r'^; ([a-z][a-z0-9_]*) = (.*)$', line))}


def infer_printer(cfg):
    name = cfg.get('printer_model', '') + cfg.get('printer_settings_id', '')
    if 'Prodcan3' in name or cfg.get('nonplanar_head') == 'prodcan3':
        return 'prodcan3'
    if 'A1 mini' in name:
        return 'a1mini'
    if 'A1' in name:
        return 'a1'
    if 'P1S' in name:
        return 'p1s'
    if 'MINI' in name:
        return 'mini'
    if cfg.get('gcode_flavor') in FLAVOURS:
        return FLAVOURS[cfg['gcode_flavor']]
    raise ValueError('cannot identify focus printer; pass --printer')


def stock_mcodes(printer):


    allowed = {'M73', 'M104', 'M106', 'M107', 'M109', 'M140', 'M190', 'M201',
               'M203', 'M204', 'M205', 'M82', 'M83', 'M900', 'M624', 'M625', 'M981'}

    if printer == 'p1s': allowed.add('M142')
    name = {'a1': 'Bambu Lab A1', 'a1mini': 'Bambu Lab A1 mini', 'p1s': 'Bambu Lab P1S'}[printer]
    directory = RESOURCES / 'profiles/BBL/machine'
    def visit(path, seen):
        if path in seen:
            return
        seen.add(path)
        data = json.loads(path.read_text())
        for key, value in data.items():
            if 'gcode' in key and isinstance(value, str):
                allowed.update(re.findall(r'(?m)^\s*(M\d+(?:\.\d+)?)(?=\s|$)', value))
        for include in data.get('include', []):
            visit(directory / (include + '.json'), seen)
        if data.get('inherits'):
            visit(directory / (data['inherits'] + '.json'), seen)
    visit(directory / (name + ' 0.4 nozzle.json'), set())
    return allowed


def toolchange_retraction_lines(lines):
    """The stock set_extruder prelude retracts/wipes before filament_end_gcode and M620.

    Keep checking its depth, using retract_length_toolchange rather than the travel
    length. Stop at the last depositing move, so earlier ordinary retracts retain
    their usual limit. The AMS template itself is checked separately as before.
    """
    result = set()
    filament_end = None
    for n, raw in enumerate(lines):
        tag = raw.strip()
        if tag == '; filament end gcode':
            filament_end = n
        code = raw.split(';', 1)[0].strip()
        if code.startswith('M620 S') and filament_end is not None:
            start = filament_end
            for k in range(filament_end - 1, -1, -1):
                line = lines[k].split(';', 1)[0].strip()
                if lines[k].strip() in (';LAYER_CHANGE', '; CHANGE_LAYER') or line.startswith(('M620 ', 'M621 ')):
                    break
                e = re.search(r'(?<![A-Za-z])E\s*(-?(?:\d*\.)?\d+)', line)
                if e and float(e[1]) > 0:
                    break
                start = k
            result.update(range(start + 1, n + 1))
            filament_end = None
        elif code.startswith('M621 '):
            filament_end = None
    return result


def toolchange_retract_limit(cfg):
    length = cfg.get('filament_retract_length_toolchange', 'nil').split(',')[0]
    if length in ('', 'nil'):
        length = cfg.get('retract_length_toolchange', '0').split(',')[0]
    return float(length)


def check_lines(lines, printer=None, package=None, reference=None, head_path=None):
    cfg = configuration(lines)
    if printer is None:
        try:
            printer = infer_printer(cfg)
        except ValueError:
            return {'printer': None, 'skipped': 'outside focus-printer contract',
                    'rules': {}, 'checks': {}, 'failure_count': 0, 'ok': True}
    generic = printer in BUFFERS
    if not generic and printer not in LIMITS:
        raise ValueError('unsupported printer ' + printer)
    rules = {}
    def rule(name, source):
        rules.setdefault(name, {'source': SOURCES[source], 'failures': 0, 'lines': []})
    def fail(name, line=0):
        r = rules[name]
        r['failures'] += 1
        if len(r['lines']) < 8:
            r['lines'].append(line)
    bbl = printer in MODEL_IDS
    if generic:

        commands, motion = {'klipper': ('klipper_commands', 'klipper_kinematics'),
                            'marlin2': ('marlin_planner', 'marlin_planner'),
                            'reprapfirmware': ('rrf_commands', 'rrf_commands')}[printer]
        for name, source in [('no_g93', commands), ('positive_feed', 'feed'), ('no_negative_print_z', 'emitter'),
                             ('arcs_enabled', 'emitter'), ('axis_feeds', motion), ('z_feed', motion),
                             ('z_acceleration', motion), ('segment_floor', 'emitter'),
                             ('buffer_feed', 'klipper_toolhead' if printer == 'klipper' else 'marlin_planner' if printer == 'marlin2' else 'rrf_moves')]:
            rule(name, source)
        if printer == 'klipper':
            rule('klipper_commands', 'klipper_commands')
            rule('velocity_limit', 'klipper_toolhead')
        else:
            rule('z_jerk', motion)
        if printer == 'marlin2':
            rule('command_length', 'marlin_config')
            rule('min_segment_time', 'marlin_config')
    rule('arc_geometry', {'klipper': 'klipper_arcs', 'reprapfirmware': 'rrf_commands'}.get(printer, 'marlin_arcs'))



    rule('retraction_depth', 'retraction')
    length = cfg.get('filament_retraction_length', 'nil').split(',')[0]
    length = cfg.get('retraction_length', '0.8').split(',')[0] if length in ('', 'nil') else length
    retract_limit = PRESSURE_RETRACT_MAX * float(length) + .01
    change_retract_lines = toolchange_retraction_lines(lines)
    change_retract_limit = toolchange_retract_limit(cfg) + .01
    first_layer = next((n for n, l in enumerate(lines, 1) if l.strip() in (';LAYER_CHANGE', '; CHANGE_LAYER')), len(lines) + 1)
    end_custom = max((n for n, l in enumerate(lines, 1) if l.startswith((';TYPE:Custom', '; FEATURE: Custom'))), default=0)
    body_end = end_custom if end_custom > first_layer and not any(
        l.strip() in (';LAYER_CHANGE', '; CHANGE_LAYER') for l in lines[end_custom:]) else len(lines) + 1
    drawn, toolchange = 0., False


    for name, source in [('no_g93', 'commands' if bbl else 'buddy_commands'), ('positive_feed', 'feed'),
                         ('no_negative_print_z', 'commands' if bbl else 'motion'),
                         ('arcs_enabled', 'bbl_arcs' if bbl else 'arcs' if printer == 'mini' else 'tags'),
                         ('axis_feeds', 'commands' if bbl else 'motion'),
                         ('z_feed', 'commands' if bbl else 'motion'), ('z_acceleration', 'commands' if bbl else 'motion'),
                         ('z_jerk', 'commands' if bbl else 'motion')]:
        if not generic and (printer != 'prodcan3' or name in ('positive_feed', 'arcs_enabled')):
            rule(name, source)
    if bbl:
        for name, source in [('mcode_whitelist', 'commands'), ('bambu_tags', 'tags'),
                             ('start_handshake', 'commands'), ('layer_change_clearance', 'tags'),
                             ('layer_counters', 'tags'), ('object_labels', 'tags')]:
            rule(name, source)
        allowed = stock_mcodes(printer)
    elif printer == 'mini':
        for name, source in [('command_length', 'line'), ('mini_model', 'model'),
                             ('mini_header', 'model'), ('mini_nozzle', 'model'), ('mini_gcode_level', 'model'), ('mini_firmware_version', 'model'), ('mini_features', 'model'),
                             ('segment_steps', 'segments')]:
            rule(name, source)
    else:
        rule('axis_feeds', 'rotary')
        for name, source in [('g93_blocks', 'inverse_time'), ('g94_planar', 'inverse_time'),
                             ('rotary_limits', 'rotary'), ('rotary_speed', 'rotary'), ('rotary_acceleration', 'rotary'),
                             ('rotary_wrap', 'rotary'), ('table_clearance', 'rotary')]:
            rule(name, source)
    rotary_acceleration = {'B': 120., 'C': 240.}
    rotary_acceleration_seen = set()
    rotary_limits = [float(v) for v in cfg.get('rotary_axis_limits', '-60,60,-180,180').split(',')]
    rotary_speeds = [float(v) for v in cfg.get('rotary_max_speed', '28.647,57.295').split(',')]
    pos = dict.fromkeys('XYZBC', 0.)
    known = set()
    absolute, relative_e, inverse, shell, custom = True, False, False, False, True
    epos = 0.
    linear_feed_pending = False
    feed, layer_z, previous_layer_z = 0., None, None
    label_open = layer_block_open = layer_ids_tag = False
    highest_print_z = 0.
    ams_change = False
    text = '\n'.join(lines)
    short_segments = 0
    head_boxes = []


    template_mcodes = {m.upper() for k, v in cfg.items() if k.endswith('_gcode')
                       for m in re.findall(r'(?:^|\\n)\s*(M\d+)\b', v)}
    if printer == 'prodcan3':
        head_boxes = [box for box in json.loads((Path(head_path) if head_path is not None else RESOURCES / 'nonplanar/heads/prodcan3.json').read_text())['boxes']
                      if not box.get('nozzle')]
    if generic:


        vmax, amax, jerk = (float(cfg.get('machine_max_' + k, '0').split(',')[0])
                            for k in ('speed_z', 'acceleration_z', 'jerk_z'))
        vmax, amax, jerk = vmax or math.inf, amax or math.inf, jerk or math.inf
        if printer == 'klipper':
            k_limits = {'VELOCITY': max(float(cfg.get('machine_max_speed_' + a, '0').split(',')[0]) for a in 'xy'),
                        'ACCEL': max(float(cfg.get('machine_max_acceleration_' + a, '0').split(',')[0])
                                     for a in ('x', 'y', 'extruding', 'travel')),
                        'SQUARE_CORNER_VELOCITY': float(cfg.get('machine_max_jerk_x', '0').split(',')[0])}
    else:
        vmax, amax, jerk = LIMITS[printer]
    rate = 0.
    segments = []
    block = 0
    arcs = []
    path_end = set()
    for n, raw in enumerate(lines, 1):
        tag = raw.strip()
        if tag.startswith(';_NONPLANAR_BEGIN'):
            shell = True
            block += 1
            tagged = re.search(r'\brate=(\d+)', tag)
            rate = float(tagged[1]) if tagged else 0.
        elif tag.startswith(';_NONPLANAR_END'):
            shell = False
        if tag.startswith((';_NONPLANAR_PATH', ';_NONPLANAR_END')) and segments:
            path_end.add(len(segments) - 1)
        if tag.startswith((';TYPE:', '; FEATURE:')):
            custom = tag.split(':', 1)[1].strip() == 'Custom'


        if tag.startswith('; CP TOOLCHANGE START') or tag.startswith('M620 S'):
            toolchange = True
        elif toolchange and (tag.startswith('; CP TOOLCHANGE END') or tag.startswith('M621 S')):
            toolchange, drawn = False, 0.
        zm = re.match(r';\s?(?:Z_HEIGHT: ?|Z:)([-\d.]+)', tag)
        if zm:
            previous_layer_z, layer_z = layer_z, float(zm[1])
            if bbl and previous_layer_z is not None and layer_z > float(cfg.get('printable_height', '256')) + .001:
                fail('layer_change_clearance', n)
        if tag.startswith('; object ids of layer'):
            layer_ids_tag = True
        code = raw.split(';', 1)[0].strip()
        if not code:
            continue


        if printer in ('mini', 'marlin2') and len(raw.split(';', 1)[0].encode()) >= 96:
            fail('command_length', n)
        if printer == 'klipper' and code.upper().startswith('SET_VELOCITY_LIMIT'):
            for key, value in re.findall(r'([A-Z_]+)\s*=\s*([-+]?[\d.]+)', code.upper()):
                if k_limits.get(key, 0) > 0 and float(value) > k_limits[key] * 1.001:
                    fail('velocity_limit', n)


        if bbl and re.match(r'M62[01] S\d+A', code):
            ams_change = code.startswith('M620')
        m = COMMAND.match(code)
        if not m:
            continue
        cmd = m[1].upper()
        words = {k.upper(): float(v) for k, v in WORDS.findall(code[m.end():])}


        if cmd == 'M600':
            drawn = 0.



        if bbl and cmd == 'M624':
            if label_open and layer_ids_tag:
                layer_block_open = True
            else:
                if label_open: fail('object_labels', n)
                label_open = True
            layer_ids_tag = False
        if bbl and cmd == 'M625':
            if layer_block_open:
                layer_block_open = False
            else:
                if not label_open: fail('object_labels', n)
                label_open = False
        if bbl and cmd.startswith('M') and cmd not in allowed:
            fail('mcode_whitelist', n)



        if printer == 'klipper' and not custom and cmd.startswith('M') and cmd not in KLIPPER_MCODES | template_mcodes:
            fail('klipper_commands', n)
        if cmd == 'M83': relative_e = True
        if cmd == 'M82': relative_e = False
        if cmd == 'G90': absolute = True
        if cmd == 'G91': absolute = False
        if cmd == 'G93':
            inverse = True
            if printer != 'prodcan3': fail('no_g93', n)
        if cmd == 'G94':
            if printer == 'prodcan3':


                linear_feed_pending = linear_feed_pending or inverse
                if 'F' in words:
                    feed = words['F']
                    if not math.isfinite(feed) or feed <= 0: fail('positive_feed', n)
                    else: linear_feed_pending = False
            inverse = False
        if cmd in ('M201', 'M203', 'M205', 'M566') and 'Z' in words and printer not in ('prodcan3', 'klipper'):

            scale = 1 / 60 if printer == 'reprapfirmware' and cmd in ('M203', 'M566') else 1
            name, limit = {'M201': ('z_acceleration', amax), 'M203': ('z_feed', vmax), 'M205': ('z_jerk', jerk),
                           'M566': ('z_jerk', jerk)}[cmd]
            if (cmd != 'M566' or printer == 'reprapfirmware') and words['Z'] * scale > limit + .001: fail(name, n)
        if printer == 'prodcan3' and cmd == 'M201':
            for axis, limit in rotary_acceleration.items():
                if axis in words:
                    rotary_acceleration_seen.add(axis)
                    if not 0 < words[axis] <= limit + .001:
                        fail('rotary_acceleration', n)
        if cmd == 'G92':
            if 'E' in words: epos = words['E']
            for axis in pos:
                if axis in words: pos[axis] = words[axis]; known.add(axis)
        if cmd not in ('G0', 'G1', 'G2', 'G3'):
            continue
        if cmd in ('G2', 'G3') and (printer not in HELIX if shell else cfg.get('enable_arc_fitting', '0') != '1'):
            fail('arcs_enabled', n)




        if 'F' in words:
            feed = words['F']
            if not math.isfinite(feed) or feed <= 0: fail('positive_feed', n)
            elif not inverse: linear_feed_pending = False
        de = words.get('E', 0.) if relative_e else words.get('E', epos) - epos
        if 'E' in words: epos = epos + words['E'] if relative_e else words['E']
        if first_layer < n < body_end and not toolchange:
            drawn = drawn - de if de < 0 else max(0., drawn - de)
            limit = change_retract_limit if n in change_retract_lines else retract_limit
            if drawn > limit: fail('retraction_depth', n)
        old = pos.copy()
        old_known = known.copy()
        for axis in pos:
            if axis in words:
                pos[axis] = words[axis] if absolute else pos[axis] + words[axis]
                known.add(axis)
        delta = [pos[k] - old[k] for k in 'XYZ']
        length = math.sqrt(sum(d*d for d in delta))
        axis_delta = delta
        if cmd in ('G2', 'G3'):



            if 'I' in words or 'J' in words:
                centre = (old['X'] + words.get('I', 0.), old['Y'] + words.get('J', 0.))
                r0 = math.hypot(old['X'] - centre[0], old['Y'] - centre[1])
                r1 = math.hypot(pos['X'] - centre[0], pos['Y'] - centre[1])
                if abs(r0 - r1) > ARC_TOLERANCE: fail('arc_geometry', n)



                u, w = (old['X'] - centre[0], old['Y'] - centre[1]), (pos['X'] - centre[0], pos['Y'] - centre[1])
                sweep = math.atan2(u[0] * w[1] - u[1] * w[0], u[0] * w[0] + u[1] * w[1]) * (1 if cmd == 'G3' else -1)
                swept = r0 * (sweep if sweep > 0 else sweep + 2 * math.pi)
                length = math.hypot(swept, delta[2])
                axis_delta = [swept, swept, delta[2]]
                if not custom and de > 0:


                    arcs.append((n, (old['X'], old['Y'], old['Z']), (pos['X'], pos['Y'], pos['Z']), centre, cmd == 'G3',
                                 shell or abs(delta[2]) > .001))
            elif 'R' in words:
                if math.hypot(delta[0], delta[1]) > 2 * abs(words['R']) + ARC_TOLERANCE: fail('arc_geometry', n)
            else:
                fail('arc_geometry', n)

            if de > 0 and abs(delta[2]) > .001 and not (shell and printer in HELIX): fail('arc_geometry', n)
        if generic and shell and cmd != 'G0' and length > 1e-9 and feed > 0:
            segments.append((n, length, 60 * length / feed, block, rate))
        if printer != 'prodcan3':
            if not custom and not ams_change and de > 0: highest_print_z = max(highest_print_z, pos['Z'])
            if not custom and pos['Z'] < -.001: fail('no_negative_print_z', n)
            if bbl and not custom and pos['Z'] > float(cfg.get('printable_height', '256')) + .003:
                fail('layer_change_clearance', n)


            if shell and length > 1e-9 and 'Z' in old_known and abs(delta[2]) * feed / (60 * length) > vmax * 1.02:
                fail('z_feed', n)
            if shell and length > 1e-9:
                limits = [float(cfg.get('machine_max_speed_' + axis, '0').split(',')[0]) for axis in 'xyze']
                for d, limit in zip(axis_delta + [de], limits):
                    if limit > 0 and abs(d) * feed / (60 * length) > limit * 1.02:
                        fail('axis_feeds', n)
            if printer == 'mini' and shell and 0 < max(abs(delta[0])*100, abs(delta[1])*100, abs(delta[2])*400, abs(de)*325) < 6:


                short_segments += 1
        else:


            if shell and cmd != 'G0' and length > 1e-9:
                limits = [float(cfg.get('machine_max_speed_' + axis, '0').split(',')[0]) for axis in 'xyze']
                seconds = 60 / feed if inverse and feed > 0 else (60 * length / feed if feed > 0 else math.inf)
                for d, limit in zip(delta + [de], limits):
                    if limit > 0 and abs(d) / seconds > limit * 1.02:
                        fail('axis_feeds', n)
            if any(k in words for k in 'BC'):
                for axis in 'BC':
                    if axis not in rotary_acceleration_seen:
                        fail('rotary_acceleration', n)
                        rotary_acceleration_seen.add(axis)
            moving = any(k in words for k in 'XYZBCE')


            if inverse and cmd != 'G0' and ('F' not in words or words['F'] <= 0):
                fail('g93_blocks', n)
            if not inverse and moving and cmd != 'G0' and linear_feed_pending:
                fail('g94_planar', n)
            if inverse and not shell: fail('g94_planar', n)
            for i, axis in enumerate('BC'):
                lo, hi, speed = rotary_limits[2*i], rotary_limits[2*i+1], rotary_speeds[i]
                if not lo - .0001 <= pos[axis] <= hi + .0001: fail('rotary_limits', n)
                d = abs(pos[axis] - old[axis])
                if inverse and cmd != 'G0' and d * feed / 60 > speed * 1.02: fail('rotary_speed', n)

                if d > 180 and de > 0: fail('rotary_wrap', n)
            if moving and all(k in known for k in 'XYZ'):




                b = math.radians(pos['B'])
                rz = pos['Z'] - 102 + 82.05 - 60
                rx = pos['X'] - 91 - 3
                height = math.sin(b) * rx + math.cos(b) * rz - 6.5



                collision = height < -.003
                steps = max(1, math.ceil(abs(pos['B'] - old['B']) / .25))
                for step in (range(steps + 1) if all(k in old_known for k in "XYZ") else [steps]):
                    u = step / steps
                    bb = math.radians(old['B'] + u * (pos['B'] - old['B']))
                    sn, cs = math.sin(bb), math.cos(bb)
                    x = old['X'] + u * delta[0] - 94
                    z = old['Z'] + u * delta[2] - 79.95
                    h = sn*x + cs*z - 6.5
                    if h < -.003:
                        collision = True
                    for box in head_boxes:
                        support = sn * box['min' if sn >= 0 else 'max'][0] + cs * box['min' if cs >= 0 else 'max'][2]
                        if h + support < .3 - .003:
                            collision = True
                if collision: fail('table_clearance', n)
    if inverse and printer == 'prodcan3': fail('g94_planar', len(lines))
    if generic:

        for k, (n, length, seconds, _, _) in enumerate(segments):


            if length < MIN_SEGMENT_MM and k not in path_end: fail('segment_floor', n)


            if printer == 'marlin2' and seconds < MIN_SEGMENT_S: fail('min_segment_time', n)




        size = BUFFERS[printer]
        start = 0
        while start < len(segments):
            end = start
            while end < len(segments) and segments[end][3] == segments[start][3]:
                end += 1
            run, r = segments[start:end], segments[start][4]
            if r > 0 and printer != 'klipper':
                t_end = 0.
                for k, (n, _, seconds, _, _) in enumerate(run):
                    arrival = max(0, k - size + 1) / r
                    if arrival - t_end > 1e-4: fail('buffer_feed', n)
                    t_end = max(t_end, arrival) + seconds
            elif r > 0 and len(run) > size:
                window = sum(x[2] for x in run[:size])
                for k in range(size, len(run)):
                    if size / max(window, 1e-9) > r * 1.05: fail('buffer_feed', run[k - 1][0])
                    window += run[k][2] - run[k - size][2]
            start = end
    if reference is not None and arcs:
        details = compare_arcs(arcs, reference)
        for n, deviation in details['over']:
            fail('arc_geometry', n)
        rules['arc_geometry']['reference'] = {k: v for k, v in details.items() if k != 'over'}
    if bbl:
        if label_open: fail('object_labels', len(lines))
        changes = sum(line.strip() == '; CHANGE_LAYER' for line in lines)
        total = re.search(r'^; total layer number: (\d+)$', text, re.M)
        counters = [int(m[1]) for m in re.finditer(r'^M73 L(\d+)', text, re.M)]

        announced = [int(m[1]) for m in re.finditer(r'^M991 S0 P(\d+)', text, re.M)]
        if not total or int(total[1]) != changes or counters != list(range(1, changes + 1)) or [n + 1 for n in announced] != counters:
            fail('layer_counters')
        zs = [float(m[1]) for m in re.finditer(r'^; Z_HEIGHT: ([-\d.]+)', text, re.M)]
        maxz = re.search(r'^; max_z_height: ([-\d.]+)', text, re.M)

        restart = float(cfg.get('initial_layer_print_height', '0')) + .001 if cfg.get('print_sequence') == 'by object' else -math.inf
        if len(zs) != changes or any(b < a and b > restart for a, b in zip(zs, zs[1:])):
            fail('layer_change_clearance')
        if not maxz or float(maxz[1]) < highest_print_z - float(cfg.get('z_offset', '0')) - .005:
            fail('layer_change_clearance')
        required = ['; HEADER_BLOCK_START', '; HEADER_BLOCK_END', '; CONFIG_BLOCK_START',
                    '; CONFIG_BLOCK_END', '; total layer number:', '; max_z_height:',
                    '; CHANGE_LAYER', '; Z_HEIGHT:', '; LAYER_HEIGHT:', '; FEATURE:']
        for tag in required:
            if not any(line.startswith(tag) for line in lines): fail('bambu_tags')
        for begin, end in [('; HEADER_BLOCK_START', '; HEADER_BLOCK_END'), ('; CONFIG_BLOCK_START', '; CONFIG_BLOCK_END')]:
            starts = [i for i, line in enumerate(lines) if line == begin]
            ends = [i for i, line in enumerate(lines) if line == end]
            if len(starts) != 1 or len(ends) != 1 or starts[0] >= ends[0]: fail('bambu_tags')


        start = '\n'.join(line.split(';', 1)[0].strip() for line in text.split('; CHANGE_LAYER', 1)[0].splitlines())
        for token in ['G28', 'M109', 'M190', 'G29', 'G29.2 S1',
                      'M1002 judge_flag g29_before_print_flag', 'M622 J1', 'M623']:
            if token not in start: fail('start_handshake')
    if printer == 'mini':
        if not re.search(r'M862\.3\s+P\s*"MINI"', text): fail('mini_model')
        for m in re.finditer(r'M862\.2\s+P\s*(\d+)', text):
            if int(m[1]) != 120: fail('mini_model')
        for m in re.finditer(r'M862\.3\s+P\s*"([^"]+)"', text):
            if m[1] != 'MINI': fail('mini_model')


        for m in re.finditer(r'M862\.4\s+P\s*"?(\d+)\.(\d+)\.(\d+)', text):
            if tuple(map(int, m.groups())) > (6, 10, 1): fail('mini_firmware_version')


        for m in re.finditer(r'M862\.5\s+P\s*(\d+)', text):
            if int(m[1]) > 2: fail('mini_gcode_level')
        for m in re.finditer(r'M862\.6\s+P\s*"([^"]+)"', text):
            if m[1].strip().lower() != 'input shaper': fail('mini_features')
        nozzle = float(cfg.get('nozzle_diameter', '.4').split(',')[0])
        for m in re.finditer(r'M862\.1\s+P\s*([\d.]+)', text):
            if abs(float(m[1]) - nozzle) > .001: fail('mini_nozzle')
        for token in ['; generated by', '; estimated printing time (normal mode)', '; filament used [mm]', '; filament used [g]']:
            if token not in text: fail('mini_header')
    if package:
        rule('bambu_package', 'package')
        try:
            with zipfile.ZipFile(package) as z:
                if z.read('Metadata/plate_1.gcode').decode().splitlines() != lines:
                    fail('bambu_package')
                    rules['bambu_package'].setdefault('details', []).append('package G-code differs from checked text')
        except (OSError, KeyError, ValueError, zipfile.BadZipFile) as e:
            fail('bambu_package')
            rules['bambu_package'].setdefault('details', []).append(str(e))
        for error in check_package(package, printer):
            fail('bambu_package')
            rules['bambu_package'].setdefault('details', []).append(error)
    return {'printer': printer, 'rules': rules, 'short_segments_coalesced': short_segments,
            'checks': {k: v['failures'] == 0 for k, v in rules.items()},
            'failure_count': sum(v['failures'] for v in rules.values()),
            'ok': all(v['failures'] == 0 for v in rules.values())}


SNAP = .03
CELL = 2.


def cell(p):
    return (int(math.floor(p[0] / CELL)), int(math.floor(p[1] / CELL)), round(p[2], 2))


def segment_distance(p, a, b):
    d = [y - x for x, y in zip(a, b)]
    dd = sum(c * c for c in d)
    t = min(max(sum((pc - ac) * dc for pc, ac, dc in zip(p, a, d)) / dd, 0.), 1.) if dd > 0 else 0.
    return math.dist(p, [ac + t * dc for ac, dc in zip(a, d)])


def near(index, points, p):
    """Reference segments (k, k + 1) passing within SNAP of p, in order."""
    c = cell(p)
    found = set()
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for i in index.get((c[0] + dx, c[1] + dy, c[2]), []):
                for k in (i - 1, i):
                    if 0 <= k < len(points) - 1 and segment_distance(p, points[k], points[k + 1]) <= SNAP:
                        found.add(k)
    return sorted(found)


def positions(lines):
    """XYZ after every motion line of an arc-free export, with each point's indices."""
    pos, absolute, out, index = [0., 0., 0.], True, [], {}
    for raw in lines:
        code = raw.split(';', 1)[0].strip()
        m = COMMAND.match(code)
        if not m:
            continue
        cmd = m[1].upper()
        if cmd == 'G90': absolute = True
        elif cmd == 'G91': absolute = False
        elif cmd in ('G0', 'G1', 'G2', 'G3', 'G92'):
            words = {k.upper(): float(v) for k, v in WORDS.findall(code[m.end():])}
            for i, axis in enumerate('XYZ'):
                if axis in words: pos[i] = words[axis] if absolute or cmd == 'G92' else pos[i] + words[axis]
            if cmd != 'G92':
                index.setdefault(cell(pos), []).append(len(out))
                out.append(tuple(pos))
    return out, index


def compare_arcs(arcs, reference):
    """Re-expand every fitted arc as the firmware does (a helix about its centre, Z linear in the sweep) and
    measure the arc-free export's polyline between the same end points against it. That polyline runs from
    a vertex within SNAP of the arc's start to the first one within SNAP of its end, every vertex on the way
    within 0.1 mm of the circle (a crossing road is a line width away)."""
    points, index = positions(reference)
    worst, worst_planar, matched, over = 0., 0., 0, []
    for n, a, b, centre, ccw, curved in arcs:
        r = math.hypot(a[0] - centre[0], a[1] - centre[1])
        a0 = math.atan2(a[1] - centre[1], a[0] - centre[0])
        sweep = math.atan2(b[1] - centre[1], b[0] - centre[0]) - a0
        if ccw and sweep <= 1e-12: sweep += 2 * math.pi
        if not ccw and sweep >= -1e-12: sweep -= 2 * math.pi

        slack = SNAP / max(r * abs(sweep), 1e-9)

        def helix_distance(p):

            angle = math.atan2(p[1] - centre[1], p[0] - centre[0]) - a0
            angle = angle % (2 * math.pi) if sweep > 0 else -(-angle % (2 * math.pi))
            u = angle / sweep
            if u > 1. + slack and u - 2 * math.pi / abs(sweep) >= -slack:
                u -= 2 * math.pi / abs(sweep)
            if not -slack <= u <= 1. + slack:
                return None
            u = min(max(u, 0.), 1.)
            q = (centre[0] + r * math.cos(a0 + u * sweep), centre[1] + r * math.sin(a0 + u * sweep), a[2] + u * (b[2] - a[2]))

            return math.dist(p, q) if abs(p[2] - q[2]) <= .1 else None

        deviation = None
        for k in near(index, points, a):
            d = 0.
            for j in range(k + 1, min(len(points), k + 4000)):
                if segment_distance(b, points[j - 1], points[j]) <= SNAP:
                    deviation = d
                    break
                p = points[j]
                h = helix_distance(p)
                if h is None or abs(math.hypot(p[0] - centre[0], p[1] - centre[1]) - r) > .1:
                    break
                d = max(d, h)
            if deviation is not None:
                break
        if deviation is None:
            continue
        matched += 1


        if curved:
            worst = max(worst, deviation)
            if deviation > ARC_TOLERANCE: over.append((n, deviation))
        else:
            worst_planar = max(worst_planar, deviation)


    return {'arcs': len(arcs), 'curved_arcs': sum(1 for x in arcs if x[5]), 'matched': matched,
            'max_deviation_mm': round(worst, 4), 'max_planar_deviation_mm': round(worst_planar, 4), 'over': over}


def check_package(path, printer):
    errors = []
    try:
        with zipfile.ZipFile(path) as z:
            required = ['Metadata/plate_1.gcode', 'Metadata/plate_1.gcode.md5', 'Metadata/slice_info.config',
                        'Metadata/plate_1.json', 'Metadata/plate_1.png', 'Metadata/top_1.png',
                        'Metadata/pick_1.png', 'Metadata/plate_no_light_1.png', '[Content_Types].xml', '_rels/.rels',
                        'Metadata/project_settings.config', 'Metadata/model_settings.config']
            for name in required:
                if name not in z.namelist(): errors.append('missing ' + name)
            g = z.read('Metadata/plate_1.gcode')
            if hashlib.md5(g).hexdigest().lower() != z.read('Metadata/plate_1.gcode.md5').decode().strip().lower(): errors.append('md5')
            plate = ET.fromstring(z.read('Metadata/slice_info.config')).find('plate')
            if plate is None: return errors + ['no plate']
            meta = {m.get('key'): m.get('value') for m in plate.findall('metadata')}
            if meta.get('printer_model_id') != MODEL_IDS.get(printer): errors.append('printer_model_id')
            for key in ('prediction', 'weight'):
                if float(meta.get(key, '0')) <= 0: errors.append(key)
            if meta.get('outside', 'false') != 'false': errors.append('outside')
            if meta.get('label_object_enabled') != 'true': errors.append('label_object_enabled')
            ids = {o.get('identify_id', '') for o in plate.findall('object')}
            if ids != set(re.findall(r'; start printing object, unique label id: (\d+)', g.decode())): errors.append('object ids')


            for label in re.findall(r'^M624\s+(\S+)', g.decode(), re.M):
                data = base64.b64decode(label, validate=True)
                mask = int.from_bytes(data, 'little')
                if len(data) != 8 or not 0 < mask < (1 << len(ids)): errors.append('object label mask')
            fs = plate.findall('filament')
            if not fs or any(not f.get('tray_info_idx', '').startswith('GF') for f in fs): errors.append('filament ids')
            pj = json.loads(z.read('Metadata/plate_1.json'))
            if not pj.get('bbox_objects') or any(o.get('layer_height', 0) <= 0 for o in pj['bbox_objects']): errors.append('first layer bbox')
            for key in ('first_layer_time', 'nozzle_diameter'):
                if float(pj.get(key, 0)) <= 0: errors.append(key)
            for name in required:
                if name.endswith('.png') and name in z.namelist() and not z.read(name).startswith(b'\x89PNG\r\n\x1a\n'): errors.append(name)
    except (OSError, ValueError, KeyError, zipfile.BadZipFile, ET.ParseError) as e:
        errors.append(str(e))
    return errors


def check(path, printer=None, package=None, reference=None):
    path = Path(path)
    if path.name.endswith('.3mf'):
        with zipfile.ZipFile(path) as z:
            lines = z.read('Metadata/plate_1.gcode').decode().splitlines()
        package = path
    else:
        lines = path.read_text().splitlines()
    if reference is not None:
        reference = Path(reference).read_text().splitlines()
    return check_lines(lines, printer, package, reference)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('gcode')
    ap.add_argument('--printer', choices=list(LIMITS) + list(BUFFERS))
    ap.add_argument('--reference', help='the same export with enable_arc_fitting=0: every arc is re-expanded against it')
    ap.add_argument('--package')
    ap.add_argument('--json')
    args = ap.parse_args()
    result = check(args.gcode, args.printer, args.package, args.reference)
    if result.get('skipped'):
        ap.error('cannot identify a focus printer; pass --printer')
    if args.json: Path(args.json).write_text(json.dumps(result, indent=2) + '\n')
    for name, value in result['rules'].items():
        print(f"{name}: {value['failures']} failures {value['lines']}")
    return 0 if result['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
