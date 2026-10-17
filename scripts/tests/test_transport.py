import importlib.util
from pathlib import Path
import tempfile
import unittest
import io
import subprocess
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('kafel_transport',Path(__file__).resolve().parents[1]/'deploy.py')
deploy=importlib.util.module_from_spec(spec);spec.loader.exec_module(deploy)

class TransportTests(unittest.TestCase):
    def test_arm64_identity_before_any_connection(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'binary'
            for bad in (b'',b'#!/bin/sh\n',b'\x7fELF\x02\x01'+bytes(14)):
                path.write_bytes(bad)
                with self.assertRaises(RuntimeError):deploy.arm64_digest(path)
            header=bytearray(64);header[:6]=b'\x7fELF\x02\x01';header[18:20]=b'\xb7\x00';path.write_bytes(header)
            self.assertEqual(len(deploy.arm64_digest(path)),64)

    def test_units_share_real_tmp_and_no_legacy_executable_paths(self):
        root=Path(__file__).resolve().parents[2]
        for name in ('kafel-dashboard','kafel-weather','kafel-waste'):
            unit=(root/'packaging/systemd'/(name+'.service.in')).read_text()
            self.assertIn('ExecStart=/opt/kafel/current/bin/'+name,unit)
            self.assertNotIn('PrivateTmp=yes',unit)
            self.assertIn('WantedBy=multi-user.target',unit)

    def test_identical_binary_skips_network_transfer(self):
        for same in (True, False):
            calls=[]
            def run(command, **kwargs):
                calls.append(command)
                return subprocess.CompletedProcess(command, 0 if same else 1, ('a'*64+' file') if same else '', '')
            with patch.object(deploy.subprocess,'run',side_effect=run):
                result=deploy.stage_binary(['ssh'],['scp'],'operator@cm4','/tmp/stage','kafel-weather',Path('/local/weather'),'a'*64,io.StringIO())
            self.assertEqual(result,same)
            self.assertEqual(calls[-1][0], 'ssh' if same else 'scp')
            if same:self.assertIn('cp --',calls[-1][-1])

if __name__=='__main__':unittest.main()
