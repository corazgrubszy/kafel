"""Build and measure a disposable executable; never install or restart services."""
import argparse
import pathlib
import re
import shlex
import subprocess
import sys
import tarfile
import tempfile
root = pathlib.Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('target', help='Explicit SSH destination: user@host')
args = parser.parse_args()
if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_.-]*@(?:[A-Za-z0-9][A-Za-z0-9.:-]*|\[[A-Fa-f0-9:]+\])', args.target):
    parser.error('Expected an SSH destination in user@host form')
host = args.target
with tempfile.TemporaryDirectory(prefix='kafel-http-') as temporary:
    local = pathlib.Path(temporary)
    subprocess.run(['zig','build','-Dtarget=aarch64-linux-musl','-Doptimize=ReleaseSmall','--prefix',str(local/'build')], cwd=root, check=True)
    subprocess.run([sys.executable,str(root/'tests/make_certificates.py'),str(local/'fixtures')],check=True)
    with tarfile.open(local/'test.tar','w') as archive:
        archive.add(local/'build/bin/http-client-probe',arcname='http-client-probe')
        for name in ('integration.py','measure.py'): archive.add(root/'tests'/name,arcname=name)
        archive.add(local/'fixtures',arcname='fixtures')
    options=['-o','ControlMaster=auto','-o','ControlPersist=60','-o','ControlPath='+str(local/'ssh')]
    remote=None
    try:
        path=subprocess.check_output(['ssh',*options,host,'mktemp -d /tmp/kafel-http.XXXXXXXX'],text=True).strip()
        if not path.startswith('/tmp/kafel-http.') or '/' in path[len('/tmp/'):]: raise RuntimeError('Unexpected remote path')
        remote=path
        with (local/'test.tar').open('rb') as data:
            subprocess.run(['ssh',*options,host,'tar -xf - -C '+shlex.quote(remote)],stdin=data,check=True)
        result=subprocess.run(['ssh',*options,host,'cd '+shlex.quote(remote)+' && python3 measure.py'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        output=root/'results'; output.mkdir(exist_ok=True)
        (output/'cm4.log').write_text(result.stdout)
        print(result.stdout,end=''); print('Report:',output/'cm4.log')
        if result.returncode: raise SystemExit(result.returncode)
    finally:
        if remote: subprocess.run(['ssh',*options,host,'rm -rf -- '+shlex.quote(remote)],check=False)
        subprocess.run(['ssh',*options,'-O','exit',host],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=False)
