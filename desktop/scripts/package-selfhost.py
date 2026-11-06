#!/usr/bin/env python3
import argparse
import gzip
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--tool', type=Path, required=True)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--dependency-prefix', type=Path)
    args = parser.parse_args()
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    image = 'pancakeslicer:' + revision[:10]
    extra = ['--dependency-prefix', str(args.dependency_prefix.resolve())] if args.dependency_prefix else []
    subprocess.run([sys.executable, str(ROOT/'desktop/scripts/stage-ci.py'), '--os', 'linux', '--arch', 'x64',
                    '--tool', str(args.tool.resolve()), '--reference', str(args.reference.resolve()), '--image', image] + extra, check=True)
    output = args.output or ROOT/'desktop/dist'/f'PancakeSlicer-linux-x64-{revision[:10]}.docker.tar.gz'
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('wb') as target, gzip.GzipFile(fileobj=target, mode='wb', filename='', mtime=0, compresslevel=9) as archive:
        save = subprocess.Popen(['docker', 'save', image], stdout=subprocess.PIPE)
        while block := save.stdout.read(1024*1024):
            archive.write(block)
        if save.wait():
            raise RuntimeError('docker save failed')
    print(f'{output.name}: {output.stat().st_size} bytes; docker load < {output.name}')


if __name__ == '__main__':
    main()
