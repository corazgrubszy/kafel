#!/usr/bin/env python3
"""Transactional CM4 installer. Invoked by deploy.py with sudo; no shell parsing."""
import argparse
import fcntl
import importlib.util
import sys
sys.dont_write_bytecode = True
import stat
import grp
import hashlib
import json
import math
import os
from pathlib import Path
import pwd
import re
import shutil
import shlex
import signal
import subprocess
import tarfile
import tempfile
import time

NEW = ('kafel-dashboard.service', 'kafel-weather.service', 'kafel-waste.service')
OLD = ('dashboard.service', 'infohub.service')
NAMES = ('kafel-dashboard', 'kafel-weather', 'kafel-waste')
TEMP_PREFIXES = ('dashboard-deploy.', 'infohub-mongoose.', 'kafel-http.', 'infohub-http-client.')

def run(*args, check=True, **kwargs):
    result=subprocess.run(list(args), check=False, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, **kwargs)
    if check and result.returncode:
        raise RuntimeError('Command failed: '+shlex.join(args)+'\n'+result.stderr.strip())
    return result

def coordinate(value, limit):
    if not isinstance(value, str) or not re.fullmatch(r'-?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?', value):
        raise ValueError('Missing or invalid weather coordinates')
    number = float(value)
    if not math.isfinite(number) or abs(number) > limit:
        raise ValueError('Weather coordinate out of range')
    return format(number, '.12g')

def file_environment(path):
    """Read only the two numeric coordinate assignments, never evaluate a file."""
    values = {}
    for line in path.read_text().splitlines():
        match = re.fullmatch(r'\s*(?:export\s+)?((?:INFOHUB|KAFEL_WEATHER)_(?:LATITUDE|LONGITUDE))\s*=\s*[\"\']?([-+0-9.eE]+)[\"\']?\s*', line)
        if match: values[match[1]] = match[2]
    return values

def atomic_write(path, data, mode=0o644, uid=None, gid=None):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.'+path.name+'.', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data);stream.flush();os.fsync(stream.fileno())
        os.chmod(temporary, mode)
        if uid is not None: os.chown(temporary,uid,gid)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary): os.unlink(temporary)

