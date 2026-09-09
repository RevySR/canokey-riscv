#!/usr/bin/env python3
"""Read SD through the SRAM probe. Explicit flags enable destructive test/provision operations."""
import argparse
from pathlib import Path
import socket
import re
import subprocess
import tempfile
import time
from support import ROOT, run

LAYOUT = {name: int(value) for name, value in re.findall(r"#define (K230D_\w+) (\d+)", (ROOT/"include/raw_layout.h").read_text())}
FORMAT_MARKER = b'K230D-LFS-FORMAT-v1\0'


class SDProbe:
    def __init__(self):
        symbols = subprocess.check_output(['riscv64-unknown-elf-nm', str(ROOT/'build/k230d-sd-probe.elf')], text=True)
        self.address = next(int(line.split()[0], 16) for line in symbols.splitlines() if line.endswith(' B firmware_state'))
        self.sock = socket.create_connection(('127.0.0.1', 6666), timeout=15)
        self.temp = tempfile.TemporaryDirectory(prefix='k230d-sd-')
        self.file = Path(self.temp.name)/'sector.bin'
        self.cmd('halt')
        self.check()

    def cmd(self, command):
        return run(self.sock, command, verbose=False)

    def check(self):
        values = [int(v, 0) for v in self.cmd(f'read_memory {self.address} 64 4').split()]
        if values[:3] != [0x4b32333053445052, 3, 0]:
            raise RuntimeError(f'SD probe not ready or operation failed: {values}')
        return values[3]

    def operation(self, lba, request):
        if not 0 <= lba < self.check():
            raise ValueError('LBA outside card')
        self.cmd(f'mww {self.address+108} {lba}')
        self.cmd(f'mww {self.address+104} {request}')
        for _ in range(20):
            self.cmd('resume')
            self.cmd('sleep 100')
            self.cmd('halt')
            pending = int(self.cmd(f'read_memory {self.address+104} 32 1').strip(), 0)
            if not pending:
                self.check()
                return
        raise RuntimeError('SD probe request did not complete')

    def read(self, lba):
        # Clear the transfer buffer first: a stale buffer must never pass a test.
        self.cmd(f'mww {self.address+112} 0 128')
        self.operation(lba, 1)
        self.cmd(f'dump_image {self.file} {self.address+112} 512')
        data = self.file.read_bytes()
        if len(data) != 512:
            raise RuntimeError('Short SD sector')
        return data

    def write(self, lba, data):
        if len(data) != 512:
            raise ValueError('Exactly 512 bytes required')
        self.file.write_bytes(data)
        self.cmd(f'load_image {self.file} {self.address+112} bin')
        self.operation(lba, 2)
        if self.read(lba) != data:
            raise RuntimeError(f'SD write verification failed at {lba}')

    def restart(self):
        self.cmd('resume 0x80280000')
        self.cmd('sleep 1500')
        self.cmd('halt')
        self.check()

    def close(self):
        self.sock.close()
        self.temp.cleanup()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write-test', action='store_true', help='overwrite 16 raw test sectors after the storage region')
    parser.add_argument('--stress', type=int, default=0, metavar='ROUNDS',
                        help='write/read unique patterns in the 16-sector scratch area, 1..1024 rounds')
    parser.add_argument('--initialize-raw', action='store_true', help='erase LBA0 and reset the fixed raw storage area with a format marker')
    args = parser.parse_args()
    if not 0 <= args.stress <= 1024:
        parser.error('--stress must be 1..1024')
    sd = SDProbe()
    try:
        start = LAYOUT['K230D_STORAGE_LBA']
        count = LAYOUT['K230D_STORAGE_SECTORS']
        print('SD sectors:', sd.check(), 'raw storage:', start, count, flush=True)
        if start+count+16 > sd.check():
            raise RuntimeError('Card too small for raw layout and test area')
        if args.stress:
            sd.cmd(f'mww {sd.address+632} {args.stress}')
            sd.cmd(f'mww {sd.address+104} 3')
            sd.cmd('resume')
            deadline=time.monotonic()+180
            while True:
                time.sleep(1)
                sd.cmd('halt')
                pending=int(sd.cmd(f'read_memory {sd.address+104} 32 1'),0)
                if not pending:
                    sd.check()
                    values=[int(v,0) for v in sd.cmd(f'read_memory {sd.address+624} 32 5').split()]
                    retries, hz, rounds, done, ms=values
                    if done!=args.stress:
                        raise RuntimeError(f'Incomplete stress test: {values}')
                    print(f'PASS: stress rounds={done}, clock={hz} Hz, retries={retries}, elapsed={ms} ms',flush=True)
                    break
                if time.monotonic()>deadline:
                    raise RuntimeError('Stress test timed out')
                sd.cmd('resume')
        if args.write_test:
            patterns = [bytes([v])*512 for v in [0, 255, 0x55, 0xaa]]
            patterns += [bytes((j*73+i*29+19)&255 for j in range(512)) for i in range(12)]
            for i, data in enumerate(patterns):
                sd.write(start+count+i, data)
            sd.restart()
            for i, data in enumerate(patterns):
                if sd.read(start+count+i) != data:
                    raise RuntimeError('Data changed after probe restart')
            print('PASS: 16 raw SDIO sectors write/read and restart/read', flush=True)
        if args.initialize_raw:
            sd.write(0, bytes(512))
            sd.write(start, FORMAT_MARKER.ljust(512, b'\0'))
            print('INITIALIZED: no partition table; one-time format marker at fixed raw storage LBA', start, flush=True)
    finally:
        sd.close()


if __name__ == '__main__':
    main()
