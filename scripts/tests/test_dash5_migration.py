"""Protocol generation checks happen before any service or release changes."""
import importlib.util
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
spec=importlib.util.spec_from_file_location('dash5_install',Path(__file__).resolve().parents[1]/'install.py')
install=importlib.util.module_from_spec(spec);spec.loader.exec_module(install)
class MigrationTests(unittest.TestCase):
    def exercise(self, selected, installed, staged):
        temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup)
        root=Path(temp.name);stage=root/'stage';current=root/'current';stage.mkdir();(current/'bin').mkdir(parents=True)
        versions={};calls=[]
        for name in install.NAMES:
            path=current/'bin'/name;path.touch();versions[str(path)]=name+' '+installed
            if name in selected:
                path=stage/name;path.touch();versions[str(path)]=name+' '+staged
        def command(*args):
            calls.append(args);return SimpleNamespace(stdout=versions[args[0]])
        job=SimpleNamespace(stage=stage,current=current,command=command)
        return lambda:install.Installer.verify_protocol(job,{'dash_protocol':5},selected),calls
    def test_manifest_generation_is_required(self):
        with self.assertRaisesRegex(RuntimeError,'DASH/5'):
            install.Installer.verify_protocol(SimpleNamespace(),{},list(install.NAMES))
    def test_partial_migration_refused(self):
        for name in install.NAMES:
            run,calls=self.exercise([name],'0.1.0 DASH/4','0.5.0 DASH/5')
            with self.assertRaisesRegex(RuntimeError,'all three'):run()
            self.assertTrue(all(call[-1]=='--version' for call in calls))
    def test_full_migration_allowed(self):
        run,_=self.exercise(list(install.NAMES),'0.1.0 DASH/4','0.5.0 DASH/5');run()
    def test_independent_updates_after_migration(self):
        for name in install.NAMES:
            run,_=self.exercise([name],'0.5.0 DASH/5','0.5.1 DASH/5');run()
    def test_wrong_staged_generation_refused(self):
        run,_=self.exercise(list(install.NAMES),'0.1.0 DASH/4','0.1.1 DASH/4')
        with self.assertRaisesRegex(RuntimeError,'Mixed protocol'):run()
