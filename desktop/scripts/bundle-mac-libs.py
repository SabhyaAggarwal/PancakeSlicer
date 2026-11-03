#!/usr/bin/env python3
"""Copy non-system dylibs into the writable staging tree; rewrite only those copies."""
import pathlib
import shutil
import subprocess
import sys

source = pathlib.Path(sys.argv[1]).resolve()
engine = pathlib.Path(sys.argv[2]).resolve()
libdir = engine / 'lib'
libdir.mkdir(exist_ok=True)
copied = {}


def output(*args):
    return subprocess.check_output(args, text=True)


def expand(name, owner):
    return pathlib.Path(name.replace('@loader_path', str(owner.parent))
                        .replace('@executable_path', str(source.parent)))


def rpaths(owner):
    lines = output('otool', '-l', str(owner)).splitlines()
    return [expand(lines[i + 2].strip().split(' (offset', 1)[0][5:], owner)
            for i, line in enumerate(lines) if line.strip() == 'cmd LC_RPATH']


def bundle(owner, target, inherited=()):
    paths = rpaths(owner) + list(inherited)
    lines = output('otool', '-L', str(owner)).splitlines()[1:]
    for line in lines:
        name = line.strip().split(' (compatibility', 1)[0]
        if name.startswith(('/usr/lib/', '/System/Library/')):
            continue
        if owner != source and name == output('otool', '-D', str(owner)).splitlines()[-1]:
            continue
        if name.startswith('@rpath/'):
            candidates = [p / name[7:] for p in paths]
        else:
            candidates = [expand(name, owner)]
        dep = next((p.resolve() for p in candidates if p.is_file()), None)
        if dep is None:
            raise RuntimeError(f'Cannot resolve {name} referenced by {owner}')
        dest = libdir / dep.name
        if dest.name in copied and copied[dest.name] != dep:
            raise RuntimeError(f'Dylib name collision: {dep} and {copied[dest.name]}')
        if dest.name not in copied:
            copied[dest.name] = dep
            shutil.copy2(dep, dest)
            dest.chmod(dest.stat().st_mode | 0o200)
            subprocess.run(['install_name_tool', '-id', '@rpath/' + dest.name, str(dest)], check=True)
            bundle(dep, dest, paths)
        prefix = '@loader_path/lib/' if target.parent == engine else '@loader_path/'
        subprocess.run(['install_name_tool', '-change', name, prefix + dest.name, str(target)], check=True)


bundle(source, engine / 'nonplanar_slice_tool')
