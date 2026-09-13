"""Preserve native build output without PowerShell stderr error conversion."""
import subprocess
import sys

with open(sys.argv[1], "wb") as log:
    result = subprocess.run([sys.executable, "-B", *sys.argv[2:]], stdout=log, stderr=subprocess.STDOUT)
sys.exit(result.returncode)
