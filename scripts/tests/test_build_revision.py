"""Source identity changes with commits and dirty worktrees, never ignored data."""
import importlib.util
import json
import shutil
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

    def test_generated_source_tracks_git_state_on_each_invocation(self):
        script=Path(__file__).resolve().parents[1]/'build_revision.py'
        def source():
            return subprocess.check_output(['python3',str(script),'--source',str(self.root)],text=True)
        def expected(value):
            return 'extern "C" const char* kafel_git_revision() noexcept { return "'+value+'"; }\n'
        self.assertEqual(source(),expected(self.head))
        (self.root/'source.cpp').write_text('changed')
        self.assertEqual(source(),expected(self.head+'-dirty'))

    @unittest.skipUnless(shutil.which('zig'), 'requires the Zig build tool')
    def test_cached_binary_refreshes_after_commit_and_dirty_changes(self):
        script=str(Path(__file__).resolve().parents[1]/'build_revision.py')
        (self.root/'main.c').write_text('#include <stdio.h>\nconst char* kafel_git_revision(void);\nint main(void){puts(kafel_git_revision());}\n')
        (self.root/'.gitignore').write_text('private.json\n.zig-cache/\nzig-out/\n')
        build = r'''const std = @import("std");
pub fn build(b: *std.Build) void {
    const identity = b.addSystemCommand(&.{"python3", SCRIPT, "--source"});
    identity.addDirectoryArg(b.path("."));
    identity.has_side_effects = true;
    const source = identity.captureStdOut(.{.basename="revision.cpp"});
    const binary = b.addExecutable(.{.name="revision-probe", .root_module=b.createModule(.{.target=b.graph.host, .optimize=.ReleaseSmall, .link_libc=true})});
    binary.root_module.addCSourceFile(.{.file=b.path("main.c"),.flags=&.{}});
    binary.root_module.addCSourceFile(.{.file=source,.flags=&.{"-std=c++20"}});
    const install = b.addInstallArtifact(binary,.{});
    b.getInstallStep().dependOn(&install.step);
}
'''
        (self.root/'build.zig').write_text(build.replace('SCRIPT',json.dumps(script)))
        self.git('add','.')
        self.git('-c','user.name=Test','-c','user.email=test@example.invalid','commit','-qm','probe')
        def built_revision():
            result=subprocess.run(['zig','build','-j2'],cwd=self.root,text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            return subprocess.check_output([str(self.root/'zig-out/bin/revision-probe')],text=True).strip()
        first=self.git('rev-parse','HEAD').strip()
        self.assertEqual(built_revision(),first)
        (self.root/'source.cpp').write_text('next commit')
        self.git('add','source.cpp')
        self.git('-c','user.name=Test','-c','user.email=test@example.invalid','commit','-qm','next')
        second=self.git('rev-parse','HEAD').strip()
        self.assertNotEqual(first,second)
        self.assertEqual(built_revision(),second)
        (self.root/'source.cpp').write_text('dirty source')
        self.assertEqual(built_revision(),second+'-dirty')
