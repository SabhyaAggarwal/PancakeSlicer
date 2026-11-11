#!/usr/bin/env python3
"""Markdown report of how a new engine reference differs from a base one: non-comment G-code lines,
verifier and print-check output, and audit counts per case."""
import json, subprocess, sys, tempfile
from pathlib import Path

AUDIT = ['passed', 'total', 'hit_moves', 'clearance_hit_moves', 'contact_moves']


def gcode(path):
    lines = []
    for line in path.read_text(errors='replace').splitlines():
        code = line.split(';', 1)[0].rstrip()
        if code:
            lines.append(code)
    return lines


def text(path, root):
    return [line.replace(str(root), '<ref>') for line in path.read_text(errors='replace').splitlines()] if path.is_file() else None


def untimed(value):
    if isinstance(value, dict):
        return {k: untimed(v) for k, v in value.items() if not (k.startswith('time_') or k.endswith('_time_s'))}
    if isinstance(value, list):
        return [untimed(v) for v in value]
    return value


def checks(path):
    if not path.is_file():
        return None
    return json.dumps(untimed(json.loads(path.read_text())), indent=1, sort_keys=True).splitlines()


def wall(path):
    found = []
    def visit(value):
        if isinstance(value, dict):
            for k, v in value.items():
                if k == 'wall_time_s' and isinstance(v, (int, float)):
                    found.append(v)
                else:
                    visit(v)
        elif isinstance(value, list):
            for v in value:
                visit(v)
    if path.is_file():
        visit(json.loads(path.read_text()))
    return sum(found) if found else None


def changed(a, b):
    if a == b:
        return None
    with tempfile.NamedTemporaryFile('w') as fa, tempfile.NamedTemporaryFile('w') as fb:
        fa.write('\n'.join(a) + '\n'); fb.write('\n'.join(b) + '\n'); fa.flush(); fb.flush()
        out = subprocess.run(['diff', fa.name, fb.name], capture_output=True, text=True).stdout
    lines = out.splitlines()
    return f'−{sum(l[:1] == "<" for l in lines)} +{sum(l[:1] == ">" for l in lines)} lines'


def main(base, new):
    base, new = Path(base).resolve(), Path(new).resolve()
    qa = json.loads((base / 'qualification.json').read_text())['audits']
    qb = json.loads((new / 'qualification.json').read_text())['audits']
    cases = sorted({p.parent.name for p in [*base.glob('*/out.gcode'), *new.glob('*/out.gcode')]})
    rows, identical = [], True
    for case in cases:
        a, b = base / case, new / case
        if not (a / 'out.gcode').is_file() or not (b / 'out.gcode').is_file():
            rows.append(f'| {case} | only in {"new" if (b / "out.gcode").is_file() else "base"} | | | | |'); identical = False
            continue
        g = changed(gcode(a / 'out.gcode'), gcode(b / 'out.gcode'))
        cells = [g or 'identical']
        for name, read in [('verifier.txt', lambda p, r: text(p, r)), ('print_check.json', lambda p, r: checks(p))]:
            ta, tb = read(a / name, base), read(b / name, new)
            if ta is None and tb is None:
                cells.append('—')
            else:
                n = changed(ta or [], tb or [])
                cells.append(n or 'identical')
        audit = [f'{k} {qa.get(case, {}).get(k)} → {qb.get(case, {}).get(k)}' for k in AUDIT if qa.get(case, {}).get(k) != qb.get(case, {}).get(k)]
        cells.append(', '.join(audit) if audit else 'identical')
        identical &= all(c in ('identical', '—') for c in cells)
        wa, wb = wall(a / 'print_check.json'), wall(b / 'print_check.json')
        cells.append(f'{wa:.1f} → {wb:.1f} s' if wa is not None and wb is not None else '—')
        rows.append(f'| {case} | ' + ' | '.join(cells) + ' |')
    print('### Engine reference compared with the base\n')
    print('**Identical:** non-comment G-code, verifier output, print checks and audits match for every case.\n' if identical else
          '**Differs** from the base reference (reported, not a failure):\n')
    print('| case | G-code (non-comment) | verifier | print check | audit | wall time |\n|---|---|---|---|---|---|')
    print('\n'.join(rows))


if __name__ == '__main__':
    main(*sys.argv[1:3])
