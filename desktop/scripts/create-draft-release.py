#!/usr/bin/env python3
import argparse
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--tag',required=True)
    parser.add_argument('--main',required=True)
    parser.add_argument('--artifacts',required=True,type=Path)
    args=parser.parse_args()
    subprocess.run([sys.executable,str(ROOT/'desktop/scripts/release-inputs.py'),'--tag',args.tag,'--expected-main',args.main],check=True)
    files=sorted(path for path in args.artifacts.iterdir() if path.is_file() and path.suffix in {'.dmg','.exe','.zip','.gz','.json'})
    if len([p for p in files if p.suffix=='.dmg'])!=1 or len([p for p in files if p.suffix=='.exe'])!=1:
        raise RuntimeError('The combined run must provide one Mac DMG and one Windows installer')
    if not any(p.name=='source.tar.gz' for p in files):
        raise RuntimeError('Matching source archive is missing')
    source=next(p for p in files if p.name=='source.tar.gz')
    renamed=source.with_name('PancakeSlicer-source-'+args.tag[1:]+'.tar.gz')
    source.rename(renamed)
    files=[renamed if path==source else path for path in files]
    exists=subprocess.run(['gh','release','view',args.tag],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0
    if exists:
        raise RuntimeError('Release tag already exists; refusing to replace it')
    subprocess.run(['gh','release','create',args.tag,*map(str,files),'--target',args.main,'--draft','--prerelease','--latest=false',
                    '--title','PancakeSlicer '+args.tag[1:],'--notes-file',str(ROOT/'.github/release-notes'/(args.tag+'.md'))],check=True)


if __name__=='__main__':
    main()
