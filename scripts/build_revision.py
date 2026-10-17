"""Build-time source identity; source archives explicitly report unknown."""
from pathlib import Path
import re
import subprocess
import sys


def revision(root):
    def git(*arguments):
        return subprocess.run(['git','-C',str(root),*arguments],capture_output=True,text=True,timeout=10)
    try:
        # Do not accidentally identify a parent repository when building an archive.
        if not (Path(root)/'.git').exists():return 'unknown'
        head=git('rev-parse','--verify','HEAD')
        commit=head.stdout.strip()
        if head.returncode or not re.fullmatch(r'[0-9a-f]{40}',commit):return 'unknown'
        status=git('status','--porcelain','--untracked-files=normal')
        if status.returncode:return 'unknown'
        return commit+('-dirty' if status.stdout else '')
    except (OSError,subprocess.SubprocessError):return 'unknown'


if __name__=='__main__':print(revision(Path(sys.argv[1])))
