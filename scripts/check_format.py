#!/usr/bin/env python3
"""Check owned C/C++ sources; --fix applies the repository style."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EXTENSIONS = {".c", ".cc", ".cpp", ".h", ".hpp"}


def source_files(root=ROOT):
    tracked = subprocess.check_output(["git", "ls-files", "-z"], cwd=root).decode().split("\0")
    return sorted(path for path in tracked if Path(path).suffix in EXTENSIONS
                  and "vendor" not in Path(path).parts)


def formatter():
    candidates = [os.environ["CLANG_FORMAT"]] if os.environ.get("CLANG_FORMAT") else [
        "clang-format-22", "clang-format", "/opt/homebrew/opt/llvm@22/bin/clang-format",
        "/usr/local/opt/llvm@22/bin/clang-format"]
    for candidate in candidates:
        executable = shutil.which(candidate)
        if not executable:
            continue
        version = subprocess.check_output([executable, "--version"], text=True)
        if re.search(r"version 22\.", version):
            return executable
    raise RuntimeError("Install clang-format 22 or set CLANG_FORMAT to its executable.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fix", action="store_true")
    args = parser.parse_args()
    executable = formatter()
    files = source_files()
    if args.fix:
        subprocess.run([executable, "--style=file", "-i", *files], cwd=ROOT, check=True)
    else:
        subprocess.run([executable, "--style=file", "--dry-run", "--Werror", *files],
                       cwd=ROOT, check=True)
    print(f"PASS formatting: {len(files)} owned C/C++ files (clang-format 22)")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
