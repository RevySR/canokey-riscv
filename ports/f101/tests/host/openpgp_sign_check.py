#!/usr/bin/env python3
"""Verify OpenPGP Ed25519 signing; generate a key only in an empty slot."""
import argparse
import os
from pathlib import Path
import sys
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
sys.path.insert(0, str(Path(__file__).resolve().parents[3]/'k230d/tests/host'))
from host_check import Ccid  # noqa: E402
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--generate-if-empty', action='store_true')
args = parser.parse_args()
card = Ccid()
try:
    card.select('d27600012401')
    public, a, b = card.connection.transmit(list(bytes.fromhex('0047810002b60000')))
    if (a, b) == (0x6a, 0x88) and args.generate_if_empty:
        card.apdu(bytes.fromhex('00200083083132333435363738'))
        card.apdu(bytes.fromhex('00da00c10a162b06010401da470f01'))
        card.apdu(bytes.fromhex('0047800002b60000'))
        public = card.apdu(bytes.fromhex('0047810002b60000'))
        print('Generated Ed25519 test key in previously empty signing slot', flush=True)
    elif (a, b) != (0x90, 0):
        raise RuntimeError(f'Cannot read signing key: {a:02x}{b:02x}')
    public = bytes(public)
    if len(public) != 37 or public[:5] != bytes.fromhex('7f49228620'):
        raise RuntimeError('Signing slot is not Ed25519; left unchanged')
    key = Ed25519PublicKey.from_public_bytes(public[5:])
    message = os.urandom(32)
    signatures = []
    for _ in range(3):
        card.apdu(bytes.fromhex('0020008106313233343536'))
        sig = card.apdu(bytes([0, 0x2a, 0x9e, 0x9a, len(message)])+message+b'\0')
        key.verify(sig, message)
        signatures.append(sig)
    if len(set(signatures)) != 1:
        raise RuntimeError('Ed25519 signatures were not deterministic')
    print('PASS: OpenPGP Ed25519 signing, host verification and repeatability')
finally:
    card.close()
