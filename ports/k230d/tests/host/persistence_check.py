#!/usr/bin/env python3
"""Create/check a public HOTP vector across a firmware restart; check deletes it."""
import argparse
from host_check import Ccid

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('operation', choices=['prepare', 'check'])
args = parser.parse_args()
card = Ccid()
name = b'k230d-persistence-test'
name_tlv = bytes([0x71, len(name)]) + name
try:
    card.select('a0000005272101')
    if args.operation == 'prepare':
        key = b'12345678901234567890'
        data = name_tlv + bytes([0x73, len(key)+2, 0x11, 6]) + key
        card.apdu(bytes([0, 1, 0, 0, len(data)]) + data)
    result = card.apdu(bytes([0, 0xa2, 0, 1, len(name_tlv)]) + name_tlv)
    if result[:3] != b'\x76\x05\x06' or len(result) != 7:
        raise RuntimeError('Invalid HOTP response')
    expected = 287082 if args.operation == 'prepare' else 359152
    actual = int.from_bytes(result[3:], 'big') % 1000000
    if actual != expected:
        raise RuntimeError(f'Expected {expected}, got {actual}')
    if args.operation == 'check':
        card.apdu(bytes([0, 2, 0, 0, len(name_tlv)]) + name_tlv)
    print(f'PASS: persistence {args.operation}, HOTP={actual}', flush=True)
finally:
    card.close()
