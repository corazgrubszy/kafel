"""Exercise the real HttpClient -> Open-Meteo decoder -> DASH/5 path offline."""
import re
import json
import http.server
import subprocess
import sys
import threading

FORECAST = b'{"current":{"temperature_2m":12.8,"apparent_temperature":11.2,"wind_speed_10m":18,"weather_code":2,"is_day":1,"time":1800000900},"hourly":{"time":[1800000000,1800003600],"rain":[0,1],"snowfall":[0,0],"precipitation":[0.4,1.5]}}'
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_GET(self):
        body = FORECAST if self.path in ('/valid', '/status') else b'x' * 65537 if self.path == '/large' else b'{}'
        self.send_response(503 if self.path == '/status' else 200)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        try: self.wfile.write(body)
        except OSError: pass

server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
try:
    for path, success in (('/valid', True), ('/status', False), ('/invalid', False), ('/large', False)):
        result = subprocess.run([sys.argv[1], f'http://127.0.0.1:{server.server_port}{path}'], capture_output=True, text=True, timeout=10)
        assert result.returncode == (0 if success else 1), (path, result)
        if success:
            assert 'DETAILS apparent=11.2 precipitation=0.4 hour_end=1800000000 code=2 is_day=1' in result.stderr
            tile = json.loads(result.stdout)[0]
            assert tile['state'] == 'ok' and tile['freshness'] == 'fresh'
            assert tile['content']['data']['temperature'] == 12.8
            assert tile['content']['data']['wind_ms'] == 5
            assert tile['content']['data']['description'] == 'Zachmurzenie'
        print('PASS weather HTTP integration', path, flush=True)
finally:
    server.shutdown(); server.server_close(); thread.join(2)

# Exercise the real daemon without external networking: a missing CA fails
# before DNS. Its heartbeat must continue while the source is in retry.
import os
import pathlib
import socket
import tempfile


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3]/'scripts'))
from dash5_test_peer import listener, collect
with tempfile.TemporaryDirectory(prefix='kw-', dir='/tmp') as directory:
    inbox = pathlib.Path(directory)/'dashboard.sock'
    environment = dict(os.environ, KAFEL_WEATHER_LATITUDE='52', KAFEL_WEATHER_LONGITUDE='17',
                       KAFEL_WEATHER_CA_BUNDLE=str(pathlib.Path(directory)/'missing.pem'),
                       KAFEL_DASHBOARD_SOCKET=str(inbox))
    with listener(inbox) as receiver:
        process = subprocess.Popen([sys.argv[2]], env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            snapshots, resources = collect(receiver)
            assert not resources
            assert all(tiles[0]['state'] == 'error' and tiles[0]['content'] is None for tiles in snapshots)
            process.terminate(); stdout, stderr = process.communicate(timeout=4)
            assert process.returncode == 0, (stdout, stderr)
            assert stderr.count('weather refresh failed:') == 1, stderr
            assert stderr.rfind('PUBLISHED kafel-weather:') > stderr.rfind('weather refresh failed:'), stderr
            print('PASS daemon DASH/5 heartbeat during source failure, retry delay and SIGTERM', flush=True)
        finally:
            if process.poll() is None: process.kill(); process.communicate()
    environment.pop('KAFEL_WEATHER_LATITUDE')
    assert subprocess.run([sys.argv[2]], env=environment, capture_output=True, timeout=3).returncode == 2
    result = subprocess.run([sys.argv[2], '--version'], env=environment, capture_output=True, timeout=3)
    assert result.returncode == 0 and b'DASH/5' in result.stdout
    assert re.search(rb' git=(?:[0-9a-f]{40}(?:-dirty)?|unknown)\n$', result.stdout)
    print('PASS daemon configuration and version', flush=True)
