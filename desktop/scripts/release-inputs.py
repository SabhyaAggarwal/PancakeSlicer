#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--tag',required=True)
    parser.add_argument('--expected-main',required=True)
    args=parser.parse_args()
    if not re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?',args.tag):
        raise RuntimeError('Release tag must name a semantic version')
    if not re.fullmatch(r'[0-9a-f]{40}',args.expected_main):
        raise RuntimeError('Expected publication must be a full commit SHA')
    actual=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    if actual!=args.expected_main:
        raise RuntimeError('Checkout differs from the approved publication')
    version=args.tag[1:]
    for name in ['web','desktop']:
        for filename in ['package.json','package-lock.json']:
            package=json.loads((ROOT/name/filename).read_text(encoding='utf-8'))
            if package['version']!=version or filename.endswith('-lock.json') and package['packages']['']['version']!=version:
                raise RuntimeError(name+'/'+filename+' does not match the release tag')
    print(json.dumps({'tag':args.tag,'version':version,'publication_main_sha':actual,'draft':True},indent=2))


if __name__=='__main__':
    main()
