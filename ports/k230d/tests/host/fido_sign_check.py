#!/usr/bin/env python3
"""Provision unique development attestation and test local FIDO2 signing.

This writes a development attestation key/certificate using the default admin
PIN. --debug-presence explicitly grants the two local test requests via JTAG.
No account or external relying party is contacted.
"""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
import math
from pathlib import Path
import socket
import subprocess
import threading
import statistics
import time
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID
from fido2.hid import CtapHidDevice
from fido2.ctap2 import Ctap2
from host_check import Ccid
from support import ROOT, run

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--debug-presence', action='store_true', required=True)
parser.add_argument('--elf', type=Path, default=ROOT/'build/canokey-k230d-sd.elf')
parser.add_argument('--benchmark', type=int, default=0, metavar='N',
                    help='time N additional ES256 assertions (1..100); requires profiling firmware')
parser.add_argument('--output', type=Path, help='write timing statistics as JSON')
args = parser.parse_args()
if not 0 <= args.benchmark <= 100:
    parser.error('--benchmark must be between 1 and 100 when enabled')
if args.output and not args.benchmark:
    parser.error('--output requires --benchmark')

key = ec.generate_private_key(ec.SECP256R1())
subject = x509.Name([
    x509.NameAttribute(NameOID.COUNTRY_NAME, 'CN'),
    x509.NameAttribute(NameOID.ORGANIZATION_NAME, 'K230D Development'),
    x509.NameAttribute(NameOID.ORGANIZATIONAL_UNIT_NAME, 'Authenticator Attestation'),
    x509.NameAttribute(NameOID.COMMON_NAME, 'Development CanoKey K230D'),
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

symbols = subprocess.check_output(['riscv64-unknown-elf-nm', str(args.elf)], text=True)
address = next(int(line.split()[0],16) for line in symbols.splitlines() if line.endswith(' B firmware_state'))+56

def presence(timing=None):
    sent = False
    def keepalive(status):
        nonlocal sent
        if int(status) == 2 and not sent:
            start = time.perf_counter()
            with socket.create_connection(('127.0.0.1', 6666), timeout=5) as sock:
                for command in ['halt', f'mww {address} 1', 'resume']:
                    run(sock, command, verbose=False)
            if timing is not None:
                timing['debug_confirmation_ms'] += (time.perf_counter()-start)*1000
            sent = True
    return keepalive

device = next(d for d in CtapHidDevice.list_devices() if (d.descriptor.vid, d.descriptor.pid)==(0x20a0,0x42d4))
with device:
    ctap = Ctap2(device)
    client_hash = hashlib.sha256(b'K230D local registration test').digest()
    cancel = threading.Event()
    timer = threading.Timer(60, cancel.set)
    timer.start()
    try:
        result = ctap.make_credential(client_hash,
            {'id': 'k230d.test', 'name': 'K230D local test'},
            {'id': b'k230d-test-user', 'name': 'test'},
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
    assertion_hash = hashlib.sha256(b'K230D local assertion test').digest()
    cancel.clear()
    timer = threading.Timer(60, cancel.set)
    timer.start()
    try:
        assertion = ctap.get_assertion('k230d.test', assertion_hash,
            [{'type': 'public-key', 'id': credential.credential_id}],
            event=cancel, on_keepalive=presence())
    finally:
        timer.cancel()
    credential.public_key.verify(bytes(assertion.auth_data)+assertion_hash, assertion.signature)
    if not assertion.auth_data.is_user_present():
        raise RuntimeError('Missing assertion user-presence flag')
    print('PASS: FIDO2 GetAssertion and credential ECDSA signature', flush=True)
    if args.benchmark:
        profile_address = next(int(line.split()[0],16) for line in symbols.splitlines()
                               if line.endswith(' B signature_profile'))
        def profile():
            with socket.create_connection(('127.0.0.1',6666),timeout=5) as sock:
                run(sock,'halt',verbose=False)
                try:
                    return [int(x,0) for x in run(sock,
                        f'read_memory {profile_address} 64 6',verbose=False).split()]
                finally:
                    run(sock,'resume',verbose=False)
        samples=[]
        for i in range(args.benchmark):
            before=profile()
            digest=hashlib.sha256(b'K230D signature benchmark'+i.to_bytes(4,'big')).digest()
            timing={'debug_confirmation_ms':0.0}
            cancel.clear()
            timer=threading.Timer(60,cancel.set)
            timer.start()
            try:
                start=time.perf_counter()
                response=ctap.get_assertion('k230d.test',digest,
                    [{'type':'public-key','id':credential.credential_id}],
                    event=cancel,on_keepalive=presence(timing))
                host_ms=(time.perf_counter()-start)*1000
            finally:
                timer.cancel()
            credential.public_key.verify(bytes(response.auth_data)+digest,response.signature)
            if not response.auth_data.is_user_present():
                raise RuntimeError('Benchmark response lacks user presence')
            after=profile()
            if after[0]!=before[0]+1:
                raise RuntimeError('Expected exactly one measured signing call per assertion')
            samples.append({'device_sign_ms':after[2]/27000,
                            'host_roundtrip_ms':host_ms,**timing})
        def summary(field):
            values=sorted(s[field] for s in samples)
            return {'min':min(values),'median':statistics.median(values),
                    'mean':statistics.mean(values),'p95':values[math.ceil(len(values)*0.95)-1],
                    'max':max(values)}
        report={'algorithm':'FIDO2 ES256 / ECDSA P-256 + SHA-256',
                'samples':samples,'count':len(samples),'all_signatures_verified':True,
                'device_scope':'ecc_sign elapsed time, excludes user-presence wait and JTAG reads',
                'host_scope':'GetAssertion roundtrip, includes USB, storage and debug confirmation',
                'device_sign_ms':summary('device_sign_ms'),
                'host_roundtrip_ms':summary('host_roundtrip_ms'),
                'debug_confirmation_ms':summary('debug_confirmation_ms')}
        print(json.dumps({k:v for k,v in report.items() if k!='samples'},indent=2),flush=True)
        if args.output:
            args.output.write_text(json.dumps(report,indent=2)+'\n')
