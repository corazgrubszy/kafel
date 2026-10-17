"""Deterministic loopback tests. Certificates are disposable development fixtures."""
import contextlib
import http.server
import pathlib
import socketserver
import ssl
import subprocess
import sys
import threading
import time

binary = pathlib.Path(sys.argv[1]).resolve()
fixtures = pathlib.Path(sys.argv[2]).resolve()

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_GET(self):
        try:
            if self.path == '/slow': time.sleep(0.8)
            if self.path == '/oversize':
                self.send_response(200); self.send_header('Content-Length', '1000000'); self.end_headers(); return
            status = 503 if self.path == '/status' else 302 if self.path == '/redirect' else 200
            self.send_response(status)
            if self.path == '/redirect': self.send_header('Location', '/status')
            body = self.path.encode() if self.path.startswith('/query') else b'12345678'
            if self.path == '/chunked': self.send_header('Transfer-Encoding', 'chunked')
            elif self.path not in ('/eof', '/drip'): self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            if self.path == '/chunked': self.wfile.write(b'8\r\n12345678\r\n0\r\n\r\n')
            elif self.path == '/drip':
                for _ in range(20):
                    self.wfile.write(b'x'); self.wfile.flush(); time.sleep(0.08)
            else: self.wfile.write(body)
        except (OSError, ssl.SSLError): pass

class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    def handle_error(self, *args): pass

@contextlib.contextmanager
def server(label='valid', tls=True):
    instance = Server(('127.0.0.1', 0), Handler)
    if tls:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = context.maximum_version = (ssl.TLSVersion.TLSv1_2 if label == 'valid-tls12' else ssl.TLSVersion.TLSv1_3)
        context.load_cert_chain(str(fixtures/(label+'.pem')), str(fixtures/(label+'.key')))
        instance.socket = context.wrap_socket(instance.socket, server_side=True)
    thread = threading.Thread(target=instance.serve_forever, daemon=True); thread.start()
    try: yield f'{"https" if tls else "http"}://localhost:{instance.server_port}'
    finally:
        instance.shutdown(); instance.server_close(); thread.join(2)

count = 0
def check(name, url, expected=None, timeout=2000, limit=64, ca=None, contains=None):
    global count
    start = time.monotonic()
    result = subprocess.run([str(binary), url, str(ca or fixtures/'valid.ca.pem'), str(timeout), str(limit)], capture_output=True, text=True, timeout=6)
    elapsed = time.monotonic()-start
    ok = result.returncode == (1 if expected else 0)
    if expected: ok = ok and f'ERROR code={expected} ' in result.stdout
    if contains: ok = ok and contains in result.stdout
    if expected == 'timeout': ok = ok and elapsed < 1.5
    if not ok: raise AssertionError(f'{name}: elapsed={elapsed}\n{result.stdout}\n{result.stderr}')
    count += 1
    print('PASS', name, flush=True)

for label in ('valid','valid-rsa','valid-intermediate','valid-rsa-sha512','valid-p384','valid-tls12', 'expired','future','wrong-host','untrusted','expired-rsa','wrong-host-rsa'):
    with server(label) as origin:
        check('certificate '+label, origin+'/', None if label.startswith('valid') else 'tls', ca=fixtures/(label+'.ca.pem'))

for tls in (False, True):
    with server(tls=tls) as origin:
        check('exact limit', origin+'/', limit=8, contains='body_bytes=8')
        for path in ('/', '/oversize', '/chunked', '/eof'):
            check('body limit '+path, origin+path, 'response_too_large', limit=7)
        for path in ('/slow', '/drip'):
            check('deadline '+path, origin+path, 'timeout', timeout=200)
        check('HTTP status preserved', origin+'/status', contains='HTTP status=503')
        check('redirect not followed', origin+'/redirect', contains='HTTP status=302')
        check('query preserved', origin+'/query?a=one%2Ctwo#ignored', contains='BODY /query?a=one%2Ctwo')

class Stall(socketserver.BaseRequestHandler):
    def handle(self): time.sleep(0.8)
class StallServer(socketserver.ThreadingTCPServer):
    daemon_threads = True
with StallServer(('127.0.0.1',0), Stall) as stall:
    thread = threading.Thread(target=stall.serve_forever,daemon=True); thread.start()
    try: check('TLS handshake deadline', f'https://localhost:{stall.server_address[1]}/', 'timeout', timeout=200)
    finally: stall.shutdown(); thread.join(2)
for url in ('', 'ftp://localhost/', 'http://user:secret@localhost/', 'http://localhost:99999/', 'http://localhost/\n'):
    check('invalid argument', url, 'invalid_argument')
check('zero timeout', 'http://localhost/', 'invalid_argument', timeout=0)
check('zero limit', 'http://localhost/', 'invalid_argument', limit=0)
check('missing CA', 'https://localhost/', 'configuration', ca=fixtures/'absent.pem')
print(f'ALL {count} TESTS PASSED', flush=True)
