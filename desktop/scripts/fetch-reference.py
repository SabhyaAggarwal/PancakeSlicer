#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile

ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--repo',default=os.environ.get('GITHUB_REPOSITORY','ZimengXiong/PancakeSlicer'))
    args=parser.parse_args()
    source=json.loads((ROOT/'scripts/engine-source.json').read_text(encoding='utf-8'))
    args.output.mkdir(parents=True,exist_ok=True)
    archive=args.output/source['reference_asset']
    if not archive.is_file() or hashlib.sha256(archive.read_bytes()).hexdigest()!=source['reference_sha256']:
        subprocess.run(['gh','release','download',source['reference_release'],'--repo',args.repo,
                        '--pattern',source['reference_asset'],'--dir',str(args.output),'--clobber'],check=True)
    if hashlib.sha256(archive.read_bytes()).hexdigest()!=source['reference_sha256']:
        raise RuntimeError('Reference archive hash mismatch')
    with tarfile.open(archive,'r:gz') as packaged:
        packaged.extractall(args.output,filter='data')
    reference=json.loads((args.output/'qualification.json').read_text(encoding='utf-8'))
    if reference['engine_source_sha']!=source['engine_source_sha'] or reference['binary_sha256']!=source['reference_binary_sha256']:
        raise RuntimeError('Reference source or binary identity mismatch')
    if reference['platform']!={'os':'linux','arch':'x86_64'} or not reference['audits_passed']:
        raise RuntimeError('Reference is not the audited Linux artifact')
    print('Verified same-source Linux reference:',source['engine_source_sha'])


if __name__=='__main__':
    main()
