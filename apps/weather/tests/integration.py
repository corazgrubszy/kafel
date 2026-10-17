"""Exercise the real HttpClient -> Open-Meteo decoder -> DASH/4 path offline."""
import http.server
import subprocess
import sys
import threading

FORECAST = b'{"current_weather":{"temperature":12.8,"windspeed":18,"time":1800000900},"hourly":{"time":[1800000000,1800003600],"rain":[0,1],"snowfall":[0,0]}}'
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
            assert result.stdout == 'DASH/4\tkafel-weather\tKafel Weather\nTILE\tweather\t2\tok\tnormal\tPogoda\tmetric-meter\t13\t°C\tSucho\tWiatr: 5 m/s\t1529\n'
        print('PASS weather HTTP integration', path, flush=True)
finally:
    server.shutdown(); server.server_close(); thread.join(2)

# Exercise the real daemon without external networking: a missing CA fails
# before DNS. Its heartbeat must continue while the source is in retry.
import os
import pathlib
import socket
import tempfile

with tempfile.TemporaryDirectory(prefix='kafel-weather-daemon-') as directory:
    inbox = pathlib.Path(directory)/'status.sock'
    environment = dict(os.environ, KAFEL_WEATHER_LATITUDE='52', KAFEL_WEATHER_LONGITUDE='17',
                       KAFEL_WEATHER_CA_BUNDLE=str(pathlib.Path(directory)/'missing.pem'),
                       KAFEL_DASHBOARD_SOCKET=str(inbox))
    # macOS Unix socket paths are short; use a private /tmp directory below.
    with tempfile.TemporaryDirectory(prefix='kw-', dir='/tmp') as short_directory:
        inbox = pathlib.Path(short_directory)/'status.sock'
        environment['KAFEL_DASHBOARD_SOCKET'] = str(inbox)
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as receiver:
            receiver.bind(str(inbox)); receiver.settimeout(8)
            process = subprocess.Popen([sys.argv[2]], env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                expected = 'DASH/4\tkafel-weather\tKafel Weather\nTILE\tweather\t2\terror\tnormal\tPogoda\tmetric\t\t°C\tBrak danych\n'
                assert receiver.recv(8192).decode() == expected
                assert receiver.recv(8192).decode() == expected
                process.terminate()
                stdout, stderr = process.communicate(timeout=3)
                assert process.returncode == 0, (stdout, stderr)
                assert stderr.count('weather refresh failed:') == 1, stderr
                print('PASS daemon heartbeat during source failure, retry delay and SIGTERM', flush=True)
            finally:
                if process.poll() is None:
                    process.kill(); process.communicate()
    environment.pop('KAFEL_WEATHER_LATITUDE')
    result = subprocess.run([sys.argv[2]], env=environment, capture_output=True, timeout=3)
    assert result.returncode == 2
    result = subprocess.run([sys.argv[2], '--version'], env=environment, capture_output=True, timeout=3)
    assert result.returncode == 0 and b'DASH/4' in result.stdout
    print('PASS daemon configuration and version', flush=True)
