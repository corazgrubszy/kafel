import pathlib
import subprocess
import sys
import tempfile
root = pathlib.Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='kafel-http-tests-') as temporary:
    subprocess.run([sys.executable, str(root/'make_certificates.py'), temporary], check=True)
    subprocess.run([sys.executable, str(root/'integration.py'), sys.argv[1], temporary], check=True)
