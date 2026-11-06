#!/usr/bin/env python3
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--installer',required=True,type=Path)
    parser.add_argument('--revision',required=True)
    parser.add_argument('--engine-source',required=True)
    args=parser.parse_args()
    if sys.platform!='win32':raise RuntimeError('This installer smoke requires Windows')
    report=Path(__file__).resolve().parents[2]/'desktop/build/installer-smoke'
    report.mkdir(parents=True,exist_ok=True)
    root=Path(tempfile.mkdtemp(prefix='pancake-install-'))
    destination=root/'application'
    subprocess.run([str(args.installer.resolve()),'/S','/D='+str(destination)],check=True,timeout=180)
    application=destination/'PancakeSlicer.exe'
    if not application.is_file():raise RuntimeError('Installer did not create the application')
    env={**os.environ,'ORCA_DESKTOP_USER_DATA':str(root/'data')}
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with (root/'application.log').open('wb') as log:
        health=None
        process=subprocess.Popen([str(application),'--service-only'],env=env,stdout=log,stderr=subprocess.STDOUT)
        try:
            deadline=time.monotonic()+60
            health=None
            while time.monotonic()<deadline:
                if process.poll() is not None:raise RuntimeError('Installed application exited: '+str(process.returncode))
                try:
                    with opener.open('http://127.0.0.1:29731/api/healthz',timeout=2) as response:
                        health=json.load(response)
                    artifact=health.get('artifact_manifest',{})
                    if health.get('server_revision')==args.revision and artifact.get('source_sha')==args.engine_source and artifact.get('print_qualified') is True:
                        break
                except (OSError,ValueError,urllib.error.URLError):pass
                time.sleep(.25)
            else:raise RuntimeError('Installed application did not prove qualified health: '+json.dumps(health))
            subprocess.run([sys.executable,str(Path(__file__).resolve().parent/'installed-slices.py'),'--engine',str(destination/'resources/engine'),
                            '--qualification',str(report.parent/'qualification-win-x64'),'--output',str(report/'win')],check=True)
            print(json.dumps({'installed':True,'server_revision':health['server_revision'],'artifact_manifest':health['artifact_manifest']},indent=2))
        finally:
            subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],check=False,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            process.wait(timeout=15)
            log.flush()
            for name,source in [('application.log',root/'application.log'),('server.log',root/'data/logs/server.log')]:
                if source.is_file():shutil.copy2(source,report/name)
            (report/'health.json').write_text(json.dumps(health,indent=2)+'\n',encoding='utf-8')
    shutil.rmtree(root)


if __name__=='__main__':
    main()
