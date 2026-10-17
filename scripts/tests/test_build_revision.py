"""Source identity changes with commits and dirty worktrees, never ignored data."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('build_revision',Path(__file__).resolve().parents[1]/'build_revision.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class RevisionTests(unittest.TestCase):
    def setUp(self):
        tmp=tempfile.TemporaryDirectory();self.addCleanup(tmp.cleanup)
        self.root=Path(tmp.name)
        self.git('init','-q')
        (self.root/'source.cpp').write_text('initial')
        (self.root/'.gitignore').write_text('private.json\n')
        self.git('add','.')
        self.git('-c','user.name=Test','-c','user.email=test@example.invalid','commit','-qm','fixture')
        self.head=self.git('rev-parse','HEAD').strip()
    def git(self,*args):
        return subprocess.run(['git','-C',str(self.root),*args],check=True,capture_output=True,text=True).stdout
    def test_clean_commit_and_ignored_data(self):
        (self.root/'private.json').write_text('private')
        self.assertEqual(module.revision(self.root),self.head)
    def test_tracked_modification_is_dirty(self):
        (self.root/'source.cpp').write_text('modified')
        self.assertEqual(module.revision(self.root),self.head+'-dirty')
    def test_untracked_source_is_dirty(self):
        (self.root/'new.cpp').write_text('new')
        self.assertEqual(module.revision(self.root),self.head+'-dirty')
    def test_archive_does_not_inherit_parent_repository_identity(self):
        child=self.root/'archive';child.mkdir()
        self.assertEqual(module.revision(child),'unknown')
    def test_commit_changes_identity(self):
        (self.root/'source.cpp').write_text('next')
        self.git('add','source.cpp')
        self.git('-c','user.name=Test','-c','user.email=test@example.invalid','commit','-qm','next')
        self.assertNotEqual(module.revision(self.root),self.head)
        self.assertEqual(module.revision(self.root),self.git('rev-parse','HEAD').strip())
