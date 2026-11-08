#!/usr/bin/env python3
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'engine/scripts/qualification'))
from qualify import compare_case,digest,gcode_lines,resource_digest


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--engine',required=True,type=Path)
    parser.add_argument('--qualification',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    engine=args.engine.resolve()
    reference=args.qualification.resolve()
    output=args.output.resolve()
    output.mkdir(parents=True,exist_ok=True)
    tool=engine/('nonplanar_slice_tool.exe' if sys.platform=='win32' else 'nonplanar_slice_tool')
    resources=engine/'resources'
    proof=json.loads((reference/'qualification.json').read_text(encoding='utf-8'))
    if not proof['ready'] or digest(tool)!=proof['binary_sha256'] or resource_digest(resources)[0]!=proof['resources_sha256']:
        raise RuntimeError('Installed engine/resources are not the qualified payload')
    cases=json.loads((ROOT/'engine/scripts/qualification/fixtures/cases.json').read_text(encoding='utf-8'))
    result={'binary_sha256':digest(tool),'resources_sha256':proof['resources_sha256'],'publication_main_sha':proof['publication_main_sha'],'cases':{}}
    env={**os.environ,'TZ':'UTC','PYTHONUTF8':'1','NONPLANAR_RESOURCES_DIR':str(resources),'NONPLANAR_HEADS_DIR':str(resources/'nonplanar/heads'),'OMP_NUM_THREADS':'1','OPENBLAS_NUM_THREADS':'1'}
    for name in ['benchy_flat','benchy']:
        out=output/name
        out.mkdir(exist_ok=True)
        (out/'data').mkdir(exist_ok=True)
        model=reference/name/'benchy.stl'
        values={'resources':str(resources),'out':str(out),'model':str(model)}
        command=[str(tool)]+[item.format(**values) for item in cases[name]['argv']]
        (out/'command.json').write_text(json.dumps(command,indent=2)+'\n',encoding='utf-8')
        with (out/'stdout.log').open('wb') as stdout,(out/'stderr.log').open('wb') as stderr:
            subprocess.run(command,check=True,stdout=stdout,stderr=stderr,env=env,timeout=300)
        comparison=compare_case(reference/name,out)
        before=(reference/name/'out.gcode').read_bytes()
        after=(out/'out.gcode').read_bytes()
        body_identical=gcode_lines(before)==gcode_lines(after)
        result['cases'][name]={'gcode_body_byte_identical':body_identical,'whole_gcode_byte_identical':before==after,
                               'ignored':'only the declared generated-by header and LF/CRLF line endings','comparison':comparison}
        (output/'installed-slices.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        if not body_identical or comparison['package']['identity']=='diff':
            raise RuntimeError(name+': installed output differs from this OS pre-package output')
    result['ok']=True
    (output/'installed-slices.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'installed_slice_identity':True,'cases':{name:value['gcode_body_byte_identical'] for name,value in result['cases'].items()}},indent=2))


if __name__=='__main__':
    main()
