#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--platform',required=True,choices=['mac','win'])
    parser.add_argument('--arch',required=True)
    args=parser.parse_args()
    dist=ROOT/'desktop/dist'
    suffixes=['.dmg','.zip'] if args.platform=='mac' else ['.exe','.zip']
    files={}
    for suffix in suffixes:
        candidates=list(dist.glob('*'+suffix))
        if len(candidates)!=1:
            raise RuntimeError('Expected exactly one '+suffix+' distribution')
        path=candidates[0]
        with path.open('rb') as stream:
            sha=hashlib.file_digest(stream,'sha256').hexdigest()
        files[path.name]={'bytes':path.stat().st_size,'sha256':sha}
    apps=list(dist.glob('mac*/PancakeSlicer.app')) if args.platform=='mac' else list(dist.glob('win-unpacked'))
    if len(apps)!=1:
        raise RuntimeError('Expected exactly one unpacked application')
    unpacked=apps[0]
    groups={}
    count=0
    for path in unpacked.rglob('*'):
        if not path.is_file() or path.is_symlink():
            continue
        relative=path.relative_to(unpacked).as_posix()
        if path.name=='source.tar.gz' or path.suffix.lower() in {'.map','.pdb','.lib'}:
            raise RuntimeError('Unexpected development/source file in app: '+relative)
        group='runtime'
        for name in ['engine/resources/fonts','engine/resources/profiles','engine','web']:
            if '/'+name+'/' in '/'+relative:
                group=name
                break
        groups[group]=groups.get(group,0)+path.stat().st_size
        count+=1
    result={'platform':args.platform,'arch':args.arch,'files':files,'unpacked':{'bytes':sum(groups.values()),'file_count':count,'groups':groups}}
    output=ROOT/f'desktop/build/artifact-sizes-{args.platform}-{args.arch}.json'
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    main()
