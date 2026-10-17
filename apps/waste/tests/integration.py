"""Real daemon and Unix inbox; synthetic fixture only, never the user's schedule."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
from zoneinfo import ZoneInfo

binary=sys.argv[1]
with tempfile.TemporaryDirectory(prefix='kwaste-',dir='/tmp') as directory:
    root=Path(directory);schedule=root/'schedule.json';inbox=root/'status.sock'
    today=datetime.datetime.now(ZoneInfo('Europe/Warsaw')).date()
    schedule.write_text(json.dumps({'timezone':'Europe/Warsaw','pickups':[{'date':today.isoformat(),'fraction':'paper'}]}))
    before=hashlib.sha256(schedule.read_bytes()).hexdigest()
    environment=dict(os.environ,KAFEL_WASTE_SCHEDULE=str(schedule),KAFEL_DASHBOARD_SOCKET=str(inbox))
    with socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM) as receiver:
        receiver.bind(str(inbox));receiver.settimeout(8)
        process=subprocess.Popen([binary],env=environment,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            for _ in range(2):
                wire=receiver.recv(8192).decode()
                assert wire=='DASH/4\tkafel-waste\tKafel Waste\nTILE\twaste\t1\tok\tattention\tŚmieci\ttext\t[#0072CE]🗑[/]\u2028Dzisiaj\n',wire
            process.terminate();stdout,stderr=process.communicate(timeout=3)
            assert process.returncode==0,(stdout,stderr)
            assert 'waste schedule loaded:' in stderr
            assert hashlib.sha256(schedule.read_bytes()).hexdigest()==before,'schedule must never be modified'
            print('PASS waste daemon heartbeat, single-column snapshot, attention, readonly schedule and SIGTERM')
        finally:
            if process.poll() is None:process.kill();process.communicate()
    check=subprocess.run([binary,'--check',str(schedule)],capture_output=True,text=True,timeout=3)
    assert check.returncode==0,check.stderr
    environment['KAFEL_WASTE_SCHEDULE']=str(root/'missing.json')
    with socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM) as receiver:
        inbox.unlink();receiver.bind(str(inbox));receiver.settimeout(3)
        process=subprocess.Popen([binary],env=environment,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            assert '\terror\tnormal\tŚmieci\ttext\tBrak harmonogramu\n' in receiver.recv(8192).decode()
            process.terminate();process.communicate(timeout=3)
            assert process.returncode==0
        finally:
            if process.poll() is None:process.kill();process.communicate()
    environment['KAFEL_WASTE_SPAN']='3'
    assert subprocess.run([binary],env=environment,capture_output=True,timeout=3).returncode==2
    assert subprocess.run([binary,'--version'],capture_output=True,timeout=3).returncode==0
    print('PASS waste missing file stays alive, configuration validation and diagnostic check')
