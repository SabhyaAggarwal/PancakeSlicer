#!/usr/bin/env python3
import argparse
import hashlib
from pathlib import Path
import re


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--root',required=True,type=Path)
    parser.add_argument('--web',required=True,type=Path)
    parser.add_argument('--prefix',required=True,type=Path)
    args=parser.parse_args()
    notices=[]
    seen=set()
    def add(name,text):
        text=text.strip()
        key=hashlib.sha256(text.encode()).digest()
        if text and key not in seen:
            seen.add(key)
            notices.append(name+'\n\n'+text+'\n')
    named=re.compile(r'licen[cs]e|copying|copyright|notice|ofl',re.I)
    legal=re.compile(r'copyright|SPDX|@license|\blicen[cs]e\b|permission is hereby|redistribution and use|all rights reserved|this file is part of|public domain',re.I)
    for directory in ['engine/src','engine/deps','server/vendor','engine/resources/fonts','desktop/src']:
        for path in sorted((args.root/directory).rglob('*')):
            if not path.is_file() or any(part in {'build','node_modules','__pycache__'} for part in path.relative_to(args.root).parts):continue
            name=path.relative_to(args.root).as_posix()
            if named.match(path.name):
                add(name,path.read_text(encoding='utf-8',errors='replace'))
            elif path.suffix.lower() in {'.c','.h','.cpp','.hpp','.cc','.ipp','.ts'}:
                text=path.read_text(encoding='utf-8',errors='replace')
                blocks=re.findall(r'/\*[\s\S]*?\*/|(?://[^\n]*\n?)+',text)
                for block in blocks:
                    if legal.search(block):add(name,block)
    for directory in ['share/licenses','share/doc']:
        for path in sorted((args.prefix/directory).rglob('*')):
            if path.is_file() and (directory=='share/licenses' or named.search(path.name) or path.name=='README.ijg'):
                add('Native dependency: '+path.relative_to(args.prefix).as_posix(),path.read_text(encoding='utf-8',errors='replace'))
    target=args.web/'third-party-licences.txt'
    original=target.read_text(encoding='utf-8')
    target.write_text(original+'\n\nNative engine, service, fonts and desktop notices\n\n'+'\n'+'\n'+('\n'+'-'*72+'\n\n').join(notices),encoding='utf-8')
    for suffix in ['.br','.gz']:
        target.with_name(target.name+suffix).unlink(missing_ok=True)
    print(f'Bundled {len(notices)} native, service and font notices ({target.stat().st_size} bytes including web notices)')


if __name__=='__main__':
    main()