class Installer:
    def __init__(self, stage, user, root=Path('/'), command=run):
        self.stage, self.user, self.root, self.command = Path(stage), user, root, command
        self.base = root/'opt/kafel'
        self.current = self.base/'current'
        self.units = root/'etc/systemd/system'
        self.envfile = root/'etc/kafel/weather.env'
        self.waste_envfile = root/'etc/kafel/waste.env'
        self.dashboard_envfile = root/'etc/kafel/dashboard.env'
        self.helperfile = self.base/'libexec/display_console.py'
        self.dashboard_environment = b''
        self.dashboard_settings = None
        self.display_files = {}
        self.display_modes = {}
        self.display_report = {}
        self.waste_environment = b''
        self.schedule_path = None
        self.saved = {}
        self.states = {}
        self.legacy_binaries = []
        self.legacy_units = []
        self.legacy_environments = []
        self.removed = []
        self.health_report = {}

    def systemctl(self, *args, check=True):
        return self.command('systemctl', *args, check=check)

    def property(self, unit, key):
        return self.systemctl('show', unit, '-p', key, '--value', check=False).stdout.strip()

    def snapshot_file(self, path):
        if path in self.saved: return
        if path.is_symlink(): raise RuntimeError('Refusing unexpected symlink: '+str(path))
        if path.exists():
            stat=path.stat();self.saved[path]=(path.read_bytes(),stat.st_mode&0o777,stat.st_uid,stat.st_gid)
        else:self.saved[path]=None

    def dashboard_values(self, config):
        values={'KAFEL_FRAMEBUFFER':'auto','KAFEL_TOUCH':'auto','KAFEL_IDLE_TIMEOUT':'60'}
        if self.dashboard_envfile.exists():
            for line in self.dashboard_envfile.read_text().splitlines():
                key,sep,value=line.partition('=')
                if sep:
                    parts=shlex.split(value)
                    if len(parts)==1: values[key]=parts[0]
        for key,name in (('framebuffer','KAFEL_FRAMEBUFFER'),('touch','KAFEL_TOUCH'),
                         ('dashboard_socket','KAFEL_DASHBOARD_SOCKET'),('idle_timeout','KAFEL_IDLE_TIMEOUT')):
            if key in config: values[name]=config[key]
        return values

    def shared_socket(self, config):
        value=self.dashboard_values(config).get('KAFEL_DASHBOARD_SOCKET','')
        if value and (not value.startswith('/') or len(value.encode())>=108 or Path(value).name in ('lock','.','..') or
                      value.endswith('/') or any(c in value for c in '\n\r\t\x00')):
            raise ValueError('Invalid dashboard socket path')
        return value

    def dashboard_command(self, argument):
        values=self.dashboard_settings or self.dashboard_values({})
        return self.command('runuser','-u',self.user,'--','env',
                            *(key+'='+value for key,value in values.items()),str(self.stage/'kafel-dashboard'),argument)

    def dashboard_configuration(self, config):
        values=self.dashboard_values(config)
        known={'KAFEL_FRAMEBUFFER','KAFEL_TOUCH','KAFEL_DASHBOARD_SOCKET','KAFEL_IDLE_TIMEOUT'}
        if set(values)-known:raise ValueError('Unknown setting in dashboard.env')
        for value in values.values():
            if not value or any(c in value for c in '\n\r\t\x00'):raise ValueError('Invalid dashboard setting')
        for key in ('KAFEL_FRAMEBUFFER','KAFEL_TOUCH'):
            value=values[key]
            if value not in ('auto','off') and not value.startswith('/'):raise ValueError('Invalid device path')
            if key=='KAFEL_FRAMEBUFFER' and value=='off':raise ValueError('Framebuffer cannot be disabled')
        if not re.fullmatch(r'[0-9]+',values['KAFEL_IDLE_TIMEOUT']) or int(values['KAFEL_IDLE_TIMEOUT'])>86400:
            raise ValueError('Invalid idle timeout')
        new_socket=self.shared_socket(config)
        old_socket=self.shared_socket({})
        if new_socket!=old_socket:
            for name in NAMES[1:]:
                if name not in config.get('selected',NAMES) and self.property(name+'.service','LoadState') not in ('','not-found'):
                    raise RuntimeError('Socket change requires deploying all installed producers together')
        def quote(value):return '"'+value.replace('\\','\\\\').replace('"','\\"')+'"'
        self.dashboard_settings=values
        self.dashboard_environment=''.join(key+'='+quote(value)+'\n' for key,value in sorted(values.items())).encode()
        code=(self.stage/'display_console.py').read_bytes()
        if hashlib.sha256(code).hexdigest()!=config.get('display_helper_sha256'):
            raise RuntimeError('Display helper checksum mismatch')
        self.display_files={self.dashboard_envfile:self.dashboard_environment,self.helperfile:code}
        self.display_modes={self.dashboard_envfile:0o600,self.helperfile:0o644}
        spec=importlib.util.spec_from_file_location('kafel_display',self.stage/'display_console.py')
        helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
        boot=self.root/'boot/firmware/cmdline.txt'
        connector=next((self.root/'sys/class/drm').glob('card*-HDMI-A-1'),None)
        if boot.exists() and connector is not None:
            if boot.is_symlink():raise RuntimeError('Unexpected cmdline.txt symlink')
            proposed=helper.boot_configuration(boot.read_text()).encode()
            self.display_files[boot]=proposed
            self.display_modes[boot]=boot.stat().st_mode&0o777
            runtime=self.root/'proc/cmdline'
            pending=boot.read_bytes()!=proposed or (runtime.exists() and helper.HDMI_MODE not in runtime.read_text().split())
            self.display_report={'boot_configured':True,'reboot_required':pending,
                                 'hdmi_mode':helper.HDMI_MODE}
        else:
            self.display_report={'boot_configured':False,'reboot_required':False,
                                 'fallback':'fbcon detached if no verified forced HDMI framebuffer'}
        self.dashboard_command('--check-input')
        self.dashboard_command('--display-info')

    def environment(self, config):
        values = {}
        # Read the running InfoHub's exact environment before stopping it.
        pid = self.property('infohub.service', 'MainPID')
        if pid.isdigit() and int(pid) > 0:
            proc = self.root/f'proc/{pid}/environ'
            if proc.exists():
                for item in proc.read_bytes().split(b'\0'):
                    key, sep, value = item.partition(b'=')
                    if sep: values[key.decode(errors='replace')] = value.decode(errors='replace')
        for item in shlex.split(self.property('infohub.service','Environment')):
            key,sep,value=item.partition('=')
            if sep and key in ('INFOHUB_LATITUDE','INFOHUB_LONGITUDE'): values.setdefault(key,value)
        # Dedicated legacy environment files are also retained in the local rollback archive.
        for name in ('etc/default/infohub', 'etc/infohub.env', 'etc/infohub/environment'):
            path = self.root/name
            if path.is_file() and not path.is_symlink():
                for key,value in file_environment(path).items(): values.setdefault(key,value)
                self.legacy_environments.append(path)
        declared=self.property('infohub.service','EnvironmentFiles')
        home=Path(pwd.getpwnam(self.user).pw_dir)
        for match in re.finditer(r'(\S+) \(ignore_errors=(?:yes|no)\)',declared):
            path=Path(match[1])
            if not path.is_absolute(): continue
            dedicated=(path.parent==home/'infohub' or path.parent==Path('/etc/infohub') or
                       path in (Path('/etc/infohub.env'),Path('/etc/default/infohub')))
            if not dedicated: continue
            actual=self.root/path.relative_to('/')
            if actual.is_file() and not actual.is_symlink():
                for key,value in file_environment(actual).items(): values.setdefault(key,value)
                if actual not in self.legacy_environments: self.legacy_environments.append(actual)
        if self.envfile.exists(): values.update(file_environment(self.envfile))
        latitude = config.get('latitude') or values.get('KAFEL_WEATHER_LATITUDE') or values.get('INFOHUB_LATITUDE')
        longitude = config.get('longitude') or values.get('KAFEL_WEATHER_LONGITUDE') or values.get('INFOHUB_LONGITUDE')
        socket=self.shared_socket(config)
        return ('KAFEL_WEATHER_LATITUDE='+coordinate(latitude,90)+'\n'+
                'KAFEL_WEATHER_LONGITUDE='+coordinate(longitude,180)+'\n'+
                ('KAFEL_DASHBOARD_SOCKET="'+socket.replace('\\','\\\\').replace('"','\\"')+'"\n' if socket else '')).encode()

    def waste_configuration(self, config):
        values={}
        if self.waste_envfile.exists():
            for line in self.waste_envfile.read_text().splitlines():
                key,sep,value=line.partition('=')
                if sep:
                    parts=shlex.split(value)
                    if len(parts)==1: values[key]=parts[0]
        home=Path(pwd.getpwnam(self.user).pw_dir)
        path=config.get('waste_schedule') or values.get('KAFEL_WASTE_SCHEDULE') or str(home/'infohub/data/garbage_schedule.json')
        span=config.get('waste_span') or values.get('KAFEL_WASTE_SPAN') or '1'
        if span not in ('1','2') or not Path(path).is_absolute() or any(c in path for c in '\n\r\x00'):
            raise RuntimeError('Invalid waste schedule path or span')
        actual=self.root/Path(path).relative_to('/')
        # Validate as the actual daemon account, not merely as root. No writes.
        self.command('runuser','-u',self.user,'--',str(self.stage/'kafel-waste'),'--check',str(actual))
        self.schedule_path=actual
        escaped=path.replace('\\','\\\\').replace('"','\\"')
        self.waste_environment=('KAFEL_WASTE_SCHEDULE="'+escaped+'"\nKAFEL_WASTE_SPAN='+span+'\n'+
            ('KAFEL_DASHBOARD_SOCKET="'+self.shared_socket(config).replace('\\','\\\\').replace('"','\\"')+'"\n' if self.shared_socket(config) else '')).encode()

    def preflight(self):
        manifest = json.loads((self.stage/'manifest.json').read_text())
        if set(manifest['binaries']) != set(NAMES): raise RuntimeError('Invalid manifest')
        for name in NAMES:
            binary = self.stage/name
            if hashlib.sha256(binary.read_bytes()).hexdigest() != manifest['binaries'][name]:
                raise RuntimeError('Checksum mismatch: '+name)
            os.chmod(binary,0o755)
            version = self.command(str(binary),'--version').stdout.strip()
            if not version.startswith(name+' '): raise RuntimeError('Wrong executable: '+name)
        self.dashboard_configuration(manifest)
        environment = self.environment(manifest)
        self.waste_configuration(manifest)
        for unit in NEW+OLD:
            loaded = self.property(unit,'LoadState') not in ('', 'not-found')
            self.states[unit] = {'loaded': loaded,
                'active': self.systemctl('is-active','--quiet',unit,check=False).returncode == 0,
                'enabled': self.systemctl('is-enabled','--quiet',unit,check=False).returncode == 0}
            drop_ins=shlex.split(self.property(unit,'DropInPaths')) if loaded else []
            if unit in NEW and drop_ins:
                raise RuntimeError('Inspect existing Kafel drop-ins before replacing unit: '+unit)
            for fragment in drop_ins:
                expected=Path('/etc/systemd/system')/(unit+'.d')
                path=Path(fragment)
                if unit not in OLD or path.parent!=expected or path.suffix!='.conf':
                    raise RuntimeError('Unrecognized legacy drop-in: '+fragment)
                actual=self.root/path.relative_to('/')
                self.snapshot_file(actual); self.legacy_units.append(actual)
            if loaded and unit in NEW:
                expected = '/opt/kafel/current/bin/'+unit.removesuffix('.service')
                if 'path='+expected+' ' not in self.property(unit,'ExecStart'):
                    raise RuntimeError('Unrecognized existing unit: '+unit)
            if loaded and unit in OLD:
                fragment = self.property(unit,'FragmentPath')
                if fragment != '/etc/systemd/system/'+unit:
                    raise RuntimeError('Legacy unit is not a local managed file: '+unit)
                old_user = self.property(unit,'User')
                if old_user != self.user: raise RuntimeError('Legacy service account differs: '+unit)
                executable = re.search(r'path=([^ ;]+)',self.property(unit,'ExecStart'))
                if not executable: raise RuntimeError('Cannot identify legacy binary: '+unit)
                path = Path(executable[1])
                home = Path(pwd.getpwnam(self.user).pw_dir)
                if path.name != unit.removesuffix('.service') or path.parent not in (home/path.name, home/'dashboard'):
                    raise RuntimeError('Unexpected legacy installation path: '+str(path))
                actual = self.root/path.relative_to('/')
                self.snapshot_file(actual)
                for suffix in ('.new', '.old'):
                    candidate=actual.with_name(actual.name+suffix)
                    if candidate.is_file() and not candidate.is_symlink():
                        self.snapshot_file(candidate); self.legacy_binaries.append(candidate)
                for candidate in actual.parent.iterdir():
                    if (re.fullmatch(r'\.'+re.escape(path.name)+r'\.(?:previous|new)\.[A-Za-z0-9]{8}',candidate.name)
                            and candidate.is_file() and not candidate.is_symlink()):
                        self.snapshot_file(candidate); self.legacy_binaries.append(candidate)
                self.legacy_binaries.append(actual)
                legacy_unit=self.root/fragment.lstrip('/')
                self.snapshot_file(legacy_unit); self.legacy_units.append(legacy_unit)
        for path in [*(self.units/unit for unit in NEW), self.envfile, self.waste_envfile, *self.legacy_environments, *self.display_files]:
            self.snapshot_file(path)
        return manifest, environment

    def switch(self, target):
        temporary=self.base/'.current.new'
        if temporary.exists() or temporary.is_symlink(): temporary.unlink()
        temporary.symlink_to(target)
        os.replace(temporary,self.current)

    def health(self, units=NEW, publishers=NAMES[1:]):
        pids={unit:self.property(unit,'MainPID') for unit in units}
        if any(not pid.isdigit() or int(pid)<=0 for pid in pids.values()):
            raise RuntimeError('Missing service PID')
        time.sleep(3)
        for unit,pid in pids.items():
            self.systemctl('is-active','--quiet',unit)
            if self.property(unit,'MainPID') != pid: raise RuntimeError('Service restarted: '+unit)
            log=self.command('journalctl','-b','-u',unit,'_PID='+pid,'--no-pager','-o','cat').stdout
            marker='READY '+unit.removesuffix('.service')+':'
            if marker not in log: raise RuntimeError('Readiness marker missing: '+unit)
            if unit==NEW[0]:
                deadline = time.monotonic()+12
                while any('services: registered '+name not in log for name in publishers):
                    if time.monotonic() >= deadline:
                        raise RuntimeError('Publisher heartbeat was not accepted')
                    time.sleep(0.25)
                    if self.property(unit,'MainPID') != pid: raise RuntimeError('Dashboard restarted')
                    log=self.command('journalctl','-b','-u',unit,'_PID='+pid,'--no-pager','-o','cat').stdout
            self.health_report[unit]={'pid':int(pid),'active':True,
                'enabled':self.systemctl('is-enabled','--quiet',unit,check=False).returncode==0,
                'readiness':True}
            if unit==NEW[1]:
                self.health_report[unit]['source']='updated' if 'weather cache updated' in log else 'error' if 'weather refresh failed:' in log else 'pending'
            elif unit==NEW[2]:
                self.health_report[unit]['source']='loaded' if 'waste schedule loaded:' in log else 'error' if 'waste reload failed:' in log else 'pending'

    def rollback(self, previous, units=NEW):
        errors=[]
        def attempt(operation):
            try: operation()
            except Exception as error: errors.append(str(error))
        for unit in units: attempt(lambda unit=unit:self.systemctl('stop',unit,check=False))
        for path,saved in self.saved.items():
            if saved is None:
                attempt(lambda path=path:path.unlink(missing_ok=True))
            else:
                attempt(lambda path=path,saved=saved:atomic_write(path,*saved))
        if previous is not None: attempt(lambda:self.switch(previous))
        else: attempt(lambda:self.current.unlink(missing_ok=True))
        attempt(lambda:self.systemctl('daemon-reload'))
        for unit,state in self.states.items():
            if units != NEW and unit not in units: continue
            attempt(lambda unit=unit,state=state:self.systemctl('enable' if state['enabled'] else 'disable',unit,check=False))
            if state['active']: attempt(lambda unit=unit:self.systemctl('start',unit))
        if errors: raise RuntimeError('ROLLBACK ERRORS: '+'; '.join(errors))
        print('Previous services and files restored.',flush=True)

    def archive(self, previous):
        # Downloaded by deploy.py, then removed from CM4 together with staging.
        backup=self.stage/'rollback.tar.gz'
        with tarfile.open(backup,'w:gz') as tar:
            info={'previous_current':previous,'states':self.states,'files':[]}
            for index,(path,saved) in enumerate(self.saved.items()):
                if saved is not None:
                    data,mode,uid,gid=saved
                    temporary=self.stage/('backup-'+str(index))
                    temporary.write_bytes(data);os.chmod(temporary,0o600)
                    tar.add(temporary,arcname='files/'+str(index));temporary.unlink()
                    info['files'].append({'path':'/'+str(path.relative_to(self.root)),'entry':'files/'+str(index),'mode':mode,'uid':uid,'gid':gid})
            if previous:
                old_release=(self.base/previous).resolve()
                if old_release.parent == (self.base/'releases').resolve(): tar.add(old_release,arcname='previous-release')
            temporary=self.stage/'rollback.json';temporary.write_text(json.dumps(info,indent=2))
            tar.add(temporary,arcname='rollback.json');temporary.unlink()
        os.chmod(backup,0o600)
        os.chown(backup,pwd.getpwnam(self.user).pw_uid,-1)

    def cleanup(self, release):
        for path in self.legacy_units+self.legacy_binaries+self.legacy_environments:
            if path.exists(): path.unlink(); self.removed.append(str(path))
        for directory in {path.parent for path in self.legacy_binaries+self.legacy_environments+self.legacy_units}:
            if directory in (self.root/'etc',self.root/'etc/default',self.units): continue
            try: directory.rmdir()
            except OSError: pass
        # Only directories named by the former test/deployment runners, owned
        # by the deployment account or root. Never traverse unrelated /tmp data.
        uid=pwd.getpwnam(self.user).pw_uid
        for path in (self.root/'tmp').iterdir():
            if path == self.stage or path.is_symlink() or not path.is_dir(): continue
            if path.stat().st_uid not in (0,uid): continue
            if any(re.fullmatch(re.escape(prefix)+r'[A-Za-z0-9]{8}',path.name) for prefix in TEMP_PREFIXES):
                shutil.rmtree(path); self.removed.append(str(path))
        names={'mongoose-probe','mongoose-probe-mbedtls','http-client-probe','http-client-probe-mbedtls'}
        for directory in (self.root/'tmp', self.root/Path(pwd.getpwnam(self.user).pw_dir).relative_to('/')):
            if not directory.is_dir(): continue
            for name in names:
                path=directory/name
                if path.is_symlink() or not path.is_file() or path.stat().st_uid not in (0,uid): continue
                with path.open('rb') as stream: header=stream.read(20)
                if header[:6]==b'\x7fELF\x02\x01' and header[18:20]==b'\xb7\x00':
                    path.unlink(); self.removed.append(str(path))
        for old in (self.base/'releases').iterdir():
            if old != release and old.is_dir() and not old.is_symlink() and re.fullmatch(r'[a-f0-9]{24}',old.name):
                shutil.rmtree(old);self.removed.append(str(old))
        self.systemctl('daemon-reload')
        remaining={}
        for parent in {path.parent for path in self.legacy_binaries}:
            if parent.exists(): remaining[str(parent)]=sorted(path.name for path in parent.iterdir())
        report={'removed':self.removed,'services':self.health_report,
            'remaining_legacy_directories':remaining,'rollback_archive':'rollback.tar.gz',
            'waste_schedule':str(self.schedule_path)}
        path=self.stage/'report.json';path.write_text(json.dumps(report,indent=2)+'\n')
        os.chmod(path,0o600);os.chown(path,uid,-1)
        for path in self.removed: print('Removed:',path,flush=True)

    def update(self, manifest):
        """Replace selected applications; the release retains all other binaries.

        A release switch changes paths only. Unselected processes and units are
        untouched, including during rollback. The shared lock serializes writers.
        """
        selected = manifest['selected']
        if (not isinstance(selected, list) or not selected or len(set(selected)) != len(selected)
                or not set(selected) <= set(NAMES) or set(manifest['binaries']) != set(selected)):
            raise RuntimeError('Invalid application selection')
        previous = os.readlink(self.current) if self.current.is_symlink() else None
        if self.current.exists() and previous is None:
            raise RuntimeError('current must be a symlink')
        if previous and not re.fullmatch(r'releases/[a-f0-9]{24}', previous):
            raise RuntimeError('Unexpected current release')
        for unit in OLD:
            if self.property(unit, 'LoadState') not in ('', 'not-found'):
                raise RuntimeError('Legacy installation requires the full migration installer')
        if 'kafel-dashboard' not in selected and self.systemctl('is-active', '--quiet', 'kafel-dashboard.service', check=False).returncode != 0:
            raise RuntimeError('Deploy and start kafel-dashboard first')
        untouched_before={unit:self.property(unit,'MainPID') for unit in NEW}
        if self.shared_socket(manifest)!=self.shared_socket({}) and 'kafel-dashboard' not in selected:
            raise RuntimeError('Socket change requires deploying the dashboard and producers together')
        desired = {}
        for name in selected:
            binary = self.stage/name
            if hashlib.sha256(binary.read_bytes()).hexdigest() != manifest['binaries'][name]:
                raise RuntimeError('Checksum mismatch: '+name)
            os.chmod(binary, 0o755)
            if not self.command(str(binary), '--version').stdout.startswith(name+' '):
                raise RuntimeError('Wrong executable: '+name)
            unit = name+'.service'
            loaded = self.property(unit, 'LoadState') not in ('', 'not-found')
            if loaded and (shlex.split(self.property(unit, 'DropInPaths')) or
                    'path=/opt/kafel/current/bin/'+name+' ' not in self.property(unit, 'ExecStart') or
                    self.property(unit, 'User') != self.user):
                raise RuntimeError('Unrecognized existing unit: '+unit)
            self.states[unit] = {'loaded': loaded,
                'active': self.systemctl('is-active', '--quiet', unit, check=False).returncode == 0,
                'enabled': self.systemctl('is-enabled', '--quiet', unit, check=False).returncode == 0}
            desired[self.units/unit] = (self.stage/(unit+'.in')).read_text().replace('@USER@', self.user).encode()
        if 'kafel-dashboard' in selected:
            self.dashboard_configuration(manifest)
            desired.update(self.display_files)
        if 'kafel-weather' in selected:
            desired[self.envfile] = self.environment(manifest)
        if 'kafel-waste' in selected:
            self.waste_configuration(manifest)
            desired[self.waste_envfile] = self.waste_environment
        changed = []
        for name in selected:
            binary = self.current/'bin'/name
            files = [self.units/(name+'.service')]
            if name == 'kafel-dashboard': files.extend(self.display_files)
            if name == 'kafel-weather': files.append(self.envfile)
            if name == 'kafel-waste': files.append(self.waste_envfile)
            same = (binary.is_file() and hashlib.sha256(binary.read_bytes()).hexdigest() == manifest['binaries'][name]
                    and all(path.is_file() and not path.is_symlink() and path.read_bytes() == desired[path]
                            and path.stat().st_uid == os.geteuid()
                            and path.stat().st_mode & 0o777 == self.display_modes.get(path,0o600 if path in (self.envfile, self.waste_envfile) else 0o644)
                            for path in files))
            state = self.states[name+'.service']
            if not same or not state['active'] or not state['enabled']: changed.append(name)
        def report():
            untouched={unit:{'before_pid':pid,'after_pid':self.property(unit,'MainPID')} for unit,pid in untouched_before.items() if unit.removesuffix('.service') not in changed}
            for value in untouched.values():value['same_pid']=value['before_pid']==value['after_pid']
            status=self.root/'run/kafel-display/status.json'
            if 'kafel-dashboard' in selected and status.is_file():self.display_report['isolation']=json.loads(status.read_text())
            path = self.stage/'report.json'
            path.write_text(json.dumps({'changed': changed, 'unchanged': [n for n in selected if n not in changed],
                                       'services': self.health_report, 'display': self.display_report,'untouched_services':untouched}, indent=2)+'\n')
            os.chmod(path, 0o600)
            os.chown(path, pwd.getpwnam(self.user).pw_uid, -1)
        if not changed:
            report()
            print('  · Wybrane aplikacje bez zmian; bez restartu.', flush=True)
            return
        for name in changed:
            self.snapshot_file(self.units/(name+'.service'))
            if name == 'kafel-dashboard':
                for path in self.display_files:self.snapshot_file(path)
            if name == 'kafel-weather': self.snapshot_file(self.envfile)
            if name == 'kafel-waste': self.snapshot_file(self.waste_envfile)
        contents = {}
        if previous:
            for name in NAMES:
                binary = self.current/'bin'/name
                if binary.is_file(): contents[name] = binary.read_bytes()
        for name in changed: contents[name] = (self.stage/name).read_bytes()
        release_id = hashlib.sha256(json.dumps({n: hashlib.sha256(v).hexdigest() for n,v in contents.items()}, sort_keys=True).encode()).hexdigest()[:24]
        release = self.base/'releases'/release_id
        self.archive(previous)
        (self.stage/'transaction-started').touch()
        units = tuple(name+'.service' for name in changed)
        try:
            (release/'bin').mkdir(parents=True, exist_ok=True)
            for directory in (self.base, self.base/'releases', release, release/'bin'):
                os.chmod(directory, 0o755)
            for name, data in contents.items():
                destination = release/'bin'/name
                if destination.exists():
                    if destination.read_bytes() != data: raise RuntimeError('Installed release checksum mismatch')
                else: atomic_write(destination, data, 0o755)
            for unit in units:
                if self.states[unit]['loaded']: self.systemctl('stop', unit)
            if 'kafel-dashboard' in changed: self.dashboard_command('--check')
            for path, data in desired.items():
                if path in self.saved: atomic_write(path, data, self.display_modes.get(path,0o600 if path in (self.envfile, self.waste_envfile) else 0o644))
            if 'kafel-dashboard' in changed:os.chmod(self.helperfile.parent,0o755)
            self.switch('releases/'+release_id)
            self.systemctl('daemon-reload')
            for unit in units: self.systemctl('enable', unit)
            self.systemctl('start', *units)
            self.health(units, tuple(n for n in NAMES[1:] if n in contents))
            report()
        except BaseException:
            # Limit rollback to the selected transaction, never stop other units.
            self.rollback(previous, units)
            if previous != 'releases/'+release_id: shutil.rmtree(release, ignore_errors=True)
            raise
        # The archive contains the old release; retain no extra copies on CM4.
        for old in (self.base/'releases').iterdir():
            if old != release and old.is_dir() and not old.is_symlink() and re.fullmatch(r'[a-f0-9]{24}', old.name):
                try: shutil.rmtree(old)
                except OSError as error: print('Cleanup warning: '+str(error), flush=True)
        print('  ✓ Zaktualizowano: '+', '.join(changed), flush=True)

    def install(self):
        manifest = json.loads((self.stage/'manifest.json').read_text())
        if 'selected' in manifest:
            return self.update(manifest)
        manifest,environment=self.preflight()
        self.base.mkdir(parents=True,exist_ok=True)
        previous=os.readlink(self.current) if self.current.is_symlink() else None
        if self.current.exists() and previous is None: raise RuntimeError('current must be a symlink')
        if previous and not re.fullmatch(r'releases/[a-f0-9]{24}',previous): raise RuntimeError('Unexpected current release')
        release_id=hashlib.sha256(json.dumps(manifest['binaries'],sort_keys=True).encode()).hexdigest()[:24]
        release=self.base/'releases'/release_id
        release.mkdir(parents=True,exist_ok=True)
        (release/'bin').mkdir(exist_ok=True)
        for name in NAMES:
            destination=release/'bin'/name
            if previous=='releases/'+release_id:
                if hashlib.sha256(destination.read_bytes()).hexdigest()!=manifest['binaries'][name]: raise RuntimeError('Installed checksum mismatch')
            else:
                atomic_write(destination,(self.stage/name).read_bytes(),0o755)
        for directory in (self.base, self.base/'releases', release, release/'bin'):
            os.chmod(directory,0o755)
        if previous:
            for name in NAMES: self.snapshot_file(self.base/previous/'bin'/name)
        try:
            self.archive(previous)
        except BaseException:
            if previous!='releases/'+release_id: shutil.rmtree(release,ignore_errors=True)
            raise
        (self.stage/'transaction-started').touch()
        try:
            for unit in NEW+OLD:
                if self.states[unit]['loaded']: self.systemctl('stop',unit)
            self.dashboard_command('--check')
            for unit in NEW:
                template=(self.stage/(unit+'.in')).read_text().replace('@USER@',self.user)
                atomic_write(self.units/unit,template.encode())
            for path,data in self.display_files.items():atomic_write(path,data,self.display_modes[path])
            os.chmod(self.helperfile.parent,0o755)
            atomic_write(self.envfile,environment,0o600)
            atomic_write(self.waste_envfile,self.waste_environment,0o600)
            self.switch('releases/'+release_id)
            self.systemctl('daemon-reload')
            for unit in NEW: self.systemctl('enable',unit)
            self.systemctl('start',*NEW)
            self.health()
            for unit in OLD:
                if self.states[unit]['loaded']: self.systemctl('disable',unit)
            self.cleanup(release)
        except BaseException:
            self.rollback(previous)
            if previous!='releases/'+release_id: shutil.rmtree(release,ignore_errors=True)
            raise
        print('Kafel deployed: kafel-dashboard.service + kafel-weather.service + kafel-waste.service; schedule unchanged.',flush=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage',type=Path);parser.add_argument('user')
    args=parser.parse_args()
    if os.geteuid()!=0: parser.error('Run through sudo')
    if not re.fullmatch(r'/tmp/kafel-deploy\.[A-Za-z0-9]{8}',str(args.stage)): parser.error('Unexpected staging directory')
    if not re.fullmatch(r'[a-z_][a-z0-9_.-]*',args.user): parser.error('Invalid account')
    if pwd.getpwnam(args.user).pw_uid==0: parser.error('Use a non-root service account')
    for group in ('video','input'): grp.getgrnam(group)
    def interrupted(number,frame): raise InterruptedError('Deployment interrupted')
    for signum in (signal.SIGHUP,signal.SIGINT,signal.SIGTERM): signal.signal(signum,interrupted)
    fd=os.open('/run/lock/kafel-deploy.lock',os.O_RDWR|os.O_CREAT|os.O_CLOEXEC|os.O_NOFOLLOW,0o600)
    with os.fdopen(fd,'rb') as lock:
        metadata=os.fstat(lock.fileno())
        if not stat.S_ISREG(metadata.st_mode) or metadata.st_uid!=0 or metadata.st_mode&0o077:
            raise RuntimeError('Invalid deployment lock')
        fcntl.flock(lock.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB)
        Installer(args.stage,args.user).install()

if __name__=='__main__':
    try: main()
    except Exception as error: raise SystemExit('Deployment failed: '+str(error))
