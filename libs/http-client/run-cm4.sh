#!/bin/sh
set -eu
if [ -z "${KAFEL_HOST:-}" ]; then
    echo 'Set KAFEL_HOST=user@host before running CM4 measurements.' >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -n "${PYTHON:-}" ]; then
    python=$PYTHON
elif python3 -c 'import cryptography' 2>/dev/null; then
    python=python3

else
    echo 'Set PYTHON to Python 3 with cryptography installed.' >&2
    exit 2
fi
exec "$python" "$root/tests/run_remote.py" "$KAFEL_HOST"
