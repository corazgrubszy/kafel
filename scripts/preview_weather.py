#!/usr/bin/env python3
"""Temporary weather screen, with automatic dashboard restoration. No installation."""
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import stat
import subprocess
import sys
from datetime import datetime,timezone


def command(*args):
    return subprocess.run(args,check=True,capture_output=True,text=True).stdout.strip()


def show(stage,user,digest,run=command):
    binary=stage/'weather-card-preview'
    if hashlib.sha256(binary.read_bytes()).hexdigest()!=digest:raise RuntimeError('Preview checksum mismatch')
    binary.chmod(0o755)
    regular=stage/'IBMPlexSans-Regular.ttf';bold=stage/'IBMPlexSans-Bold.ttf'
    run(str(binary),str(regular),str(bold),str(stage/'weather.json'),str(stage/'clouds.svg'),'--check')
    dashboard='kafel-dashboard.service'
    run('systemctl','is-active','--quiet',dashboard)
    def pid(unit):return run('systemctl','show',unit,'-p','MainPID','--value')
    producers=('kafel-weather.service','kafel-waste.service')
    for unit in producers:run('systemctl','is-active','--quiet',unit)
    before={unit:pid(unit) for unit in producers}
    if any(not value.isdigit() or int(value)<=0 for value in before.values()):raise RuntimeError('Missing producer PID')
    # The transient service survives a lost SSH client; systemd restores the
    # dashboard even on a crash or RuntimeMaxSec. Python finally covers launch errors.
    try:
        run('systemctl','stop',dashboard)
        run('systemd-run','--unit=kafel-weather-preview','--collect','--wait','--pipe',
            '--property=RuntimeMaxSec=135',
            '--property=ExecStopPost=/usr/bin/systemctl --no-block start kafel-dashboard.service',
            str(binary),str(regular),str(bold),str(stage/'weather.json'),str(stage/'clouds.svg'),'120')
    finally:
        run('systemctl','start',dashboard)
    run('systemctl','is-active','--quiet',dashboard)
    for unit in producers:run('systemctl','is-active','--quiet',unit)
    after={unit:pid(unit) for unit in producers}
    report={'dashboard_restored':True,'producers':{unit:{'before_pid':before[unit],'after_pid':after[unit],
            'same_pid':before[unit]==after[unit]} for unit in producers}}
    path=stage/'preview.json';path.write_text(json.dumps(report,indent=2)+'\n');path.chmod(0o600)
    import pwd
    os.chown(path,pwd.getpwnam(user).pw_uid,-1)
    print(json.dumps(report,indent=2),flush=True)


def remote(stage,user,digest):
    if os.geteuid()!=0:raise RuntimeError('Preview requires sudo')
    fd=os.open('/run/lock/kafel-deploy.lock',os.O_RDWR|os.O_CREAT|os.O_CLOEXEC|os.O_NOFOLLOW,0o600)
    with os.fdopen(fd,'rb') as lock:
        metadata=os.fstat(lock.fileno())
        if not stat.S_ISREG(metadata.st_mode) or metadata.st_uid!=0 or metadata.st_mode&0o077:
            raise RuntimeError('Invalid deployment lock')
        fcntl.flock(lock.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB)
        show(stage,user,digest)


def local():
    import deploy
    build='--build' in sys.argv[1:]
    args=deploy.settings([arg for arg in sys.argv[1:] if arg!='--build'])
    if build:subprocess.run(['zig','build','weather-card-test','weather-card-cm4','-Doptimize=ReleaseFast'],cwd=deploy.ROOT,check=True)
    binary=deploy.ROOT/'zig-out/linux-arm64/weather-card-preview'
    digest=deploy.arm64_digest(binary)
    assets=deploy.ROOT/'libs/ui/assets/ibm-plex-sans'
    target=args.user+'@'+args.host
    with deploy.connection(target,args.socket) as options:
        ssh,scp=['ssh',*options],['scp',*options]
        stage=command(*ssh,target,'mktemp -d /tmp/kafel-weather-preview.XXXXXXXX')
        if not re.fullmatch(r'/tmp/kafel-weather-preview\.[A-Za-z0-9]{8}',stage):raise RuntimeError('Unexpected preview stage')
        try:
            for path in (binary,assets/'IBMPlexSans-Regular.ttf',assets/'IBMPlexSans-Bold.ttf',deploy.ROOT/'libs/ui/examples/weather-card/weather.json',deploy.ROOT/'libs/ui/examples/weather-card/svg/clouds.svg',Path(__file__).resolve()):
                subprocess.run([*scp,str(path),target+':'+stage+'/'+path.name],check=True)
            print('Podgląd przez 120 sekund. Potem dashboard wróci automatycznie.',flush=True)
            script=shlex.join([stage+'/preview_weather.py','--remote',stage,args.user,digest])
            subprocess.run([*ssh,'-tt',target,'sudo python3 '+script],check=True)
            results=deploy.ROOT/'deploy-results'/('preview-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ'))
            results.mkdir(parents=True,mode=0o700)
            subprocess.run([*scp,target+':'+stage+'/preview.json',str(results/'preview.json')],check=True)
            (results/'preview.json').chmod(0o600)
            print('Saved preview report:',results/'preview.json')
        finally:
            subprocess.run([*ssh,target,'rm -rf -- '+shlex.quote(stage)],check=False)


if __name__=='__main__':
    try:
        if len(sys.argv)==5 and sys.argv[1]=='--remote':remote(Path(sys.argv[2]),sys.argv[3],sys.argv[4])
        else:local()
    except (OSError,RuntimeError,subprocess.SubprocessError) as error:raise SystemExit('Preview failed: '+str(error))
