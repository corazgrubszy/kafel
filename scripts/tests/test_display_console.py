"""Console policy/rollback and boot edits with fake devices; never touch host VT."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('display_console',Path(__file__).resolve().parents[1]/'display_console.py')
display=importlib.util.module_from_spec(spec);spec.loader.exec_module(display)

class Console:
    def __init__(self,target=None,fail=0):
        self.lcd='/dev/fb7';self.target=target;self.bound={'vtcon1':1};self.mapping=[7]*display.MAX_NR_CONSOLES
        self.calls=0;self.fail=fail
    def hdmi(self):return self.target
    def bindings(self):return dict(self.bound)
    def bind(self,name,value):self.bound[name]=value
    def maps(self):return list(self.mapping)
    def map(self,tty,fb):
        self.calls+=1
        if self.calls==self.fail:raise OSError('injected map error')
        self.mapping[tty-1]=fb

class ConsoleTests(unittest.TestCase):
    def test_boot_preserves_disk_serial_and_other_options(self):
        source='console=ttyS0,115200 root=PARTUUID=EXAMPLE-02 rootfstype=ext4 rootwait console=tty1 fbcon=map cfg80211.ieee80211_regdom=PL\n'
        result=display.boot_configuration(source)
        for token in ('root=PARTUUID=EXAMPLE-02','console=ttyS0,115200','rootfstype=ext4','rootwait','cfg80211.ieee80211_regdom=PL'):
            self.assertIn(token,result.split())
        self.assertIn('console=tty2',result.split());self.assertNotIn('fbcon=map',result)
        self.assertEqual(result.count(display.HDMI_MODE),1)
        self.assertEqual(display.boot_configuration(result),result)
        self.assertEqual(len(result.strip().splitlines()),1)

    def test_ambiguous_boot_settings_rejected(self):
        for source in ('root=a\nroot=b','root=a root=b','console=tty1','root=a video=HDMI-A-1:1920x1080@60D','root=a\x00'):
            with self.assertRaises(ValueError):display.boot_configuration(source)

    def test_headless_detaches_without_changing_maps(self):
        console=Console();before=console.maps()
        result=display.isolate(console)
        self.assertEqual(result['mode'],'detached');self.assertEqual(console.bound,{'vtcon1':0})
        self.assertEqual(console.maps(),before)
        self.assertEqual(display.isolate(console)['mode'],'detached')

    def test_hdmi_maps_every_valid_vt_and_never_uses_lcd(self):
        console=Console(1)
        result=display.isolate(console)
        self.assertEqual(result['mode'],'hdmi');self.assertEqual(console.mapping,[1]*63)
        self.assertEqual(console.bound,{'vtcon1':1})
        console=Console(7)
        with self.assertRaises(RuntimeError):display.isolate(console)
        self.assertEqual(console.mapping,[7]*63)

    def test_partial_mapping_failure_restores_prior_state(self):
        console=Console(1,fail=4)
        with self.assertRaisesRegex(OSError,'injected'):display.isolate(console)
        self.assertEqual(console.mapping,[7]*63)
        self.assertEqual(console.bound,{'vtcon1':1})

    def test_unmapped_consoles_fall_back_to_detach(self):
        console=Console(1);console.mapping[62]=0xffffffff
        result=display.isolate(console)
        self.assertEqual(result['mode'],'detached');self.assertEqual(console.calls,0)

    def test_failed_detach_prevents_start_and_reports_restore_error(self):
        console=Console()
        def fail(name,value):raise OSError('unbind forbidden')
        console.bind=fail
        with self.assertRaisesRegex(RuntimeError,'restore failed'):display.isolate(console)

    def test_explicit_restore(self):
        console=Console(1);before=display.snapshot(console)
        display.isolate(console);display.restore(console,before)
        self.assertEqual(console.mapping,[7]*63);self.assertEqual(console.bound,{'vtcon1':1})

    def test_auto_hdmi_requires_forced_mode_and_matching_drm_device(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            (root/'proc').mkdir();(root/'proc/cmdline').write_text(display.HDMI_MODE)
            fb=root/'sys/class/graphics/fb1';fb.mkdir(parents=True);(fb/'name').write_text('vc4drmfb')
            device=root/'devices/gpu';device.mkdir(parents=True)
            (fb/'device').symlink_to(device)
            card=root/'sys/class/drm/card0';card.mkdir(parents=True);(card/'device').symlink_to(device)
            (card.parent/'card0-HDMI-A-1').mkdir()
            console=object.__new__(display.LinuxConsole);console.root=root;console.lcd='/dev/fb7'
            self.assertEqual(console.hdmi(),1)
            (root/'proc/cmdline').write_text('console=tty2')
            self.assertIsNone(console.hdmi())
            (root/'proc/cmdline').write_text(display.HDMI_MODE)
            (fb/'name').write_text('unknown')
            self.assertIsNone(console.hdmi())
