"""Execute the real transaction in an isolated filesystem with fake systemd."""
import hashlib
import contextlib
import io
import importlib.util
import json
import os
from pathlib import Path
import pwd
import subprocess
import tarfile
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('kafel_install',Path(__file__).resolve().parents[1]/'install.py')
installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)
ROOT=Path(__file__).resolve().parents[2]

def helper_fixture():
    header=bytearray(64);header[:6]=b'\x7fELF\x02\x01';header[18:20]=b'\xb7\x00'
    return bytes(header)+b'native-helper'

class Machine:
    def __init__(self,root,stage,failure=''):
        self.root,self.stage,self.failure=root,stage,failure
        self.calls=[]
        self.units={unit:{'active':True,'enabled':True,'loaded':True,'pid':'123' if unit=='dashboard.service' else '124'} for unit in installer.OLD}
        self.units.update({unit:{'active':False,'enabled':False,'loaded':False,'pid':'0'} for unit in installer.NEW})
    def __call__(self,*args,check=True,**kwargs):
        self.calls.append(args)
        out='';result=0
        if args[0]=='systemctl':
            action=args[1]
            if action=='show':
                unit,key=args[2],args[4];state=self.units[unit]
                if key=='LoadState':out='loaded' if state['loaded'] else 'not-found'
                elif key=='MainPID':out=state['pid'] if state['active'] else '0'
                elif key=='User':out='operator'
                elif key=='DropInPaths':
                    candidate=self.root/'etc/systemd/system'/(unit+'.d')/'override.conf'
                    if candidate.exists():out='/etc/systemd/system/'+unit+'.d/override.conf'
                elif key=='Environment':out='INFOHUB_LATITUDE=52.0 INFOHUB_LONGITUDE=17.0'
                elif key=='FragmentPath':out='/etc/systemd/system/'+unit
                elif key=='ExecStart':
                    name=unit.removesuffix('.service')
                    path='/home/operator/'+name+'/'+name if unit in installer.OLD else '/opt/kafel/current/bin/'+name
                    out='{ path='+path+' ; argv[]='+path+' ; }'
            elif action in ('is-active','is-enabled'):
                unit=args[-1];result=0 if self.units[unit]['active' if action=='is-active' else 'enabled'] else 1
            elif action in ('stop','start','enable','disable'):
                for unit in args[2:]:
                    state=self.units[unit]
                    if action=='start' and unit in installer.NEW and self.failure=='start':raise RuntimeError('injected start failure')
                    if action in ('stop','start'):
                        state['active']=action=='start'
                        if action=='start':state['loaded']=True;state['pid']='333' if unit==installer.NEW[0] else '334'
                    else:state['enabled']=action=='enable'
            elif action=='daemon-reload':
                for unit in installer.NEW+installer.OLD:
                    self.units[unit]['loaded']=(self.root/'etc/systemd/system'/unit).exists()
        elif args[0]=='journalctl':
            unit=args[args.index('-u')+1]
            if self.failure=='ready':out='no marker'
            else:out='READY '+unit.removesuffix('.service')+': OK\nservices: registered kafel-weather\nservices: registered kafel-waste\nwaste schedule loaded: 1 pickup days\nweather cache updated\nPUBLISHED kafel-weather: DASH/5 snapshot accepted\nPUBLISHED kafel-waste: DASH/5 snapshot accepted'
            if self.failure=='publication':
                out='\n'.join(line for line in out.splitlines() if not line.startswith('PUBLISHED '))+'\ndashboard publish unavailable: invalid_template'
        elif args[0]=='runuser':
            if self.failure=='schedule':raise RuntimeError('schedule validation failure')
            return self(*args[4:],check=check)
        elif args[0]=='env':
            position=next(i for i,x in enumerate(args[1:],1) if x.startswith(str(self.stage)))
            return self(*args[position:],check=check)
        elif args[0].startswith(str(self.stage)) or args[0].startswith(str(self.root/'opt/kafel/current/bin')):
            if args[-1]=='--version':out=Path(args[0]).name+' 0.5.0-dev DASH/5'
            elif args[-1]=='--check-input' and self.failure=='touch':raise RuntimeError('touch failure')
            elif args[-1]=='--check' and self.failure=='framebuffer':raise RuntimeError('framebuffer failure')
        else:raise AssertionError(args)
        if check and result:raise RuntimeError('command failed '+str(args))
        return subprocess.CompletedProcess(args,result,out,'')

