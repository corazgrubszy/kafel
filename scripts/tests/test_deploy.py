"""Offline configuration and SSH lifecycle checks; never connect to a device."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('dashboard_deploy', Path(__file__).resolve().parents[1] / 'deploy.py')
deploy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deploy)


class DeployTests(unittest.TestCase):
    def test_local_settings_and_cli_precedence(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'deploy.local.json').write_text(json.dumps({'host': 'lcd.local', 'user': 'operator', 'ssh_socket': '/tmp/test'}))
            args = deploy.settings(['dashboard','weather','waste'], root)
            self.assertEqual((args.host, args.user, args.socket), ('lcd.local', 'operator', '/tmp/test'))
            args = deploy.settings(['--host', 'other.local', '--user', 'other', '--socket', '', 'dashboard', 'weather', 'waste'], root)
            self.assertEqual((args.host, args.user, args.socket), ('other.local', 'other', ''))

    def test_bad_settings_fail_before_ssh(self):
        with tempfile.TemporaryDirectory() as folder, contextlib.redirect_stderr(io.StringIO()):
            root = Path(folder)
            for content in ['{}', '[]', '{"host": 4}', '{"host":"-oProxyCommand=oops","user":"user"}', '{"unknown":"value"}']:
                (root / 'deploy.local.json').write_text(content)
                with self.assertRaises(SystemExit):
                    deploy.settings(['dashboard','weather','waste'], root)

    def test_borrowed_session_is_not_closed(self):
        with patch.object(deploy.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0)) as run:
            with deploy.connection('user@host', '/tmp/existing') as options:
                self.assertIn('ControlPath=/tmp/existing', options)
                self.assertIn('BatchMode=yes', options)
            self.assertEqual(run.call_count, 1)
            self.assertIn('check', run.call_args.args[0])

    def test_owned_session_is_closed_even_on_failure(self):
        calls = []
        def run(command, **kwargs):
            calls.append(command)
            return subprocess.CompletedProcess(command, 0)
        with patch.object(deploy.subprocess, 'run', side_effect=run):
            with self.assertRaisesRegex(RuntimeError, 'install failed'):
                with deploy.connection('user@host'):
                    raise RuntimeError('install failed')
        self.assertIn('ControlMaster=yes', calls[0])
        self.assertIn('exit', calls[-1])
        self.assertEqual([x for x in calls[0] if x.startswith('ControlPath=')],
                         [x for x in calls[-1] if x.startswith('ControlPath=')])


    def test_selected_target_builds_only_its_app_before_ssh(self):
        for app, build in [('dashboard','linux'),('weather','weather-linux'),('waste','waste-linux')]:
            with tempfile.TemporaryDirectory() as folder:
                root=Path(folder)
                (root/'scripts').mkdir()
                (root/'scripts/display_console.py').write_text('# fixture')
                (root/'scripts/waste_data.py').write_text('# fixture')
                args=deploy.settings(['--host','cm4','--user','operator','--only',app,'--build'],root)
                binary=getattr(args,app)
                binary.parent.mkdir(parents=True)
                header=bytearray(64);header[:6]=b'\x7fELF\x02\x01';header[18:20]=b'\xb7\x00';binary.write_bytes(header)
                with patch.object(deploy,'ROOT',root),patch.object(deploy.subprocess,'run') as run,patch.object(deploy,'connection',side_effect=RuntimeError('offline test')),contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaisesRegex(RuntimeError,'offline test'):deploy.deploy(args)
                command=run.call_args.args[0]
                self.assertEqual(command,['zig','build',app+'-test','deploy-test',build,'-Doptimize=ReleaseFast'])
                self.assertEqual(len(list((root/'deploy-results').glob('*/deploy.log'))),1)

if __name__ == '__main__':
    unittest.main()
