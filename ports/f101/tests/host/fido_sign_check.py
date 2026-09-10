#!/usr/bin/env python3
"""Provision unique development attestation and test local FIDO2 signing.

This writes a development attestation key/certificate using the default admin
PIN. --debug-presence explicitly grants the two local test requests via UART.
No account or external relying party is contacted.
"""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
from pathlib import Path
import threading
import time
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID
from fido2.hid import CtapHidDevice
from fido2.ctap2 import Ctap2
import os
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "k230d/tests/host"))

from host_check import Ccid  # noqa: E402

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--debug-presence', action='store_true', required=True)
parser.add_argument('--serial', default='/dev/ttyUSB0')
args = parser.parse_args()

key = ec.generate_private_key(ec.SECP256R1())
subject = x509.Name([
    x509.NameAttribute(NameOID.COUNTRY_NAME, 'CN'),
    x509.NameAttribute(NameOID.ORGANIZATION_NAME, 'F101 Development'),
    x509.NameAttribute(NameOID.ORGANIZATIONAL_UNIT_NAME, 'Authenticator Attestation'),
    x509.NameAttribute(NameOID.COMMON_NAME, 'Development CanoKey F101'),
])
now = datetime.now(timezone.utc)
cert = (x509.CertificateBuilder().subject_name(subject).issuer_name(subject)
        .public_key(key.public_key()).serial_number(x509.random_serial_number())
        .not_valid_before(now-timedelta(days=1)).not_valid_after(now+timedelta(days=3650))
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .sign(key, hashes.SHA256()))
der = cert.public_bytes(serialization.Encoding.DER)
card = Ccid()
try:
    card.select('f000000000')
    card.apdu(bytes.fromhex('0020000006313233343536'))
    card.apdu(bytes([0, 1, 0, 0, 32])+key.private_numbers().private_value.to_bytes(32, 'big'))
    card.apdu(bytes([0, 2, 0, 0, 0])+len(der).to_bytes(2, 'big')+der)
finally:
    card.close()
print('Provisioned unique self-signed development attestation (private key not logged)', flush=True)


def presence(timing=None):
    sent = False

    def keepalive(status):
        nonlocal sent
        if int(status) == 2 and not sent:
            start = time.perf_counter()
            fd = os.open(args.serial, os.O_WRONLY | os.O_NOCTTY)
            try:
                os.write(fd, b't')
            finally:
                os.close(fd)
            if timing is not None:
                timing['debug_confirmation_ms'] += (time.perf_counter()-start)*1000
            sent = True
    return keepalive


device = next(d for d in CtapHidDevice.list_devices() if (d.descriptor.vid, d.descriptor.pid) == (0x20a0, 0x42d4))
with device:
    ctap = Ctap2(device)
    client_hash = hashlib.sha256(b'F101 local registration test').digest()
    cancel = threading.Event()
    timer = threading.Timer(60, cancel.set)
    timer.start()
    try:
        result = ctap.make_credential(client_hash,
                                      {'id': 'f101.test', 'name': 'F101 local test'},
                                      {'id': b'f101-test-user', 'name': 'test'},
                                      [{'type': 'public-key', 'alg': -7}],
                                      options={'rk': False}, event=cancel, on_keepalive=presence())
    finally:
        timer.cancel()
    if result.fmt != 'packed':
        raise RuntimeError(f'Unexpected attestation format {result.fmt}')
    cert.public_key().verify(result.att_stmt['sig'], bytes(result.auth_data)+client_hash, ec.ECDSA(hashes.SHA256()))
    credential = result.auth_data.credential_data
    if credential is None or not result.auth_data.is_user_present():
        raise RuntimeError('Missing credential or user-presence flag')
    print('PASS: FIDO2 MakeCredential and attestation ECDSA signature', flush=True)
    assertion_hash = hashlib.sha256(b'F101 local assertion test').digest()
    cancel.clear()
    timer = threading.Timer(60, cancel.set)
    timer.start()
    try:
        assertion = ctap.get_assertion('f101.test', assertion_hash,
                                       [{'type': 'public-key', 'id': credential.credential_id}],
                                       event=cancel, on_keepalive=presence())
    finally:
        timer.cancel()
    credential.public_key.verify(bytes(assertion.auth_data)+assertion_hash, assertion.signature)
    if not assertion.auth_data.is_user_present():
        raise RuntimeError('Missing assertion user-presence flag')
    print('PASS: FIDO2 GetAssertion and credential ECDSA signature', flush=True)