class InstallTests(unittest.TestCase):
    def exercise(self,failure='',existing=False):
        temporary=tempfile.TemporaryDirectory(prefix='kafel-install-test-');self.addCleanup(temporary.cleanup)
        root=Path(temporary.name);stage=root/'tmp/kafel-deploy.ABCDEFGH';stage.mkdir(parents=True)
        for name in installer.NAMES:
            (stage/name).write_bytes(b'new-'+name.encode())
            (stage/(name+'.service.in')).write_bytes((ROOT/'packaging/systemd'/(name+'.service.in')).read_bytes())
        (stage/'waste_data.py').write_bytes((ROOT/'scripts/waste_data.py').read_bytes())
        (stage/'kafel-display-console').write_bytes(helper_fixture())
        manifest={'waste_data_sha256':hashlib.sha256((stage/'waste_data.py').read_bytes()).hexdigest(),'display_helper_sha256':hashlib.sha256((stage/'kafel-display-console').read_bytes()).hexdigest(),'binaries':{name:hashlib.sha256((stage/name).read_bytes()).hexdigest() for name in installer.NAMES}}
        if failure=='checksum':manifest['binaries'][installer.NAMES[0]]='0'*64
        (stage/'manifest.json').write_text(json.dumps(manifest))
        for unit in installer.OLD:
            path=root/'etc/systemd/system'/unit;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'old-unit-'+unit.encode())
            name=unit.removesuffix('.service');binary=root/'home/operator'/name/name;binary.parent.mkdir(parents=True);binary.write_bytes(b'old-'+name.encode())
        schedule=root/'home/operator/infohub/data/garbage_schedule.json'
        schedule.parent.mkdir();schedule.write_text('{"timezone":"UTC","pickups":[]}')
        override=root/'etc/systemd/system/infohub.service.d/override.conf'
        override.parent.mkdir();override.write_text('[Service]\nEnvironment=INFOHUB_LATITUDE=52\n')
        prototype=root/'tmp/infohub-mongoose.ABCDEFGH';prototype.mkdir();(prototype/'mongoose-probe').write_bytes(b'temporary')
        elf=bytearray(64);elf[:6]=b'\x7fELF\x02\x01';elf[18:20]=b'\xb7\x00'
        (root/'tmp/http-client-probe').write_bytes(elf)
        (root/'tmp/mongoose-probe').write_bytes(b'not an ARM64 executable')
        unrelated=root/'tmp/keep-me';unrelated.mkdir();(unrelated/'data').write_bytes(b'keep')
        machine=Machine(root,stage,failure)
        if existing:
            release=root/'opt/kafel/releases'/('a'*24)/'bin';release.mkdir(parents=True)
            previous_names=installer.NAMES[:2] if existing=='two' else installer.NAMES
            for name in previous_names:(release/name).write_bytes(b'previous-'+name.encode())
            (root/'opt/kafel/current').symlink_to('releases/'+'a'*24)
            previous_units=installer.NEW[:2] if existing=='two' else installer.NEW
            for unit in previous_units:
                path=root/'etc/systemd/system'/unit;path.write_bytes(b'previous-unit-'+unit.encode())
                machine.units[unit].update(active=True,enabled=True,loaded=True,pid='200')
        if existing=='two':
            for unit in installer.OLD:
                (root/'etc/systemd/system'/unit).unlink()
                machine.units[unit].update(active=False,enabled=False,loaded=False,pid='0')
            override.unlink();override.parent.rmdir()
            environment=root/'etc/kafel/weather.env';environment.parent.mkdir()
            environment.write_text('KAFEL_WEATHER_LATITUDE=52\nKAFEL_WEATHER_LONGITUDE=17\n')
        job=installer.Installer(stage,'operator',root,machine)
        if failure=='cleanup':
            original=job.cleanup
            def fail_cleanup(release):
                original(release)
                raise RuntimeError('injected cleanup failure')
            job.cleanup=fail_cleanup
        account=SimpleNamespace(pw_dir='/home/operator',pw_uid=os.getuid(),pw_gid=os.getgid())
        with patch.object(installer.pwd,'getpwnam',return_value=account),patch.object(installer.os,'chown'),patch.object(installer.time,'sleep'),contextlib.redirect_stdout(io.StringIO()):
            try:job.install();error=None
            except Exception as caught:error=caught
        return root,stage,machine,error

    def test_migration_enables_new_removes_old_and_cleans_only_known_tests(self):
        root,stage,machine,error=self.exercise()
        self.assertIsNone(error,error)
        for unit in installer.NEW:self.assertTrue(machine.units[unit]['active'] and machine.units[unit]['enabled'])
        for unit in installer.OLD:
            self.assertFalse(machine.units[unit]['active'] or machine.units[unit]['enabled'])
            self.assertFalse((root/'etc/systemd/system'/unit).exists())
        self.assertFalse((root/'home/operator/dashboard/dashboard').exists())
        self.assertFalse((root/'home/operator/infohub/infohub').exists())
        self.assertFalse((root/'etc/systemd/system/infohub.service.d').exists())
        self.assertEqual((root/'home/operator/infohub/data/garbage_schedule.json').read_text(),'{"timezone":"UTC","pickups":[]}')
        self.assertNotIn('KAFEL_WASTE_SPAN=',(root/'etc/kafel/waste.env').read_text())
        self.assertFalse((root/'tmp/infohub-mongoose.ABCDEFGH').exists())
        self.assertTrue((root/'tmp/keep-me/data').exists())
        self.assertFalse((root/'tmp/http-client-probe').exists())
        self.assertTrue((root/'tmp/mongoose-probe').exists())
        self.assertTrue((root/'opt/kafel/current/bin/kafel-dashboard').is_file())
        self.assertEqual((root/'etc/kafel/weather.env').stat().st_mode&0o777,0o600)
        with tarfile.open(stage/'rollback.tar.gz') as archive:
            metadata=json.load(archive.extractfile('rollback.json'))
            self.assertTrue(any(item['path'].endswith('/dashboard/dashboard') for item in metadata['files']))

    def test_preflight_failures_leave_existing_units_and_binaries(self):
        for failure in ('checksum','touch','schedule'):
            with self.subTest(failure=failure):
                root,stage,machine,error=self.exercise(failure)
                self.assertIsNotNone(error)
                self.assertFalse(any(call[:2]==('systemctl','stop') for call in machine.calls))
                self.assertTrue(machine.units['dashboard.service']['active'])
                self.assertFalse((root/'opt/kafel/current').exists())

    def test_framebuffer_start_and_ready_failures_restore_services(self):
        for failure in ('framebuffer','start','ready','cleanup'):
            with self.subTest(failure=failure):
                root,stage,machine,error=self.exercise(failure)
                self.assertIsNotNone(error)
                for unit in installer.OLD:self.assertTrue(machine.units[unit]['active'] and machine.units[unit]['enabled'])
                for unit in installer.NEW:
                    self.assertFalse(machine.units[unit]['active'] or machine.units[unit]['enabled'])
                    self.assertFalse((root/'etc/systemd/system'/unit).exists())
                self.assertEqual((root/'home/operator/dashboard/dashboard').read_bytes(),b'old-dashboard')
                self.assertEqual((root/'home/operator/infohub/infohub').read_bytes(),b'old-infohub')
                self.assertTrue((root/'etc/systemd/system/infohub.service.d/override.conf').is_file())

    def test_update_failure_restores_previous_release_even_after_cleanup(self):
        root,stage,machine,error=self.exercise('cleanup',existing=True)
        self.assertIsNotNone(error)
        self.assertEqual(os.readlink(root/'opt/kafel/current'),'releases/'+'a'*24)
        self.assertEqual((root/'opt/kafel/current/bin/kafel-weather').read_bytes(),b'previous-kafel-weather')
        self.assertTrue(machine.units['kafel-dashboard.service']['active'])

    def test_upgrade_from_two_apps_adds_waste_without_touching_schedule(self):
        root,stage,machine,error=self.exercise(existing='two')
        self.assertIsNone(error,error)
        self.assertTrue(machine.units['kafel-waste.service']['active'])
        self.assertTrue((root/'opt/kafel/current/bin/kafel-waste').is_file())
        self.assertEqual((root/'home/operator/infohub/data/garbage_schedule.json').read_text(),'{"timezone":"UTC","pickups":[]}')
        self.assertEqual(len(list((root/'opt/kafel/releases').iterdir())),1)

    def test_failed_two_to_three_upgrade_restores_exact_previous_set(self):
        root,stage,machine,error=self.exercise('cleanup',existing='two')
        self.assertIsNotNone(error)
        self.assertTrue(machine.units['kafel-dashboard.service']['active'])
        self.assertTrue(machine.units['kafel-weather.service']['active'])
        self.assertFalse(machine.units['kafel-waste.service']['active'])
        self.assertFalse((root/'opt/kafel/current/bin/kafel-waste').exists())
        self.assertFalse((root/'etc/systemd/system/kafel-waste.service').exists())
        self.assertTrue((root/'home/operator/infohub/data/garbage_schedule.json').is_file())

    def test_config_is_numeric_not_shell(self):
        self.assertEqual(installer.coordinate('52.1',90),'52.1')
        for value in ('nan','inf','91','52; rm -rf /',None):
            with self.assertRaises(ValueError):installer.coordinate(value,90)

