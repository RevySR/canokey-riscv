#!/usr/bin/env python3
"""Read boot timing and cache/SD status using CPU-coherent OpenOCD progbuf."""
import argparse
import json
from pathlib import Path
import socket
import subprocess
from openocd_command import run

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--elf', type=Path, default=Path(__file__).resolve().parents[1]/'build/canokey-k230d-sd.elf')
args = parser.parse_args()
symbols = {parts[2]: int(parts[0], 16)
           for line in subprocess.check_output(['riscv64-unknown-elf-nm', str(args.elf)], text=True).splitlines()
           if len(parts := line.split()) == 3}
with socket.create_connection(('127.0.0.1', 6666), timeout=10) as sock:
    def cmd(command):
        return run(sock, command, verbose=False)
    def words(address, bits, count):
        return [int(v, 0) for v in cmd(f'read_memory {address} {bits} {count}').split()]
    cmd('halt')
    try:
        cmd('riscv set_mem_access progbuf')
        address = symbols['firmware_state']
        magic, stage, loops, error = words(address, 64, 4)
        if magic != 0x4b32333043414e4f or stage != 5 or error:
            raise RuntimeError(f'Firmware not ready: stage={stage}, error={error}')
        timing = words(address+1096, 32, 4)
        status = dict(zip(('boot_ms', 'storage_ms', 'applets_ms', 'usb_ms'), timing))
        status['mhcr'] = hex(words(address+1112, 64, 1)[0])
        status['mounted'] = words(address+60, 32, 1)[0]
        status['applet_ms'] = dict(zip(('openpgp','piv','oath','ctap','admin','ndef','pass'),
                                      words(address+1120, 32, 7)))
        if 'sd_state' in symbols:
            status['sd_retries'], status['sd_clock_hz'] = words(symbols['sd_state']+624, 32, 2)
            status['sd_init_ms'], status['sd_card_ready_ms'] = words(symbols['sd_state']+644,32,2)
        if 'fs_cache_stats' in symbols:
            status['fs_cache_hits'], status['fs_cache_misses'] = words(symbols['fs_cache_stats'],32,2)
        print(json.dumps(status, indent=2))
    finally:
        cmd('resume')
