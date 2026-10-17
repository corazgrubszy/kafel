#!/usr/bin/env python3
"""Root-only fbcon routing for the dedicated LCD; no daemon and no reboot.

Invoked before every dashboard start. With a forced, verified vc4 HDMI fb,
map all VTs there; otherwise unbind fbcon so it cannot overwrite the LCD.
Isolation deliberately persists after a crash/stop. Restore is explicit.
"""
import argparse
import fcntl
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

HDMI_MODE = 'video=HDMI-A-1:640x480@60D'
MAX_NR_CONSOLES = 63  # Linux UAPI: include/uapi/linux/vt.h
FBIOGET_CON2FBMAP = 0x460F
FBIOPUT_CON2FBMAP = 0x4610


def boot_configuration(text):
    lines=text.strip().splitlines()
    if len(lines)!=1 or '\x00' in text:
        raise ValueError('cmdline.txt must contain one nonempty line')
    tokens=lines[0].split()
    if len([t for t in tokens if t.startswith('root=')])!=1:
        raise ValueError('Expected exactly one root= parameter; refusing to rewrite boot configuration')
    hdmi=[t for t in tokens if t.startswith('video=HDMI-A-1:')]
    if len(hdmi)>1 or (hdmi and hdmi[0]!=HDMI_MODE):
        raise ValueError('Existing HDMI-A-1 mode differs; resolve it explicitly before deployment')
    result=[]
    for token in tokens:
        if token.startswith('fbcon=map') or token==HDMI_MODE: continue
        if re.fullmatch(r'console=tty[0-9]+', token): continue
        result.append(token)
    result.append('console=tty2')
    result.append(HDMI_MODE)
    return ' '.join(result)+'\n'


class LinuxConsole:
    def __init__(self, binary, root=Path('/'), lcd=None):
        self.root=root
        if lcd is None:
            result=subprocess.run([str(binary),'--display-info'],check=True,text=True,capture_output=True,timeout=10)
            lcd=result.stdout.strip()
        self.lcd=lcd
        if not re.fullmatch(r'/dev/fb[0-9]+',self.lcd):
            # Custom aliases resolve to a canonical fbN before console ioctls.
            self.lcd=str(Path(self.lcd).resolve())
            if not re.fullmatch(r'/dev/fb[0-9]+',self.lcd):raise RuntimeError('Cannot identify LCD framebuffer index')
        self.fd=os.open(self.lcd,os.O_RDONLY|os.O_CLOEXEC)
    def close(self): os.close(self.fd)
    def bindings(self):
        result={}
        for path in (self.root/'sys/class/vtconsole').glob('vtcon*'):
            if 'frame buffer' in (path/'name').read_text():
                result[path.name]=int((path/'bind').read_text().strip())
        return result
    def bind(self,name,value):
        if not re.fullmatch(r'vtcon[0-9]+',name):raise ValueError('Invalid console name')
        path=self.root/'sys/class/vtconsole'/name/'bind'
        path.write_text(str(value))
        if int(path.read_text().strip())!=value:raise RuntimeError('Console binding verification failed')
    def maps(self):
        result=[]
        for tty in range(1,MAX_NR_CONSOLES+1):
            data=bytearray(struct.pack('II',tty,0))
            fcntl.ioctl(self.fd,FBIOGET_CON2FBMAP,data,True)
            result.append(struct.unpack('II',data)[1])
        return result
    def map(self,tty,framebuffer):
        fcntl.ioctl(self.fd,FBIOPUT_CON2FBMAP,struct.pack('II',tty,framebuffer))
    def hdmi(self):
        # A forced digital connector remains available with the monitor absent.
        # Never infer HDMI solely from fb1, geometry or "not the LCD".
        cmdline=(self.root/'proc/cmdline').read_text().split()
        if HDMI_MODE not in cmdline:return None
        candidates=[]
        for fb in (self.root/'sys/class/graphics').glob('fb[0-9]*'):
            if '/dev/'+fb.name==self.lcd:continue
            if (fb/'name').read_text().strip()!='vc4drmfb':continue
            device=(fb/'device').resolve()
            for card in (self.root/'sys/class/drm').glob('card[0-9]*'):
                if not re.fullmatch(r'card[0-9]+',card.name):continue
                if device not in ((card/'device').resolve(),card.resolve()):continue
                if (self.root/'sys/class/drm'/(card.name+'-HDMI-A-1')).exists():
                    candidates.append(int(fb.name[2:]))
        unique=set(candidates)
        if len(unique)>1:raise RuntimeError('Ambiguous HDMI framebuffer')
        return next(iter(unique)) if unique else None


