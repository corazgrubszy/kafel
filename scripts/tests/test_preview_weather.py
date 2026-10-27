"""Temporary preview must restore dashboard, including launch/render failures."""
import contextlib
import io
import hashlib
import importlib.util
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import subprocess

spec=importlib.util.spec_from_file_location('preview_weather',Path(__file__).resolve().parents[1]/'preview_weather.py')
preview=importlib.util.module_from_spec(spec);spec.loader.exec_module(preview)

class PreviewTests(unittest.TestCase):
    def exercise(self,failure=None):
        temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup)
        stage=Path(temp.name);(stage/'weather-card-preview').write_bytes(b'preview')
        calls=[]
        def run(*args):
            calls.append(args)
            if failure=='preflight' and args[-1]=='--check':raise RuntimeError('bad fonts')
            if args[0]=='systemd-run' and failure=='launch':raise subprocess.CalledProcessError(1,args)
            if args[:2]==('systemctl','show'):return '123' if 'weather' in args[2] else '456'
            return ''
        with patch('os.chown'),patch('pwd.getpwnam',return_value=SimpleNamespace(pw_uid=0)),contextlib.redirect_stdout(io.StringIO()):
            try:
                preview.show(stage,'operator',hashlib.sha256(b'preview').hexdigest(),run)
                error=None
            except Exception as caught:error=caught
        return stage,calls,error
    def test_success_restores_dashboard_and_does_not_mutate_producers(self):
        stage,calls,error=self.exercise();self.assertIsNone(error,error)
        mutations=[call for call in calls if call[:2] in (('systemctl','stop'),('systemctl','start'))]
        self.assertEqual(mutations,[('systemctl','stop','kafel-dashboard.service'),('systemctl','start','kafel-dashboard.service')])
        self.assertTrue((stage/'preview.json').is_file())
        launch=next(call for call in calls if call[0]=='systemd-run')
        self.assertIn('--property=RuntimeMaxSec=135',launch)
        self.assertIn('--property=ExecStopPost=/usr/bin/systemctl --no-block start kafel-dashboard.service',launch)
    def test_launch_failure_restores_dashboard(self):
        _,calls,error=self.exercise('launch');self.assertIsNotNone(error)
        self.assertEqual(calls[-1],('systemctl','start','kafel-dashboard.service'))
    def test_failed_preflight_does_not_stop_dashboard(self):
        _,calls,error=self.exercise('preflight');self.assertIsNotNone(error)
        self.assertFalse(any(call[:2]==('systemctl','stop') for call in calls))
