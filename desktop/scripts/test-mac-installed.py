#!/usr/bin/env python3
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--installer',required=True,type=Path)
    parser.add_argument('--qualification',required=True,type=Path)
    args=parser.parse_args()
    if sys.platform!='darwin':
        raise RuntimeError('The DMG installation check requires macOS')
    with tempfile.TemporaryDirectory(prefix='pancake-install-') as temporary:
        root=Path(temporary)
        mount=root/'volume'
        mount.mkdir()
        subprocess.run(['hdiutil','attach','-readonly','-nobrowse','-mountpoint',str(mount),str(args.installer.resolve())],check=True,timeout=120)
        application=root/'PancakeSlicer.app'
        try:
            apps=list(mount.glob('*.app'))
            if len(apps)!=1:
                raise RuntimeError('DMG must contain one application')
            subprocess.run(['ditto',str(apps[0]),str(application)],check=True,timeout=120)
        finally:
            subprocess.run(['hdiutil','detach',str(mount)],check=True,timeout=60)
        subprocess.run([sys.executable,str(ROOT/'desktop/scripts/installed-slices.py'),'--engine',str(application/'Contents/Resources/engine'),
                        '--qualification',str(args.qualification.resolve()),'--output',str(ROOT/'desktop/build/installer-test/mac')],check=True)


if __name__=='__main__':
    main()