class IncrementalTests(InstallTests):
    def update_existing(self, selected, changed=(), failure='', config=None, boot=False, helper_changed=False, old_helper=False, legacy_schedule=False, conflicting_schedule=False):
        root,stage,machine,error = self.exercise()
        self.assertIsNone(error, error)
        if legacy_schedule:
            schedule=root/'var/lib/kafel-waste/garbage_schedule.json'
            schedule.unlink()
            (root/'etc/kafel/waste.env').write_text('KAFEL_WASTE_SCHEDULE="/home/operator/infohub/data/garbage_schedule.json"\nKAFEL_WASTE_SPAN=1\n')
            if conflicting_schedule:schedule.write_text('{"timezone":"Europe/Warsaw","pickups":[]}')
        if boot:
            path=root/'boot/firmware/cmdline.txt';path.parent.mkdir(parents=True)
            path.write_text('root=PARTUUID=EXAMPLE rootwait console=tty2 fbcon=map\n')
            (root/'sys/class/drm/card0-HDMI-A-1').mkdir(parents=True)
            (root/'proc').mkdir(exist_ok=True);(root/'proc/cmdline').write_bytes(path.read_bytes())
        if old_helper:
            (root/'opt/kafel/libexec/display_console.py').write_bytes(b'old-python-helper')
            unit=root/'etc/systemd/system/kafel-dashboard.service'
            unit.write_text(unit.read_text().replace('+/opt/kafel/libexec/kafel-display-console isolate','+/usr/bin/python3 /opt/kafel/libexec/display_console.py isolate'))
        if helper_changed:
            with (stage/'kafel-display-console').open('ab') as stream:stream.write(b'upgraded helper')
        before = {name:(root/'opt/kafel/current/bin'/name).read_bytes() for name in installer.NAMES}
        previous = os.readlink(root/'opt/kafel/current')
        for name in set(installer.NAMES)-set(selected):
            (stage/name).unlink()
            (stage/(name+'.service.in')).unlink()
        for name in selected:
            (stage/name).write_bytes(before[name]+(b'-update' if name in changed else b''))
        manifest = {'waste_data_sha256':hashlib.sha256((stage/'waste_data.py').read_bytes()).hexdigest(),'display_helper_sha256':hashlib.sha256((stage/'kafel-display-console').read_bytes()).hexdigest(),'dash_protocol':5,'selected':list(selected), 'binaries':{n:hashlib.sha256((stage/n).read_bytes()).hexdigest() for n in selected}, **(config or {})}
        (stage/'manifest.json').write_text(json.dumps(manifest))
        machine.calls.clear();machine.failure=failure
        job=installer.Installer(stage,'operator',root,machine)
        account=SimpleNamespace(pw_dir='/home/operator',pw_uid=os.getuid(),pw_gid=os.getgid())
        with patch.object(installer.pwd,'getpwnam',return_value=account),patch.object(installer.os,'chown'),patch.object(installer.time,'sleep'),contextlib.redirect_stdout(io.StringIO()):
            try:job.install();error=None
            except Exception as caught:error=caught
        return root,stage,machine,error,before,previous

    def mutations(self, machine):
        return [args for args in machine.calls if args[0]=='systemctl' and args[1] in ('stop','start','enable','disable')]

    def test_each_application_is_independent(self):
        for selected in installer.NAMES:
            with self.subTest(selected=selected):
                root,stage,machine,error,before,_=self.update_existing([selected],[selected])
                self.assertIsNone(error,error)
                for call in self.mutations(machine): self.assertEqual(call[2:],(selected+'.service',))
                for name in installer.NAMES:
                    self.assertEqual((root/'opt/kafel/current/bin'/name).read_bytes(),before[name]+(b'-update' if name==selected else b''))
                self.assertEqual(json.loads((stage/'report.json').read_text())['changed'],[selected])
                self.assertEqual(len(list((root/'opt/kafel/releases').iterdir())),1)

    def test_failed_publication_rolls_back_only_selected_producer(self):
        import itertools
        for name in ('kafel-weather','kafel-waste'):
            with self.subTest(name=name),patch.object(installer.time,'monotonic',side_effect=itertools.count()):
                root,_,machine,error,before,previous=self.update_existing([name],[name],failure='publication')
            self.assertIsNotNone(error)
            self.assertIn('acceptance missing',str(error))
            self.assertEqual(os.readlink(root/'opt/kafel/current'),previous)
            for binary,data in before.items():self.assertEqual((root/'opt/kafel/current/bin'/binary).read_bytes(),data)
            for call in self.mutations(machine):self.assertEqual(call[2:],(name+'.service',))

    def test_no_change_never_stops_or_switches(self):
        root,stage,machine,error,_,previous=self.update_existing(installer.NAMES)
        self.assertIsNone(error,error)
        self.assertEqual(self.mutations(machine),[])
        self.assertEqual(os.readlink(root/'opt/kafel/current'),previous)
        report=json.loads((stage/'report.json').read_text())
        self.assertEqual(report['changed'],[])
        self.assertEqual(report['unchanged'],list(installer.NAMES))

    def test_all_selection_restarts_only_changed_application(self):
        _,_,machine,error,_,_=self.update_existing(installer.NAMES,['kafel-weather'])
        self.assertIsNone(error,error)
        for call in self.mutations(machine): self.assertEqual(call[2:],('kafel-weather.service',))

    def test_configuration_only_change_restarts_owner(self):
        root,_,machine,error,_,_=self.update_existing(['kafel-weather'],config={'latitude':'53','longitude':'18'})
        self.assertIsNone(error,error)
        self.assertIn('LATITUDE=53',(root/'etc/kafel/weather.env').read_text())
        self.assertTrue(self.mutations(machine))
        for call in self.mutations(machine): self.assertEqual(call[2:],('kafel-weather.service',))

    def test_failure_restores_only_owner(self):
        for failure in ('framebuffer','ready'):
            root,_,machine,error,before,previous=self.update_existing(['kafel-dashboard'],['kafel-dashboard'],failure)
            self.assertIsNotNone(error)
            self.assertEqual(os.readlink(root/'opt/kafel/current'),previous)
            for name in installer.NAMES:self.assertEqual((root/'opt/kafel/current/bin'/name).read_bytes(),before[name])
            for call in self.mutations(machine): self.assertEqual(call[2:],('kafel-dashboard.service',))
            self.assertTrue(machine.units['kafel-weather.service']['active'])
            self.assertTrue(machine.units['kafel-waste.service']['active'])

    def test_invalid_schedule_preflight_preserves_all_processes(self):
        _,_,machine,error,_,_=self.update_existing(['kafel-waste'],['kafel-waste'],'schedule')
        self.assertIsNotNone(error)
        self.assertEqual(self.mutations(machine),[])

    def test_display_boot_configuration_and_parameters_share_transaction(self):
        root,stage,machine,error,_,_=self.update_existing(['kafel-dashboard'],config={'framebuffer':'/dev/fb7','touch':'off','idle_timeout':'120'},boot=True)
        self.assertIsNone(error,error)
        boot=(root/'boot/firmware/cmdline.txt').read_text()
        self.assertIn('root=PARTUUID=EXAMPLE',boot)
        self.assertIn('video=HDMI-A-1:640x480@60D',boot)
        self.assertNotIn('fbcon=map',boot)
        environment=(root/'etc/kafel/dashboard.env').read_text()
        self.assertIn('KAFEL_FRAMEBUFFER="/dev/fb7"',environment)
        checks=[args for args in machine.calls if args[0]=='env' and args[-1] in ('--check','--check-input','--display-info')]
        self.assertEqual({args[-1] for args in checks},{'--check','--check-input','--display-info'})
        for args in checks:self.assertIn('KAFEL_FRAMEBUFFER=/dev/fb7',args)
        report=json.loads((stage/'report.json').read_text())
        self.assertTrue(report['display']['reboot_required'])
        self.assertTrue(all(item['same_pid'] for item in report['untouched_services'].values()))
        with tarfile.open(stage/'rollback.tar.gz') as archive:
            metadata=json.load(archive.extractfile('rollback.json'))
            self.assertTrue(any(item['path']=='/boot/firmware/cmdline.txt' for item in metadata['files']))

    def test_display_boot_file_restored_after_failed_dashboard_start(self):
        root,_,machine,error,_,_=self.update_existing(['kafel-dashboard'],failure='ready',boot=True)
        self.assertIsNotNone(error)
        self.assertEqual((root/'boot/firmware/cmdline.txt').read_text(),'root=PARTUUID=EXAMPLE rootwait console=tty2 fbcon=map\n')
        for call in self.mutations(machine):self.assertEqual(call[2:],('kafel-dashboard.service',))

    def test_helper_only_change_restarts_only_dashboard(self):
        root,stage,machine,error,_,_=self.update_existing(['kafel-dashboard'],helper_changed=True)
        self.assertIsNone(error,error)
        self.assertIn(b'upgraded helper',(root/'opt/kafel/libexec/kafel-display-console').read_bytes())
        self.assertEqual(json.loads((stage/'report.json').read_text())['changed'],['kafel-dashboard'])
        for call in self.mutations(machine):self.assertEqual(call[2:],('kafel-dashboard.service',))

    def test_bad_helper_digest_stops_before_any_service_change(self):
        _,_,machine,error,_,_=self.update_existing(['kafel-dashboard'],config={'display_helper_sha256':'0'*64})
        self.assertIsNotNone(error)
        self.assertIn('helper checksum',str(error))
        self.assertEqual(self.mutations(machine),[])

    def test_native_helper_retires_python_with_rollback_copy(self):
        root,stage,machine,error,_,_=self.update_existing(['kafel-dashboard'],old_helper=True)
        self.assertIsNone(error,error)
        self.assertFalse((root/'opt/kafel/libexec/display_console.py').exists())
        helper=root/'opt/kafel/libexec/kafel-display-console'
        self.assertEqual(helper.stat().st_mode&0o777,0o755)
        self.assertEqual(helper.read_bytes(),helper_fixture())
        self.assertNotIn('python',(root/'etc/systemd/system/kafel-dashboard.service').read_text())
        with tarfile.open(stage/'rollback.tar.gz') as archive:
            metadata=json.load(archive.extractfile('rollback.json'))
            self.assertTrue(any(item['path']=='/opt/kafel/libexec/display_console.py' for item in metadata['files']))
        for call in self.mutations(machine):self.assertEqual(call[2:],('kafel-dashboard.service',))

    def test_failed_native_helper_upgrade_restores_python(self):
        root,_,machine,error,_,_=self.update_existing(['kafel-dashboard'],old_helper=True,failure='ready')
        self.assertIsNotNone(error)
        self.assertEqual((root/'opt/kafel/libexec/display_console.py').read_bytes(),b'old-python-helper')
        self.assertIn('ExecStartPre=+/usr/bin/python3 /opt/kafel/libexec/display_console.py isolate',(root/'etc/systemd/system/kafel-dashboard.service').read_text())
        for call in self.mutations(machine):self.assertEqual(call[2:],('kafel-dashboard.service',))

    def test_socket_change_rejects_partial_deployment_before_stopping(self):
        for selected in (['kafel-dashboard'],['kafel-weather']):
            _,_,machine,error,_,_=self.update_existing(selected,config={'dashboard_socket':'/tmp/other/status.sock'})
            self.assertIsNotNone(error)
            self.assertEqual(self.mutations(machine),[])

    def test_legacy_schedule_migrates_with_verified_backup_and_no_other_restarts(self):
        root,stage,machine,error,_,_=self.update_existing(['kafel-waste'],legacy_schedule=True)
        self.assertIsNone(error,error)
        original=root/'home/operator/infohub/data/garbage_schedule.json'
        destination=root/'var/lib/kafel-waste/garbage_schedule.json'
        backup=root/'var/lib/kafel-waste/garbage_schedule.infohub-backup.json'
        self.assertFalse(original.exists())
        self.assertEqual(destination.read_bytes(),backup.read_bytes())
        self.assertIn('/var/lib/kafel-waste/garbage_schedule.json',(root/'etc/kafel/waste.env').read_text())
        report=json.loads((stage/'report.json').read_text())
        self.assertTrue(report['waste_schedule']['migration']['copied'])
        self.assertTrue(all(item['same_pid'] for item in report['untouched_services'].values()))
        for call in self.mutations(machine):self.assertEqual(call[2:],('kafel-waste.service',))
        with tarfile.open(stage/'rollback.tar.gz') as archive:
            self.assertEqual(archive.extractfile('waste-schedule-original.json').read(),backup.read_bytes())
            metadata=json.load(archive.extractfile('rollback.json'))
            self.assertEqual(metadata['waste_data_migration']['sha256'],hashlib.sha256(backup.read_bytes()).hexdigest())

    def test_failed_migration_deployment_restores_old_path_and_retains_both_data_copies(self):
        root,_,machine,error,_,_=self.update_existing(['kafel-waste'],legacy_schedule=True,failure='ready')
        self.assertIsNotNone(error)
        self.assertIn('/home/operator/infohub/data/garbage_schedule.json',(root/'etc/kafel/waste.env').read_text())
        self.assertEqual((root/'var/lib/kafel-waste/garbage_schedule.json').read_bytes(),
                         (root/'home/operator/infohub/data/garbage_schedule.json').read_bytes())
        for call in self.mutations(machine):self.assertEqual(call[2:],('kafel-waste.service',))

    def test_unchanged_waste_cleans_old_schedule_without_restart(self):
        root,stage,machine,error,_,_=self.update_existing(['kafel-waste'])
        self.assertIsNone(error,error)
        self.assertEqual(self.mutations(machine),[])
        report=json.loads((stage/'report.json').read_text())
        self.assertEqual(report['changed'],[])
        self.assertTrue(report['infohub_cleanup']['schedule_removed'])
        self.assertFalse(report['infohub_cleanup']['directory_remaining'])
        self.assertTrue((root/'var/lib/kafel-waste/garbage_schedule.infohub-backup.json').is_file())

    def test_conflicting_schedule_does_not_stop_any_service(self):
        root,_,machine,error,_,_=self.update_existing(['kafel-waste'],legacy_schedule=True,conflicting_schedule=True)
        self.assertIsNotNone(error)
        self.assertEqual(self.mutations(machine),[])
        self.assertIn('Europe/Warsaw',(root/'var/lib/kafel-waste/garbage_schedule.json').read_text())

if __name__=='__main__':unittest.main()
