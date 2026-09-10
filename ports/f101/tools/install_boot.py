#!/usr/bin/env python3
"""Install the F101 boot slot and provision a blank seed journal through FEL.

Preserves the CanoKey filesystem. Requires a full backup before overwriting
boot code. A backup of the immediately overwritten boot slot is also saved.
"""

import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import zlib

from load_firmware import initialize_psram


def run(*args):
    return subprocess.run(
        ['xfel', *map(str, args)], check=True, timeout=30, capture_output=True
    ).stdout


def validate_image(image):
    if len(image) % 4096 or not 0x10000 < len(image) <= 0x80000 or image[4:12] != b'eGON.BT0':
        raise ValueError('Invalid SPI boot image size/header')
    spl_length = struct.unpack_from('<I', image, 16)[0]
    if spl_length % 512 or not 64 <= spl_length <= 0x10000:
        raise ValueError('Invalid SPL length')
    spl = bytearray(image[:spl_length])
    expected = struct.unpack_from('<I', spl, 12)[0]
    struct.pack_into('<I', spl, 12, 0x5f0a6c39)
    if (sum(struct.unpack('<' + 'I' * (spl_length // 4), spl)) & 0xffffffff) != expected:
        raise ValueError('SPL checksum mismatch')
    magic, version, length, address, crc, _, _, _ = struct.unpack_from('<8I', image, 0x10000)
    if magic != 0x31464b43 or version != 1 or address != 0x40010000:
        raise ValueError('Invalid application header')
    if not 0 < length <= 0x70000 - 32 or 0x10020 + length > len(image):
        raise ValueError('Invalid application length')
    if zlib.crc32(image[0x10020:0x10020 + length]) != crc:
        raise ValueError('Application CRC mismatch')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--backup', type=Path, required=True)
    parser.add_argument(
        '--recover-rng', action='store_true',
        help='Replace the RNG journal with fresh host entropy; preserve credentials'
    )
    args = parser.parse_args()
    image = args.image.read_bytes()
    try:
        validate_image(image)
    except ValueError as error:
        parser.error(str(error))
    if args.backup.stat().st_size != 0x1000000:
        parser.error('A full 16 MiB NOR backup is required')
    build = args.image.resolve().parent
    raw = build / 'f101-nor-install.bin'
    if not raw.is_file():
        parser.error(f'{raw}: build the F101 firmware first')
    initialize_psram(build)
    run('write', '0x20000', raw)

    with tempfile.TemporaryDirectory() as directory:
        tmp = Path(directory)

        def command(op, offset, length, data=None):
            if data is not None:
                (tmp / 'data').write_bytes(data)
                run('write', '0x40000000', tmp / 'data')
            (tmp / 'mail').write_bytes(struct.pack('<5I', 0x49313046, op, offset, length, 0xffffffff))
            run('write', '0x27f00', tmp / 'mail')
            run('exec', '0x20000')
            run('read', '0x27f10', 4, tmp / 'status')
            if (tmp / 'status').read_bytes() != b'\0' * 4:
                raise RuntimeError(f'Flash operation {op} failed at {offset:#x}')
            if op == 3:
                run('read', '0x40000000', length, tmp / 'read')
                return (tmp / 'read').read_bytes()

        old = b''.join(command(3, offset, 4096) for offset in range(0, len(image), 4096))
        backup = args.backup.resolve().parent / f'boot-slot-before-{time.time_ns()}.bin'
        with os.fdopen(os.open(backup, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600), 'wb') as f:
            f.write(old)
            f.flush()
            os.fsync(f.fileno())

        seed_area = b''.join(command(3, offset, 4096) for offset in (0xfbe000, 0xfbf000))
        if args.recover_rng or seed_area == b'\xff' * 8192:
            seed = os.urandom(48)
            generation = struct.pack('<I', 0)
            record = (
                struct.pack('<3I', 0x31474e52, 0, 0xffffffff)
                + seed + hashlib.sha256(generation + seed).digest() + b'\xff' * 4
            )
            if args.recover_rng:
                seed_backup = args.backup.resolve().parent / f'seed-journal-before-{time.time_ns()}.bin'
                with os.fdopen(os.open(seed_backup, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600), 'wb') as f:
                    f.write(seed_area)
                    f.flush()
                    os.fsync(f.fileno())
                command(1, 0xfbe000, 4096)
                command(1, 0xfbf000, 4096)
            # Program the body and commit marker without erasing between writes.
            # Two xfel spinor writes would erase this sector twice, losing the body.
            command(2, 0xfbe004, 92, record[4:])
            command(2, 0xfbe000, 4, record[:4])
            print('Provisioned fresh boot RNG state (not logged)', flush=True)
        else:
            print('Preserving existing seed journal', flush=True)

        # Write the application first and publish the boot header last.
        for offset in [*range(4096, len(image), 4096), 0]:
            if old[offset:offset + 4096] == image[offset:offset + 4096]:
                continue
            command(1, offset, 4096)
            command(2, offset, 4096, image[offset:offset + 4096])
            if command(3, offset, 4096) != image[offset:offset + 4096]:
                raise RuntimeError(f'Readback mismatch at {offset:#x}')
            print(f'Verified boot sector {offset:#x}', flush=True)
    print('SPI boot installation verified; reset without forcing FEL.')


if __name__ == '__main__':
    main()
