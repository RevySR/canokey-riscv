#!/usr/bin/env python3
"""Test FIDO2 PIN protocols, resident credentials and fragmented Large Blobs.

Resets the FIDO applet before and after testing. Uses the default admin PIN.
"""
import argparse
import hashlib
import runpy
import sys
import threading
from pathlib import Path
from fido2.ctap import CtapError
from fido2.ctap2 import Ctap2, ClientPin, CredentialManagement, LargeBlobs
from fido2.ctap2.pin import PinProtocolV1, PinProtocolV2
from fido2.hid import CtapHidDevice
from support import ROOT
from host_check import Ccid
parser = argparse.ArgumentParser(description='Destructive FIDO2 PIN, resident credential and Large Blob tests')
parser.add_argument('--destructive', action='store_true', required=True)
parser.add_argument('--debug-presence', action='store_true', required=True)
parser.add_argument('--elf', type=Path, default=ROOT / 'build/canokey-k230d-sd.elf')
args = parser.parse_args()

def reset():
    c = Ccid()
    try:
        c.select('f000000000')
        c.apdu(bytes.fromhex('0020000006313233343536'))
        c.apdu(bytes.fromhex('00090000'))
    finally:
        c.close()

def call(fn, *args, **kw):
    event = threading.Event()
    timer = threading.Timer(45, event.set)
    timer.start()
    try:
        return fn(*args, event=event, on_keepalive=fixture['presence'](), **kw)
    finally:
        timer.cancel()
reset()
try:
    old_argv = sys.argv
    try:
        sys.argv = [str(ROOT / 'tests/host/fido_sign_check.py'), '--debug-presence', '--elf', str(args.elf)]
        fixture = runpy.run_path(sys.argv[0], run_name='__main__')
    finally:
        sys.argv = old_argv
    device = next((d for d in CtapHidDevice.list_devices() if (d.descriptor.vid, d.descriptor.pid) == (8352, 17108)))
    with device:
        ctap = Ctap2(device)
        cp = ClientPin(ctap, PinProtocolV2())
        cp.set_pin('123456')
        assert ctap.get_info().options['clientPin']
        for protocol in (PinProtocolV1(), PinProtocolV2()):
            client = ClientPin(ctap, protocol)
            token = client.get_pin_token('123456')
            assert len(token) >= 16
        print('PASS: PIN protocols 1 and 2', flush=True)
        try:
            cp.get_pin_token('654321')
        except CtapError as e:
            assert e.code == CtapError.ERR.PIN_INVALID, e
        else:
            raise AssertionError('Wrong PIN accepted')
        cp.get_pin_token('123456')
        assert cp.get_pin_retries()[0] == 8
        print('PASS: wrong PIN and retry recovery', flush=True)
        rp = 'k230d-suite.test'
        credentials = []
        for i in range(3):
            digest = hashlib.sha256(b'extended-create' + bytes([i])).digest()
            token = cp.get_pin_token('123456', ClientPin.PERMISSION.MAKE_CREDENTIAL, rp)
            result = call(ctap.make_credential, digest, {'id': rp, 'name': 'K230D test'}, {'id': bytes([i]), 'name': f'user{i}', 'displayName': f'Test user {i}'}, [{'type': 'public-key', 'alg': -7}], options={'rk': True}, extensions={'largeBlobKey': True}, pin_uv_param=cp.protocol.authenticate(token, digest), pin_uv_protocol=2)
            credential = result.auth_data.credential_data
            fixture['cert'].public_key().verify(result.att_stmt['sig'], bytes(result.auth_data) + digest, fixture['ec'].ECDSA(fixture['hashes'].SHA256()))
            assert result.auth_data.is_user_verified() and result.large_blob_key
            credentials.append((credential, result.large_blob_key))
        digest = hashlib.sha256(b'extended-discover').digest()
        token = cp.get_pin_token('123456', ClientPin.PERMISSION.GET_ASSERTION, rp)
        first = call(ctap.get_assertion, rp, digest, pin_uv_param=cp.protocol.authenticate(token, digest), pin_uv_protocol=2)
        assert first.number_of_credentials == 3
        responses = [first] + [ctap.get_next_assertion() for _ in range(2)]
        for result in responses:
            key = next((c.public_key for c, _ in credentials if c.credential_id == result.credential['id']))
            key.verify(bytes(result.auth_data) + digest, result.signature)
            assert result.auth_data.is_user_verified()
        print('PASS: 3 resident credentials, discovery/GetNextAssertion and signatures', flush=True)
        token = cp.get_pin_token('123456', ClientPin.PERMISSION.CREDENTIAL_MGMT)
        manager = CredentialManagement(ctap, cp.protocol, token)
        assert manager.get_metadata()[CredentialManagement.RESULT.EXISTING_CRED_COUNT] == 3
        rps = manager.enumerate_rps()
        assert len(rps) == 1
        entries = manager.enumerate_creds(hashlib.sha256(rp.encode()).digest())
        assert len(entries) == 3
        print('PASS: credential metadata and enumeration', flush=True)
        token = cp.get_pin_token('123456', ClientPin.PERMISSION.LARGE_BLOB_WRITE)
        blobs = LargeBlobs(ctap, cp.protocol, token)
        data = b''.join((hashlib.sha256(b'blob' + i.to_bytes(4, 'big')).digest() for i in range(64)))
        writes = []
        original = ctap.large_blobs

        def traced(*args, **kwargs):
            if kwargs.get('set') is not None:
                writes.append((args[0], len(kwargs['set'])))
            return original(*args, **kwargs)
        ctap.large_blobs = traced
        blobs.put_blob(credentials[0][1], data)
        assert len(writes) >= 2 and writes[0][0] == 0 and (writes[1][0] > 0), writes
        print('PASS: fragmented Large Blob writes ' + str(writes), flush=True)
        assert blobs.get_blob(credentials[0][1]) == data
        blobs.delete_blob(credentials[0][1])
        assert blobs.get_blob(credentials[0][1]) is None
        print('PASS: 2048-byte Large Blob write/read/delete', flush=True)
        token = cp.get_pin_token('123456', ClientPin.PERMISSION.CREDENTIAL_MGMT)
        manager = CredentialManagement(ctap, cp.protocol, token)
        for credential, _ in credentials:
            manager.delete_cred({'type': 'public-key', 'id': credential.credential_id})
        assert manager.get_metadata()[CredentialManagement.RESULT.EXISTING_CRED_COUNT] == 0
        cp.change_pin('123456', '654321')
        cp.get_pin_token('654321')
        print('PASS: credential deletion and PIN change', flush=True)
finally:
    reset()
    print('FIDO test data and PIN reset', flush=True)
