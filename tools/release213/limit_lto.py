"""Cap only GCC link-time parallelism; preserve all optimization flags."""
import subprocess
import sys
command = ["-flto=2" if arg == "-flto=6" else arg for arg in sys.argv[1:]]
sys.exit(subprocess.call(command))
