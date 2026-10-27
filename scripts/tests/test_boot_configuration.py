"""Boot cmdline edits belong to the administrative installer, not service startup."""
import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('install_boot',Path(__file__).resolve().parents[1]/'install.py')
installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)
class BootTests(unittest.TestCase):
    def test_boot_preserves_disk_serial_and_other_options(self):
        source='console=ttyS0,115200 root=PARTUUID=EXAMPLE-02 rootfstype=ext4 rootwait console=tty1 fbcon=map cfg80211.ieee80211_regdom=PL\n'
        result=installer.boot_configuration(source)
        for token in ('root=PARTUUID=EXAMPLE-02','console=ttyS0,115200','rootfstype=ext4','rootwait','cfg80211.ieee80211_regdom=PL'):
            self.assertIn(token,result.split())
        self.assertIn('console=tty2',result.split());self.assertNotIn('fbcon=map',result)
        self.assertEqual(result.count(installer.HDMI_MODE),1)
        self.assertEqual(installer.boot_configuration(result),result)
        self.assertEqual(len(result.strip().splitlines()),1)

    def test_ambiguous_boot_settings_rejected(self):
        for source in ('root=a\nroot=b','root=a root=b','console=tty1','root=a video=HDMI-A-1:1920x1080@60D','root=a\x00'):
            with self.assertRaises(ValueError):installer.boot_configuration(source)

