"""Current Kafel autostart links must never be labelled legacy artifacts."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('kafel_audit',Path(__file__).resolve().parents[1]/'audit.py')
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)

class AuditTests(unittest.TestCase):
    def test_exact_legacy_names_only(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for name in audit.EXPECTED+audit.LEGACY:
                path=root/name;path.symlink_to('/etc/systemd/system/'+name)
                self.assertEqual(audit.legacy_unit_link(path),name in audit.LEGACY)

class IdentityTests(unittest.TestCase):
    def test_version_revision_and_dirty_marker(self):
        for suffix in ('','-dirty'):
            revision='a'*40+suffix
            version='kafel-weather 0.5.0-dev DASH/5 git='+revision
            with patch.object(audit,'command',return_value=(0,version,'')):
                result=audit.binary_identity('/example/binary')
            self.assertEqual(result,{'version':version,'git_revision':revision})
    def test_archive_and_pre_identity_binaries_are_explicit(self):
        for version,expected in [('kafel-waste 0.5.0-dev DASH/5 git=unknown','unknown'),('kafel-dashboard 0.3.0-dev',None)]:
            with patch.object(audit,'command',return_value=(0,version,'')):
                self.assertEqual(audit.binary_identity('/binary')['git_revision'],expected)
    def test_failed_version_is_not_accepted(self):
        with patch.object(audit,'command',return_value=(1,'','cannot execute')):
            with self.assertRaises(RuntimeError):audit.binary_identity('/binary')

if __name__=='__main__':unittest.main()
