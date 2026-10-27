"""Formatting scope and enforcement, without changing repository sources."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("check_format", ROOT / "scripts/check_format.py")
formatting = importlib.util.module_from_spec(spec)
spec.loader.exec_module(formatting)


class FormatTests(unittest.TestCase):
    def test_scope(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", directory], check=True)
            for name in ("apps/a.cpp", "libs/a.hpp", "packaging/a.h", "vendor/a.h", "notes.md"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("")
            subprocess.run(["git", "add", "."], cwd=root, check=True)
            (root / "generated.cpp").write_text("")
            self.assertEqual(formatting.source_files(root),
                             ["apps/a.cpp", "libs/a.hpp", "packaging/a.h"])

    def test_rejects_bad_format_without_writing(self):
        source = "int main(){return 0;}\n"
        command = [formatting.formatter(), "--style=file", "--assume-filename=" +
                   str(ROOT / "apps/fixture.cpp")]
        bad = subprocess.run(command + ["--dry-run", "--Werror"], input=source,
                             text=True, capture_output=True)
        self.assertNotEqual(bad.returncode, 0)
        fixed = subprocess.run(command, input=source, text=True, capture_output=True, check=True)
        self.assertNotEqual(source, fixed.stdout)
        good = subprocess.run(command + ["--dry-run", "--Werror"], input=fixed.stdout,
                              text=True, capture_output=True)
        self.assertEqual(good.returncode, 0, good.stderr)
