#!/usr/bin/env python3
"""Read-only audit of Kafel installation and persistent schedule backups."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import pwd
import re
import shlex
import subprocess
import sys
import tempfile
from datetime import datetime, timezone

EXPECTED=('kafel-dashboard.service','kafel-weather.service','kafel-waste.service')
LEGACY=('dashboard.service','infohub.service')
PATTERNS=('dashboard-deploy.','infohub-mongoose.','kafel-http.','infohub-http-client.','kafel-deploy.','kafel-audit.')
PROBES={'mongoose-probe','mongoose-probe-mbedtls','http-client-probe','http-client-probe-mbedtls'}

def legacy_unit_link(path):
    return path.is_symlink() and path.name in LEGACY

def command(*args):
    result=subprocess.run(args,text=True,capture_output=True,timeout=20)
    return result.returncode,result.stdout.strip(),result.stderr.strip()

def binary_identity(path):
    code,out,error=command(str(path),'--version')
    if code:raise RuntimeError('Cannot read binary version: '+(error or str(path)))
    match=re.search(r'(?:^| )git=([0-9a-f]{40}(?:-dirty)?|unknown)(?:$| )',out)
    return {'version':out,'git_revision':match.group(1) if match else None}


def remote(user,stage):
    if os.geteuid()!=0: raise RuntimeError('Audit requires sudo to avoid silently skipping private directories')
    uid=pwd.getpwnam(user).pw_uid
    home=Path(pwd.getpwnam(user).pw_dir)
    protected=Path('/var/lib/kafel-waste')
    protected_directories={protected}
    report={'services':{},'legacy_units':[],'artifacts':[],'scan_errors':[],
            'preserved_schedule':{'path':str(protected),'exists':protected.is_dir()},'binaries':{},'scan_roots':[]}
    state=Path('/run/kafel-display/status.json')
    report['display']={'isolation':json.loads(state.read_text()) if state.is_file() else None,
        'boot_display_parameters':[token for token in Path('/proc/cmdline').read_text().split() if token.startswith(('console=','fbcon=','video='))],
        'framebuffers':{p.name:(p/'name').read_text().strip() for p in Path('/sys/class/graphics').glob('fb[0-9]*')},
        'console_bindings':{p.parent.name:p.read_text().strip() for p in Path('/sys/class/vtconsole').glob('vtcon*/bind')}}
    schedule=Path('/var/lib/kafel-waste/garbage_schedule.json')
    environment=Path('/etc/kafel/waste.env')
    if environment.is_file():
        for line in environment.read_text().splitlines():
            key,sep,value=line.partition('=')
            if sep and key=='KAFEL_WASTE_SCHEDULE':
                parts=shlex.split(value)
                if len(parts)==1:schedule=Path(parts[0])
    def schedule_metadata(path):
        result={'path':str(path),'exists':path.is_file() and not path.is_symlink()}
        if result['exists']:
            metadata=path.stat()
            result.update(sha256=hashlib.sha256(path.read_bytes()).hexdigest(),bytes=metadata.st_size,
                          uid=metadata.st_uid,mode=oct(metadata.st_mode&0o777))
        return result
    current=schedule_metadata(schedule)
    backup=schedule_metadata(protected/'garbage_schedule.infohub-backup.json')
    report['schedule_data']={'active':current,'backup':backup,'legacy_directory_exists':(home/'infohub').exists(),
        'same_sha256':current.get('sha256')==backup.get('sha256') if current['exists'] and backup['exists'] else None}
    for unit in EXPECTED:
        code,out,error=command('systemctl','show',unit,'-p','ActiveState','-p','SubState','-p','MainPID','-p','NRestarts','-p','UnitFileState','-p','ExecStart')
        report['services'][unit]=dict(line.split('=',1) for line in out.splitlines() if '=' in line)
        if code: report['scan_errors'].append({'operation':unit,'error':error})
    for item in (current,backup):
        code,out,error=command('runuser','-u',user,'--','/opt/kafel/current/bin/kafel-waste','--check',item['path'])
        item['valid_as_service_user']=code==0
        if code:report['scan_errors'].append({'operation':'validate '+item['path'],'error':error or out})
    report['journal']={}
    for unit in EXPECTED:
        code,out,error=command('journalctl','-b','-u',unit,'-p','warning','-n','100','--no-pager','-o','short-iso','--quiet')
        report['journal'][unit]={'scope':'current boot, last 100 warning-or-higher entries','entries':out.splitlines()}
        if code:report['scan_errors'].append({'operation':'journal '+unit,'error':error})
        code,out,error=command('journalctl','-b','-u',unit,'-n','40','--no-pager','-o','short-iso','--quiet')
        report['journal'][unit]['recent_entries']=out.splitlines()
        if code:report['scan_errors'].append({'operation':'recent journal '+unit,'error':error})
    code,out,error=command('systemctl','list-unit-files','dashboard*','infohub*','kafel-*','--no-legend','--no-pager')
    report['unit_files']=out.splitlines()
    if code: report['scan_errors'].append({'operation':'list-unit-files','error':error})
    for line in out.splitlines():
        if line.split()[0].startswith(('dashboard','infohub')): report['legacy_units'].append(line)
    for unit in LEGACY:
        code,out,error=command('systemctl','is-active',unit)
        if out in ('active','activating','deactivating'):report['legacy_units'].append(unit+': '+out)
    for name in ('kafel-dashboard','kafel-weather','kafel-waste'):
        path=Path('/opt/kafel/current/bin')/name
        if path.is_file():
            report['binaries'][name]={'path':str(path.resolve()),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                'uid':path.stat().st_uid,'mode':oct(path.stat().st_mode&0o777)}
            try:
                report['binaries'][name].update(binary_identity(path))
                pid=report['services'].get(name+'.service',{}).get('MainPID','0')
                if pid.isdigit() and int(pid)>0:
                    report['binaries'][name]['running']=binary_identity(Path('/proc')/pid/'exe')
            except RuntimeError as error:
                report['scan_errors'].append({'operation':'version '+name,'error':str(error)})
        else:report['scan_errors'].append({'operation':str(path),'error':'missing binary'})
    release_root=Path('/opt/kafel/releases')
    report['releases']=sorted(path.name for path in release_root.iterdir()) if release_root.is_dir() else []
    report['scope']={'maximum_directory_depth':6,'excluded_names':['.git','.cache','node_modules','.zig-cache'],'excluded_schedule':[str(path) for path in sorted(protected_directories)]}
    def record(path,reason): report['artifacts'].append({'path':str(path),'reason':reason})
    def visit(root,depth=0):
        if not root.exists() or root.is_symlink() or root in protected_directories:return
        try: entries=list(root.iterdir())
        except OSError as error:
            report['scan_errors'].append({'operation':str(root),'error':str(error)});return
        for path in entries:
            if path in protected_directories or path==stage:continue
            if path.name in ('.git','.cache','node_modules','.zig-cache'):continue
            if path.is_symlink():
                if legacy_unit_link(path):record(path,'legacy service symlink')
                continue
            if path.name in LEGACY or path.name in (unit+'.d' for unit in LEGACY):record(path,'legacy service file/drop-in')
            if path.name in PROBES:record(path,'TLS/HTTP probe')
            if any(path.name.startswith(prefix) for prefix in PATTERNS):record(path,'former deployment/test staging')
            if re.fullmatch(r'\.(?:dashboard|infohub)\.(?:previous|new)\..+',path.name):record(path,'former executable backup')
            if path.is_file() and path.name in ('dashboard','infohub','dashboard.new','infohub.new','dashboard.old','infohub.old'):
                try:
                    with path.open('rb') as stream: header=stream.read(4)
                    if header==b'\x7fELF':record(path,'legacy executable')
                except OSError as error:report['scan_errors'].append({'operation':str(path),'error':str(error)})
            if path.is_dir() and depth<5:visit(path,depth+1)
    for root in (home,Path('/tmp'),Path('/var/tmp'),Path('/usr/local/bin'),Path('/etc/systemd/system'),Path('/etc/init.d'),Path('/opt/kafel')):
        report['scan_roots'].append(str(root));visit(root)
    for directory in (Path('/etc/cron.d'),Path('/etc/cron.daily'),Path('/var/spool/cron/crontabs')):
        if not directory.is_dir():continue
        for path in directory.iterdir():
            if path.is_file() and not path.is_symlink():
                try:
                    if re.search(r'(?m)^(?!\s*#).*\b(?:dashboard|infohub)(?:\.service|/|\s|$)',path.read_text(errors='replace')):
                        record(path,'possible legacy scheduled startup (contents omitted)')
                except OSError as error:report['scan_errors'].append({'operation':str(path),'error':str(error)})
    if (home/'infohub').exists():record(home/'infohub','remaining InfoHub directory')
    for path in (Path('/etc/default/infohub'),Path('/etc/infohub.env'),Path('/etc/infohub')):
        if path.exists():record(path,'legacy configuration')
    if len(report['releases'])!=1:record(release_root,'unexpected number of retained releases')
    temporary_link=Path('/opt/kafel/.current.new')
    if temporary_link.exists() or temporary_link.is_symlink():record(temporary_link,'unfinished release switch')
    report['artifacts']=list({entry['path']:entry for entry in report['artifacts']}.values())
    report['scope_clean']=not(report['artifacts'] or report['legacy_units'] or report['scan_errors'])
    destination=stage/'audit.json';destination.write_text(json.dumps(report,indent=2)+'\n')
    destination.chmod(0o600);os.chown(destination,uid,-1)
    print('Audit complete. Schedule directory excluded; no files deleted.',flush=True)

def local():
    import deploy
    args=deploy.settings([*sys.argv[1:],'unused-dashboard','unused-weather','unused-waste'])
    target=args.user+'@'+args.host
    with deploy.connection(target,args.socket) as options:
        ssh,scp=['ssh',*options],['scp',*options]
        stage=subprocess.check_output([*ssh,target,'mktemp -d /tmp/kafel-audit.XXXXXXXX'],text=True).strip()
        if not re.fullmatch(r'/tmp/kafel-audit\.[A-Za-z0-9]{8}',stage):raise RuntimeError('Unexpected stage')
        try:
            subprocess.run([*scp,str(Path(__file__).resolve()),target+':'+stage+'/audit.py'],check=True)
            import shlex
            subprocess.run([*ssh,'-tt',target,'sudo python3 '+shlex.join([stage+'/audit.py','--remote',args.user,stage])],check=True)
            directory=deploy.ROOT/'deploy-results'/('audit-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ'))
            directory.mkdir(parents=True,mode=0o700);directory.chmod(0o700)
            output=directory/'audit.json'
            subprocess.run([*scp,target+':'+stage+'/audit.json',str(output)],check=True)
            output.chmod(0o600)
            report=json.loads(output.read_text())
            for name,item in report['binaries'].items():
                binary=deploy.ROOT/'zig-out/linux-arm64'/name
                item['matches_local_build']=binary.exists() and hashlib.sha256(binary.read_bytes()).hexdigest()==item['sha256']
            output.write_text(json.dumps(report,indent=2)+'\n')
            print('Saved audit:',output)
            print(json.dumps(report,indent=2))
        finally:subprocess.run([*ssh,target,'rm -rf -- '+__import__('shlex').quote(stage)],check=False)

if __name__=='__main__':
    try:
        if len(sys.argv)==4 and sys.argv[1]=='--remote':remote(sys.argv[2],Path(sys.argv[3]))
        else:local()
    except (OSError,RuntimeError,subprocess.SubprocessError) as error:raise SystemExit('Audit failed: '+str(error))
