#!/usr/bin/env python3
"""Deploy the ARM64 applications, migrate units and clean identified old artifacts."""
import argparse
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import time
import sys
from datetime import datetime, timezone
ROOT=Path(__file__).resolve().parent.parent
NAMES=('kafel-dashboard','kafel-weather','kafel-waste')

def settings(argv=None, root=ROOT):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',default='');parser.add_argument('--user',default='')
    parser.add_argument('--socket')
    parser.add_argument('--only', choices=('dashboard','weather','waste','all'), default='all')
    parser.add_argument('--build', action='store_true')
    for name in ('dashboard','weather','waste'):
        parser.add_argument(name, type=Path, nargs='?', default=root/'zig-out/linux-arm64'/('kafel-'+name))
    args=parser.parse_args(argv)
    local=root/'deploy.local.json'; config={}
    if local.exists():
        try: config=json.loads(local.read_text())
        except (OSError,ValueError) as error: parser.error(str(error))
        if not isinstance(config,dict) or set(config)-{'host','user','ssh_socket','latitude','longitude','waste_schedule','waste_span','framebuffer','touch','dashboard_socket','idle_timeout'} or any(not isinstance(value,str) for value in config.values()):
            parser.error('Invalid deploy.local.json settings')
    args.host=args.host or config.get('host','');args.user=args.user or config.get('user','')
    args.socket=args.socket if args.socket is not None else config.get('ssh_socket')
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9.:-]*',args.host): parser.error('Set host in deploy.local.json or -Dhost=...')
    if not re.fullmatch(r'[a-z_][a-z0-9_.-]*',args.user): parser.error('Set user in deploy.local.json or -Duser=...')
    args.configuration={key:config[key] for key in ('latitude','longitude','waste_schedule','waste_span','framebuffer','touch','dashboard_socket','idle_timeout') if key in config}
    return args

@contextmanager
def connection(target, borrowed_socket=None):
    # Reuse an explicitly supplied live master, but never close somebody else's
    # session. Otherwise authenticate once and own a private short-lived socket.
    if borrowed_socket:
        options = ['-o', 'ControlPath=' + borrowed_socket, '-o', 'ConnectTimeout=10']
        checked = subprocess.run(['ssh', *options, '-o', 'BatchMode=yes', '-O', 'check', target],
                                 stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL, check=False, timeout=10)
        if checked.returncode == 0:
            yield [*options, '-o', 'BatchMode=yes']
            return
    with tempfile.TemporaryDirectory(prefix='kafel-ssh-', dir='/tmp') as directory:
        options = ['-o', 'ControlPath=' + str(Path(directory) / 'ssh'), '-o', 'ConnectTimeout=10']
        try:
            subprocess.run(['ssh', *options, '-o', 'ControlMaster=yes',
                            '-o', 'ControlPersist=no', '-N', '-f', target], check=True)
            yield [*options, '-o', 'BatchMode=yes']
        finally:
            try:
                subprocess.run(['ssh', *options, '-o', 'BatchMode=yes', '-O', 'exit', target],
                               stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, check=False, timeout=10)
            except (OSError, subprocess.SubprocessError):
                pass


def arm64_digest(path):
    data=path.read_bytes()
    if data[:6]!=b'\x7fELF\x02\x01' or data[18:20]!=b'\xb7\x00':
        raise RuntimeError('Expected ARM64 ELF: '+str(path))
    return hashlib.sha256(data).hexdigest()

def stage_binary(ssh, scp, target, stage, name, binary, digest, log):
    """Reuse identical installed bytes; installer verifies them again under lock."""
    remote = '/opt/kafel/current/bin/'+name
    result = subprocess.run([*ssh, target, 'sha256sum -- '+shlex.quote(remote)], text=True,
                            stdout=subprocess.PIPE, stderr=log, check=False)
    fields = result.stdout.split()
    unchanged = result.returncode == 0 and bool(fields) and fields[0] == digest
    if unchanged:
        subprocess.run([*ssh, target, 'cp -- '+shlex.join([remote, stage+'/'+name])], stdout=log, stderr=log, check=True)
    else:
        subprocess.run([*scp, str(binary), target+':'+stage+'/'+name], stdout=log, stderr=log, check=True)
    return unchanged


