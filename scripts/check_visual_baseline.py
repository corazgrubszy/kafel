#!/usr/bin/env python3
"""Compare synthetic renderer output with the reviewed pre-refactor baseline."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent

def main():
    binary = str(Path(sys.argv[1]).resolve())
    expected = json.loads((ROOT / "apps/dashboard/tests/visual_baseline.json").read_text())
    with tempfile.TemporaryDirectory(prefix="kafel-visual-") as directory:
        output = Path(directory)
        subprocess.run([binary, "--assets", directory], check=True)
        subprocess.run([binary, "--clock-frames", directory], check=True)
        for scene in ("waste-many", "limits", "error", "offline", "warning"):
            subprocess.run([binary, str(output / (scene + ".ppm")), scene], check=True)
        actual = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in output.glob("*.ppm")}
        different = sorted(name for name in expected.keys() | actual.keys() if expected.get(name) != actual.get(name))
        if different:
            raise SystemExit("Changed renderer frames: " + ", ".join(different))
    print(f"PASS visual baseline: {len(expected)} pixel-identical screens, swipe and rolling-clock frames")

if __name__ == "__main__":
    main()
