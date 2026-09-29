#!/usr/bin/env python3
"""Exercise real autosave, abrupt process exit and startup recovery."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary, data = sys.argv[1:]
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_SHUTDOWN_DBUS_ON_QUIT='1')
with tempfile.TemporaryDirectory(prefix='tak-editor-recovery-') as folder:
    for phase in ('write', 'restore'):
        subprocess.run([binary, data, 'recovery', folder, phase], env=env,
                       check=True, timeout=30)
        if phase == 'write':
            assert list(Path(folder).rglob('recovery-*.kmp')), 'abrupt exit lost recovery'
    assert (Path(folder) / 'Ulasem Arena.kmp').is_file()
print('PASS: interrupted editor autosave, startup recovery, saved edits, cleanup')