def interactive_install(command, log):
    """Preserve stdin/SSH PTY for passwords; mirror output into the private log."""
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as process:
        while True:
            chunk = process.stdout.read1(4096)
            if not chunk: break
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
            log.write(chunk.decode(errors='replace'))
            log.flush()
        if process.wait(): raise subprocess.CalledProcessError(process.returncode, command)


def deploy(args):
    started = time.monotonic()
    selected = NAMES if args.only == 'all' else ('kafel-'+args.only,)
    binaries = {name: getattr(args, name.removeprefix('kafel-')) for name in selected}
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    reports = ROOT/'deploy-results'/stamp
    reports.mkdir(parents=True, mode=0o700)
    reports.parent.chmod(0o700)
    reports.chmod(0o700)
    log_path = reports/'deploy.log'
    log_path.touch(mode=0o600)
    print('\nKAFEL — WDROŻENIE', flush=True)
    print(', '.join(selected)+' → '+args.host, flush=True)
    print('\n[1/4] Testy i budowa ARM64', flush=True)
    with log_path.open('a') as log:
        try:
            if args.build:
                builds = {'dashboard': 'linux', 'weather': 'weather-linux', 'waste': 'waste-linux'}
                checks = {'dashboard': 'dashboard-test', 'weather': 'weather-test', 'waste': 'waste-test'}
                tasks = ['test', *builds.values()] if args.only == 'all' else [checks[args.only], 'deploy-test', builds[args.only]]
                subprocess.run(['zig', 'build', *tasks, '-Doptimize=ReleaseFast'], cwd=ROOT,
                               stdout=log, stderr=log, check=True)
            manifest = {'selected': list(selected), 'binaries': {n: arm64_digest(p) for n,p in binaries.items()}, **args.configuration}
            if 'kafel-dashboard' in selected:
                manifest['display_helper_sha256']=hashlib.sha256((ROOT/'scripts/display_console.py').read_bytes()).hexdigest()
            if 'kafel-waste' in selected:
                manifest['waste_data_sha256']=hashlib.sha256((ROOT/'scripts/waste_data.py').read_bytes()).hexdigest()
            for name, path in binaries.items():
                print(f'  ✓ {name} · {path.stat().st_size/1024:.0f} KiB', flush=True)
            target = args.user+'@'+args.host
            with tempfile.TemporaryDirectory(prefix='kafel-package-') as directory:
                local = Path(directory)
                (local/'manifest.json').write_text(json.dumps(manifest)+'\n')
                print('\n[2/4] Połączenie i porównanie', flush=True)
                with connection(target, args.socket) as options:
                    ssh, scp = ['ssh', *options], ['scp', '-q', *options]
                    stage = subprocess.check_output([*ssh, target, 'mktemp -d /tmp/kafel-deploy.XXXXXXXX'], text=True, stderr=log).strip()
                    if not re.fullmatch(r'/tmp/kafel-deploy\.[A-Za-z0-9]{8}', stage):
                        raise RuntimeError('Unexpected remote stage')
                    print('  ✓ Połączono', flush=True)
                    attempted = saved = False
                    try:
                        for name, binary in binaries.items():
                            unchanged = stage_binary(ssh, scp, target, stage, name, binary, manifest['binaries'][name], log)
                            print(('  · '+name+' · binarka bez zmian, bez transferu') if unchanged else ('  ✓ '+name+' · przesłano'), flush=True)
                        for source in (ROOT/'scripts/install.py', local/'manifest.json',
                                       *((ROOT/'scripts/display_console.py',) if 'kafel-dashboard' in selected else ()),
                                       *((ROOT/'scripts/waste_data.py',) if 'kafel-waste' in selected else ()),
                                       *(ROOT/'packaging/systemd'/(name+'.service.in') for name in selected)):
                            subprocess.run([*scp, str(source), target+':'+stage+'/'+source.name], stdout=log, stderr=log, check=True)
                        print('\n[3/4] Weryfikacja i instalacja wybranych aplikacji', flush=True)
                        command = 'sudo python3 '+shlex.join([stage+'/install.py', stage, args.user])
                        attempted = True
                        # Keep the real terminal for sudo. Installer output is short;
                        # its structured report supplies the final per-app result.
                        interactive_install([*ssh, '-q', '-tt', target, command], log)
                    finally:
                        if attempted:
                            archive = subprocess.run([*scp, target+':'+stage+'/rollback.tar.gz', str(reports/'rollback.tar.gz')], stdout=log, stderr=log, check=False)
                            saved = archive.returncode == 0
                            if saved: (reports/'rollback.tar.gz').chmod(0o600)
                            subprocess.run([*scp, target+':'+stage+'/report.json', str(reports/'report.json')], stdout=log, stderr=log, check=False)
                        untouched = False
                        if attempted and not saved:
                            probe = subprocess.run([*ssh, target, 'test -e '+shlex.quote(stage+'/transaction-started')], stdout=log, stderr=log, check=False)
                            untouched = probe.returncode == 1
                        if not attempted or saved or untouched:
                            subprocess.run([*ssh, target, 'rm -rf -- '+shlex.quote(stage)], stdout=log, stderr=log, check=False)
                        else:
                            raise RuntimeError('Nie pobrano archiwum odzyskiwania; pozostawiono '+stage)
                    print('\n[4/4] Kontrola działania', flush=True)
                    report = json.loads((reports/'report.json').read_text())
                    for name in report['changed']: print('  ✓ '+name+' · uruchomiono i sprawdzono', flush=True)
                    for name in report['unchanged']: print('  · '+name+' · bez zmian, bez restartu', flush=True)
                    for unit,value in report.get('untouched_services',{}).items():
                        if value['same_pid'] and value['after_pid'] not in ('','0'):
                            print('  ✓ '+unit+' · PID '+value['after_pid']+' bez zmian',flush=True)
                        elif not value['same_pid']:
                            print('  ! '+unit+' · PID zmienił się poza zakresem wdrożenia',flush=True)
                    schedule=report.get('waste_schedule')
                    if schedule and schedule.get('migration'):
                        print('  ✓ Harmonogram: '+schedule['path']+' · SHA-256 zgodne',flush=True)
                    cleanup=report.get('infohub_cleanup')
                    if cleanup and cleanup.get('schedule_removed'):
                        print('  ✓ Stary harmonogram usunięty · kopia: '+cleanup['schedule_backup'],flush=True)
                    if cleanup and cleanup.get('directory_remaining'):
                        print('  ! Katalog infohub zawiera inne pliki; pozostawiono je bez zmian.',flush=True)
                    if report.get('display',{}).get('reboot_required'):
                        print('  ! HDMI bez monitora skonfigurowane; wymaga restartu CM4. Teraz LCD chroni odłączenie fbcon.',flush=True)
            print(f'\n✓ Gotowe · {time.monotonic()-started:.1f} s', flush=True)
        except BaseException:
            print('\n✗ Wdrożenie nie powiodło się. Szczegóły i wynik odzyskiwania w dzienniku/raporcie.', file=sys.stderr)
            raise
        finally:
            print('Dziennik: '+str(log_path), flush=True)

if __name__=='__main__':
    try: deploy(settings())
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as error: raise SystemExit('Deployment failed: '+str(error))
    except KeyboardInterrupt: raise SystemExit('Deployment interrupted')
