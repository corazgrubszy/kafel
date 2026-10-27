"""Public build names and interpreter-free service startup."""
from pathlib import Path
import re
import unittest
ROOT=Path(__file__).resolve().parents[2]
class BuildCommandsTests(unittest.TestCase):
    def test_public_steps(self):
        source=(ROOT/'build.zig').read_text()
        steps=set(re.findall(r'b.step\("([^"\n]+)"',source))
        for name in ('dashboard','weather','waste','weather-native','waste-native'):
            self.assertIn(name,steps)
        for name in ('linux','weather-linux','waste-linux','weather-card-linux'):
            self.assertNotIn(name,steps)
    def test_service_runs_native_helper(self):
        unit=(ROOT/'packaging/systemd/kafel-dashboard.service.in').read_text()
        self.assertIn('ExecStartPre=+/opt/kafel/libexec/kafel-display-console isolate',unit)
        self.assertNotIn('python',unit)
        self.assertFalse((ROOT/'scripts/display_console.py').exists())
