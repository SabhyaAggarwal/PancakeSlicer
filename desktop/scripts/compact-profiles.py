#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('resources', type=Path)
    args = parser.parse_args()
    before = after = count = 0
    skipped = []
    for path in sorted((args.resources/'profiles').rglob('*.json')):
        data = path.read_bytes()
        try:
            value = json.loads(data)
            compact = (json.dumps(value, ensure_ascii=False, separators=(',', ':'), allow_nan=False)+'\n').encode('utf-8')
            if json.loads(compact) != value:
                raise ValueError('profile value changed')
        except (ValueError, UnicodeError) as error:
            skipped.append({'file':path.relative_to(args.resources).as_posix(),'reason':str(error)})
            continue
        before += len(data)
        after += min(len(compact),len(data))
        count += 1
        if len(compact) < len(data):
            path.write_bytes(compact)
    print(json.dumps({'files':count,'bytes_before':before,'bytes_after':after,'skipped':skipped},indent=2))


if __name__ == '__main__':
    main()
