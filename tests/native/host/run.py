"""Run native tests without allowing Windows loader failures to open modal dialogs."""
import ctypes
import os
from pathlib import Path
import subprocess
import sys

if os.name == "nt":
    ctypes.windll.kernel32.SetErrorMode(0x8003)
result = subprocess.run([sys.argv[1]], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
Path(sys.argv[2]).write_bytes(result.stdout)
sys.stdout.buffer.write(result.stdout)
raise SystemExit(0 if result.returncode == 0 else 1)
