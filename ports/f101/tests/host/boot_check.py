#!/usr/bin/env python3
"""Verify a FIDO credential across two independent SPI boots.

Uses the existing development attestation; does not replace attestation keys.
User presence is explicitly injected over UART for these local requests only.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import threading
from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from fido2 import cbor
from fido2.cose import CoseKey
from fido2.ctap2 import Ctap2
from fido2.hid import CtapHidDevice

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('phase', choices=['prepare', 'verify'])
parser.add_argument('--state', type=Path, required=True)
parser.add_argument('--serial', default='/dev/ttyUSB0')
parser.add_argument('--debug-presence', action='store_true', required=True)
args = parser.parse_args()
devices = [d for d in CtapHidDevice.list_devices() if (d.descriptor.vid, d.descriptor.pid) == (0x20a0, 0x42d4)]
if len(devices) != 1:
    raise RuntimeError('Expected exactly one CanoKey')
cancel = threading.Event()


def presence(status):
    if int(status) == 2:
        fd = os.open(args.serial, os.O_WRONLY | os.O_NOCTTY)
        try:
            os.write(fd, b't')
        finally:
            os.close(fd)


rp = 'f101-spi.test'
challenge = hashlib.sha256(os.urandom(32)).digest()
timer = threading.Timer(30, cancel.set)
timer.start()
try:
    with devices[0] as device:
        ctap = Ctap2(device)
        if args.phase == 'prepare':
            result = ctap.make_credential(challenge, {'id': rp, 'name': 'F101 SPI boot test'},
                                          {'id': b'boot-test',
                                              'name': 'boot-test'}, [{'type': 'public-key', 'alg': -7}],
                                          options={'rk': False}, event=cancel, on_keepalive=presence)
            if result.fmt != 'packed' or not result.auth_data.is_user_present():
                raise RuntimeError('Missing packed attestation or user presence')
            cert = x509.load_der_x509_certificate(result.att_stmt['x5c'][0])
            cert.public_key().verify(result.att_stmt['sig'], bytes(
                result.auth_data)+challenge, ec.ECDSA(hashes.SHA256()))
            cred = result.auth_data.credential_data
            state = {'credential': cred.credential_id.hex(), 'key': cbor.encode(dict(cred.public_key)).hex(),
                     'counter': result.auth_data.counter}
            with args.state.open('x') as out:
                json.dump(state, out)
            print('PASS: SPI boot MakeCredential and attestation signature')
        else:
            state = json.loads(args.state.read_text())
            result = ctap.get_assertion(rp, challenge, [{'type': 'public-key', 'id': bytes.fromhex(state['credential'])}],
                                        event=cancel, on_keepalive=presence)
            CoseKey.parse(cbor.decode(bytes.fromhex(state['key']))).verify(
                bytes(result.auth_data)+challenge, result.signature)
            if not result.auth_data.is_user_present() or result.auth_data.counter <= state['counter']:
                raise RuntimeError('Missing user presence or counter advancement')
            print('PASS: persisted FIDO credential, signature and counter after SPI reboot')
finally:
    timer.cancel()
