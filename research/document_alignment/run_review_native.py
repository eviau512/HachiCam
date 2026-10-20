"""Run only the offline reviewer, suppressing native Windows error dialogs."""
import ctypes, os, subprocess, sys
from pathlib import Path
if os.name=='nt':ctypes.windll.kernel32.SetErrorMode(0x8003)
result=subprocess.run([sys.argv[1],*sys.argv[3:]],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
Path(sys.argv[2]).write_bytes(result.stdout)
sys.stdout.buffer.write(result.stdout)
raise SystemExit(0 if result.returncode==0 else 1)
