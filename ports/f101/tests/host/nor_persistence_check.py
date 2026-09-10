#!/usr/bin/env python3
"""Prepare or verify a public HOTP credential across a board reset."""
import argparse
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3]/'k230d/tests/host'))
from host_check import Ccid  # noqa: E402

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('phase', choices=['prepare', 'verify'])
args = parser.parse_args()
card = Ccid()
name = b'f101-nor-persistence'
name_tlv = bytes([0x71, len(name)])+name
try:
    card.select('f000000000')
    version = card.apdu(bytes.fromhex('0031000000')).decode()
    if version != '3.0.2-f101-nor-dev':
        raise RuntimeError(f'Expected F101 NOR firmware, got {version}')
    card.select('a0000005272101')
    if args.phase == 'prepare':
        key = b'12345678901234567890'
        data = name_tlv+bytes([0x73, len(key)+2, 0x11, 6])+key
        card.apdu(bytes([0, 1, 0, 0, len(data)])+data)
    result = card.apdu(bytes([0, 0xa2, 0, 1, len(name_tlv)])+name_tlv)
    if len(result) != 7 or result[:3] != bytes([0x76, 5, 6]):
        raise RuntimeError('Malformed HOTP response')
    expected = 287082 if args.phase == 'prepare' else 359152
    actual = int.from_bytes(result[3:], 'big') % 1000000
    if actual != expected:
        raise RuntimeError(f'Expected {expected}, got {actual}')
    print(f'PASS: {args.phase}, persisted HOTP counter yields {actual}', flush=True)
    if args.phase == 'verify':
        card.apdu(bytes([0, 2, 0, 0, len(name_tlv)])+name_tlv)
finally:
    card.close()
