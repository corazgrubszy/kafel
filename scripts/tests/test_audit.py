"""Current Kafel autostart links must never be labelled legacy artifacts."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('kafel_audit',Path(__file__).resolve().parents[1]/'audit.py')
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)

class AuditTests(unittest.TestCase):
    def test_exact_legacy_names_only(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for name in audit.EXPECTED+audit.LEGACY:
                path=root/name;path.symlink_to('/etc/systemd/system/'+name)
                self.assertEqual(audit.legacy_unit_link(path),name in audit.LEGACY)

if __name__=='__main__':unittest.main()
