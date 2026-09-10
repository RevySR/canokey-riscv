#!/usr/bin/env python3
"""Load the F101S3 RAM development firmware through FEL (no flash writes)."""

import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def run(*args):
    return subprocess.run(['xfel', *map(str, args)], check=True, timeout=60,
                          capture_output=True).stdout


def initialize_psram(build):
    if 'ID=0x00193700(F101)' not in run('version').decode():
        raise SystemExit('Expected an F101 in FEL mode')
    image = Path(build) / 'f101-psram-init.bin'
    if not image.is_file():
        raise FileNotFoundError(f'{image}: build the F101 firmware first')
    with tempfile.TemporaryDirectory() as directory:
        result = Path(directory) / 'result.bin'
        result.write_bytes(bytes(16))
        run('write', '0x27f00', result)
        run('write', '0x28000', image)
        run('exec', '0x28000')
        run('read', '0x27f00', 16, result)
        magic, size, cause, pc = struct.unpack('<4I', result.read_bytes())
    if magic != 0x5053524d or size != 0x1000000 or cause:
        raise RuntimeError(f'PSRAM initialization failed: size={size:#x}, cause={cause:#x}, pc={pc:#x}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    args = parser.parse_args()
    data = args.image.read_bytes()
    if not data or len(data) > 4 * 1024 * 1024:
        parser.error('Expected a nonempty raw firmware binary smaller than 4 MiB')
    initialize_psram(args.image.resolve().parent)
    with tempfile.TemporaryDirectory(prefix='canokey-f101-') as name:
        work = Path(name)
        image = work / 'firmware.bin'
        image.write_bytes(data)
        seed = work / 'seed.bin'
        seed.write_bytes(struct.pack('<I', 0x53454544) + os.urandom(48))
        run('write', '0x40010000', image)
        check = work / 'readback.bin'
        run('read', '0x40010000', len(data), check)
        if check.read_bytes() != data:
            raise SystemExit('Firmware RAM readback mismatch')
        run('write', '0x40fff000', seed)
        print(f'Verified {len(data)} bytes; starting CanoKey', flush=True)
        # The ROM normally acknowledges execution before the USB takeover.
        run('exec', '0x40010000')


if __name__ == '__main__':
    main()
