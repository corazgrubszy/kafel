"""Real daemon and Unix inbox; synthetic fixture only, never the user's schedule."""
import re
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

sys.path.insert(0,str(Path(__file__).resolve().parents[3]/'scripts'))
from dash5_test_peer import listener, collect
binary=sys.argv[1]
with tempfile.TemporaryDirectory(prefix='kwaste-',dir='/tmp') as directory:
    root=Path(directory);schedule=root/'schedule.json';inbox=root/'status.sock'
    today=datetime.datetime.now(ZoneInfo('Europe/Warsaw')).date()
    schedule.write_text(json.dumps({'timezone':'Europe/Warsaw','pickups':[{'date':today.isoformat(),'fraction':'paper'},{'date':(today+datetime.timedelta(days=1)).isoformat(),'fraction':'glass'}]}))
    before=hashlib.sha256(schedule.read_bytes()).hexdigest()
    environment=dict(os.environ,KAFEL_WASTE_SCHEDULE=str(schedule),KAFEL_DASHBOARD_SOCKET=str(inbox))
    with listener(inbox) as receiver:
        process=subprocess.Popen([binary],env=environment,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            snapshots, resources=collect(receiver)
            for tiles in snapshots:
                tile=tiles[0]
                assert tile['title']=='Śmieci' and tile['state']=='ok' and tile['attention']
                assert tile['content']['data']['caption']=='Dziś'
                assert tile['content']['data']['fractions']=='Jutro: Szkło'
                assert 'month' not in tile['content']['data']
                graphic=resources[tile['content']['data']['illustration']][1].decode()
                assert '#0072CE' in graphic
            assert snapshots[0]==snapshots[1], 'no five-second rotation when today and tomorrow both have pickup'
            process.terminate();stdout,stderr=process.communicate(timeout=4)
            assert process.returncode==0,(stdout,stderr)
            assert 'waste schedule loaded:' in stderr
            assert stderr.rfind('PUBLISHED kafel-waste:') > stderr.rfind('waste schedule loaded:'), stderr
            assert hashlib.sha256(schedule.read_bytes()).hexdigest()==before
            print('PASS waste daemon DASH/5 upload, atomic bins/caption, heartbeat, readonly schedule and SIGTERM')
        finally:
            if process.poll() is None:process.kill();process.communicate()
    check=subprocess.run([binary,'--check',str(schedule)],capture_output=True,text=True,timeout=3)
    assert check.returncode==0,check.stderr
    environment['KAFEL_WASTE_SCHEDULE']=str(root/'missing.json')
    inbox.unlink()
    with listener(inbox) as receiver:
        process=subprocess.Popen([binary],env=environment,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            snapshots,_=collect(receiver,1)
            assert snapshots[0][0]['state']=='error' and snapshots[0][0]['content'] is None
            process.terminate();process.communicate(timeout=4)
            assert process.returncode==0
        finally:
            if process.poll() is None:process.kill();process.communicate()
    version=subprocess.run([binary,'--version'],capture_output=True,timeout=3)
    assert version.returncode==0
    assert re.search(rb' git=(?:[0-9a-f]{40}(?:-dirty)?|unknown)\n$',version.stdout)
    print('PASS waste missing file stays alive, configuration validation and diagnostic check')
