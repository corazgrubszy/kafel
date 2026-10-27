"""Run on CM4: verify first, then measure fresh processes against Open-Meteo."""
import hashlib
import pathlib
import platform
import ssl
import subprocess
import sys
binary = pathlib.Path('http-client-probe').resolve()
print('platform:', platform.platform(), 'test server:', ssl.OPENSSL_VERSION, flush=True)
print('binary_bytes:', binary.stat().st_size, 'sha256:', hashlib.sha256(binary.read_bytes()).hexdigest(), flush=True)
subprocess.run([sys.executable, 'integration.py', str(binary), 'fixtures'], check=True)
url = 'https://api.open-meteo.com/v1/forecast?latitude=-33.8568&longitude=151.2153&current_weather=true'
for sample in range(3):
    print('OPEN_METEO sample=', sample+1, flush=True)
    result = subprocess.run([str(binary), url, '/etc/ssl/certs/ca-certificates.crt', '10000', '65536'], capture_output=True, text=True, timeout=15)
    print(result.stdout, end='', flush=True)
    if result.returncode or 'HTTP status=200 ' not in result.stdout or '"current_weather"' not in result.stdout:
        raise SystemExit('Open-Meteo request failed')
print('CM4 MEASUREMENTS COMPLETE', flush=True)
