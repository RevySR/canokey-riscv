#!/usr/bin/env python3
"""Read/write NDEF over CCID and restore the original file afterward."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3]/'k230d/tests/host'))
from host_check import Ccid  # noqa: E402
card = Ccid()
try:
    card.select('d2760000850101')
    card.apdu(bytes.fromhex('00a4000c02e103'))
    cc = card.apdu(bytes.fromhex('00b000000f'))
    if len(cc) != 15 or cc[:3] != bytes.fromhex('000f20') or cc[7:9] != b'\x04\x06':
        raise RuntimeError('Invalid NDEF capability container')
    if cc[13:15] != b'\0\0':
        raise RuntimeError('NDEF access is restricted; left unchanged')
    size = int.from_bytes(cc[11:13], 'big')
    card.apdu(bytes.fromhex('00a4000c02')+cc[9:11])
    original = card.apdu(bytes.fromhex('00b0000000')+size.to_bytes(2, 'big'))
    if len(original) != size:
        raise RuntimeError('Incomplete NDEF backup')
    try:
        # A short text record; exercise extended APDU writes with filler.
        record = b'\xd1\x01\x0bT\x02enF101test'
        value = len(record).to_bytes(2, 'big')+record
        value += bytes((i*13) & 255 for i in range(size-len(value)))
        card.apdu(bytes.fromhex('00d6000000')+len(value).to_bytes(2, 'big')+value)
        actual = card.apdu(bytes.fromhex('00b0000000')+size.to_bytes(2, 'big'))
        if actual != value:
            raise RuntimeError('NDEF readback mismatch')
        print(f'PASS: NDEF {size}-byte extended APDU write/read', flush=True)
    finally:
        card.apdu(bytes.fromhex('00d6000000')+len(original).to_bytes(2, 'big')+original)
        restored = card.apdu(bytes.fromhex('00b0000000')+size.to_bytes(2, 'big'))
        if restored != original:
            raise RuntimeError('NDEF restore verification failed')
        print('PASS: original NDEF content restored', flush=True)
finally:
    card.close()
