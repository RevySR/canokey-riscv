#!/usr/bin/env python3
"""Package SRAM firmware using the supplied SDK's non-encrypted K230 BootROM format."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
LAYOUT = {name: int(value) for name, value in re.findall(r'#define (K230D_\w+) (\d+)', (ROOT/'include/raw_layout.h').read_text())}
BOOT_OFFSETS = (0x100000, 0x180000)  # SDK genimage.cfg primary/backup SPL
SLOT_SIZE = 0x80000


def wrap(binary):
    payload = struct.pack('<I', 0) + binary
    header = b'K230' + struct.pack('<II', len(payload), 0)
    header += hashlib.sha256(payload).digest() + bytes(484)
    wrapped = header + payload
    if not binary or len(wrapped) > SLOT_SIZE:
        raise ValueError('Firmware must fit a 512 KiB BootROM slot including its header')
    return wrapped


def verify(wrapped, binary):
    if wrapped[:4] != b'K230':
        raise ValueError('Bad BootROM magic')
    size, encryption = struct.unpack_from('<II', wrapped, 4)
    if encryption != 0 or size != len(wrapped)-528:
        raise ValueError('Bad BootROM header length/type')
    payload = wrapped[528:]
    if wrapped[12:44] != hashlib.sha256(payload).digest() or payload != bytes(4)+binary:
        raise ValueError('BootROM payload/hash mismatch')


def write(path, content):
    path.write_bytes(content)
    path.with_suffix(path.suffix+'.sha256').write_text(hashlib.sha256(content).hexdigest()+'  '+path.name+'\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('prefix', type=Path)
    parser.add_argument('--with-storage', action='store_true')
    args = parser.parse_args()
    binary = args.binary.read_bytes()
    boot = wrap(binary)
    verify(boot, binary)
    image = bytearray(LAYOUT['K230D_BOOT_IMAGE_BYTES'])
    for offset in BOOT_OFFSETS:
        image[offset:offset+len(boot)] = boot
        verify(bytes(image[offset:offset+len(boot)]), binary)
    write(Path(str(args.prefix)+'.boot.bin'), boot)
    write(Path(str(args.prefix)+'.img'), image)
    offset = LAYOUT['K230D_STORAGE_LBA']*512
    length = LAYOUT['K230D_STORAGE_SECTORS']*512
    if len(image) > offset:
        raise ValueError('Boot image overlaps raw storage')
    if args.with_storage:
        factory = bytearray(b'\xff')*(offset+length)
        factory[:len(image)] = image
        factory[offset:offset+512] = b'K230D-LFS-FORMAT-v1\0'.ljust(512, b'\0')
        write(Path(str(args.prefix)+'-factory.img'), factory)
    Path(str(args.prefix)+'.layout.json').write_text(json.dumps({
        'entry': '0x80300000', 'boot_offsets': list(BOOT_OFFSETS),
        'boot_slot_bytes': SLOT_SIZE, 'boot_payload_bytes': len(binary),
        'firmware_update_image_bytes': len(image),
        'raw_storage_offset': offset, 'raw_storage_bytes': length,
        'partition_table': False,
    }, indent=2)+'\n')
    print(f'Packaged {len(binary)} bytes; two BootROM copies; raw storage at {offset:#x}')


if __name__ == '__main__':
    main()
