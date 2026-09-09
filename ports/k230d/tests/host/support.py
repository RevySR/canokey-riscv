"""Paths and JTAG transport shared by the host-side tests."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from openocd_command import run