def snapshot(console):
    bindings=console.bindings()
    # If fbcon is absent/unbound, mapping ioctls need not be supported.
    return {'lcd':console.lcd,'bindings':bindings,'maps':console.maps() if any(bindings.values()) else []}


def restore(console,state):
    errors=[]
    # Restore maps before binding text rendering again.
    for tty,fb in enumerate(state['maps'],1):
        if fb>=32:continue  # -1/unmapped is not a settable fbdev target
        try:console.map(tty,fb)
        except Exception as error:errors.append(str(error))
    for name,bound in state['bindings'].items():
        try:console.bind(name,bound)
        except Exception as error:errors.append(str(error))
    if errors:raise RuntimeError('Console restore failed: '+'; '.join(errors))


def isolate(console):
    before=snapshot(console)
    try:
        target=console.hdmi()
        if target is not None and target==int(console.lcd.removeprefix('/dev/fb')):
            raise RuntimeError('Console target equals LCD')
        if target is not None and any(before['bindings'].values()) and all(fb<32 for fb in before['maps']):
            for tty in range(1,MAX_NR_CONSOLES+1):console.map(tty,target)
            if console.maps()!=[target]*MAX_NR_CONSOLES:raise RuntimeError('HDMI mapping verification failed')
            return {'mode':'hdmi','lcd':console.lcd,'console':'/dev/fb'+str(target)}
        for name,bound in before['bindings'].items():
            if bound:console.bind(name,0)
        if any(console.bindings().values()):raise RuntimeError('fbcon is still attached')
        return {'mode':'detached','lcd':console.lcd,'console':None}
    except BaseException:
        restore(console,before)
        raise


def save(path,value):
    fd,temp=tempfile.mkstemp(dir=path.parent,prefix='.'+path.name)
    try:
        with os.fdopen(fd,'w') as stream:
            json.dump(value,stream,indent=2);stream.write('\n');stream.flush();os.fsync(stream.fileno())
        os.chmod(temp,0o600);os.replace(temp,path)
    finally:
        if os.path.exists(temp):os.unlink(temp)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=('isolate','restore'))
    parser.add_argument('--binary',default='/opt/kafel/current/bin/kafel-dashboard')
    args=parser.parse_args()
    if os.geteuid()!=0:parser.error('Root required for console routing')
    directory=Path('/run/kafel-display')
    directory.mkdir(mode=0o700,exist_ok=True)
    if directory.is_symlink() or directory.stat().st_uid!=0 or directory.stat().st_mode&0o077:
        raise RuntimeError('Unsafe display state directory')
    lock=os.open(directory/'lock',os.O_RDWR|os.O_CREAT|os.O_NOFOLLOW|os.O_CLOEXEC,0o600)
    with os.fdopen(lock,'rb') as stream:
        fcntl.flock(stream,fcntl.LOCK_EX|fcntl.LOCK_NB)
        original=directory/'original.json'
        saved=json.loads(original.read_text()) if args.action=='restore' else None
        console=LinuxConsole(args.binary,lcd=saved['lcd'] if saved else None)
        try:
            if args.action=='restore':
                restore(console,saved)
                original.unlink();(directory/'status.json').unlink(missing_ok=True)
                print('display: original console routing restored')
            else:
                if not original.exists():save(original,snapshot(console))
                report=isolate(console)
                save(directory/'status.json',report)
                print('display: '+json.dumps(report))
        finally:console.close()


if __name__=='__main__':
    try:main()
    except Exception as error:raise SystemExit('display isolation failed: '+str(error))
