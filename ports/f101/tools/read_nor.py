#!/usr/bin/env python3
"""Back up an F101 SPI NOR range through the FEL reader."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile
from load_firmware import initialize_psram


def run(*args):
    subprocess.run(['xfel', *map(str, args)], check=True, timeout=30, capture_output=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reader', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--offset', type=lambda x: int(x, 0), default=0)
    parser.add_argument('--length', type=lambda x: int(x, 0), required=True)
    args = parser.parse_args()
    if args.offset < 0 or args.length <= 0 or args.offset+args.length > 0x1000000:
        parser.error('Range must fit the 24-bit NOR address space')
    initialize_psram(args.reader.resolve().parent)
    # Exclusive creation prevents accidental overwrite of an earlier backup.
    with os.fdopen(os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'wb') as dest, tempfile.TemporaryDirectory() as directory:
        tmp = Path(directory)
        run('write', '0x20000', args.reader)
        for offset in range(0, args.length, 65536):
            count = min(65536, args.length-offset)
            mailbox = tmp/'mailbox.bin'
            mailbox.write_bytes(struct.pack('<III', args.offset+offset, count, 0xffffffff))
            run('write', '0x27f00', mailbox)
            run('exec', '0x20000')
            run('read', '0x27f08', 4, tmp/'result.bin')
            if (tmp/'result.bin').read_bytes() != b'\0'*4:
                raise RuntimeError(f'Flash read failed at {args.offset+offset:#x}')
            run('read', '0x40000000', count, tmp/'data.bin')
            data = (tmp/'data.bin').read_bytes()
            if len(data) != count:
                raise RuntimeError('Short FEL read')
            dest.write(data)
            print(f'{offset+count}/{args.length}', flush=True)
    print('SHA256', hashlib.sha256(args.output.read_bytes()).hexdigest())


if __name__ == '__main__':
    main()
