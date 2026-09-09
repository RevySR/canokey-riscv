#!/usr/bin/env python3
"""Load a verified M-mode SRAM ELF through an existing OpenOCD instance."""
import argparse
from pathlib import Path
import socket
import subprocess
from openocd_command import run

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('image', nargs='?', default=str(Path(__file__).resolve().parents[1]/'build/canokey-k230d.elf'))
args = parser.parse_args()
image = Path(args.image).resolve(strict=True)
quoted = '[binary format H* ' + str(image).encode().hex() + ']'
symbols = subprocess.check_output(['riscv64-unknown-elf-nm', str(image)], text=True)
entry = next(int(line.split()[0],16) for line in symbols.splitlines() if line.endswith(' T _start'))
with socket.create_connection(('127.0.0.1', 6666), timeout=60) as sock:
    for command in [
        'halt',
        'riscv set_mem_access progbuf',
        'riscv exec_progbuf 0x03300293 0x7c229073',
        'riscv exec_progbuf 0x7c105073 0x0000100f',
        'mww 0x91500804 2',
        'mww 0x91500010 0',
        'sleep 100',
        f'load_image {quoted}',
        f'verify_image {quoted}',
        'riscv exec_progbuf 0x0000100f',
        f'resume {entry}',
    ]:
        run(sock, command)
