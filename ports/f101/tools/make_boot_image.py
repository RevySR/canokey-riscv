#!/usr/bin/env python3
"""Build an F101 SPI boot image."""
import argparse
from pathlib import Path
import struct
import zlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--spl', type=Path, required=True)
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build = args.output.resolve().parent
    build.mkdir(parents=True, exist_ok=True)
    spl = bytearray(args.spl.read_bytes())
    if len(spl) < 64 or len(spl) > 0x6000 or spl[4:12] != b'eGON.BT0':
        parser.error('Invalid SPL size/header')
    spl.extend(b'\0'*(-len(spl) % 512))
    struct.pack_into('<I', spl, 12, 0x5f0a6c39)
    struct.pack_into('<I', spl, 16, len(spl))
    struct.pack_into('<I', spl, 12, sum(struct.unpack('<'+'I'*(len(spl)//4), spl)) & 0xffffffff)
    app = args.firmware.read_bytes()
    if not app or len(app) > 0x70000-32:
        raise SystemExit('Application does not fit boot slot')
    image = spl+b'\xff'*(0x10000-len(spl))
    image += struct.pack('<8I', 0x31464b43, 1, len(app), 0x40010000, zlib.crc32(app), 0, 0, 0)+app
    image += b'\xff'*(-len(image) % 4096)
    args.output.write_bytes(image)
    print('SPL:', len(spl), 'application:', len(app), 'boot image:', len(image))


if __name__ == '__main__':
    main()
