#!/usr/bin/env python3
"""USB/CTAP/CCID checks; --oath-test writes and deletes a public HOTP vector."""
import argparse
import time
from smartcard.Exceptions import CardConnectionException
from smartcard.System import readers
from fido2.hid import CtapHidDevice
from fido2.ctap1 import Ctap1
from fido2.ctap2 import Ctap2


class Ccid:
    def __init__(self):
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            matches = [r for r in readers() if 'canokey' in str(r).lower()]
            if len(matches) > 1:
                raise RuntimeError(f'Expected one CanoKey PC/SC reader, found {matches}')
            if matches:
                connection = matches[0].createConnection()
                try:
                    connection.connect()
                    self.connection = connection
                    return
                except CardConnectionException:
                    connection.disconnect()
            time.sleep(0.2)
        raise RuntimeError('CanoKey PC/SC reader did not become ready in 30 seconds')

    def close(self):
        self.connection.disconnect()

    def apdu(self, command):
        data, sw1, sw2 = self.connection.transmit(list(command))
        if (sw1, sw2) != (0x90, 0):
            raise RuntimeError(f'APDU header {command[:4].hex()} failed: {sw1:02x}{sw2:02x}')
        return bytes(data)

    def select(self, aid):
        aid = bytes.fromhex(aid)
        return self.apdu(bytes([0, 0xa4, 4, 0, len(aid)]) + aid)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oath-test', action='store_true')
    args = parser.parse_args()
    found = False
    devices = []
    deadline = time.monotonic() + 30
    while not devices and time.monotonic() < deadline:
        for dev in CtapHidDevice.list_devices():
            if (dev.descriptor.vid, dev.descriptor.pid) == (0x20a0, 0x42d4):
                devices.append(dev)
            else:
                dev.close()
        if not devices:
            time.sleep(0.2)
    for dev in devices:
        if (dev.descriptor.vid, dev.descriptor.pid) != (0x20a0, 0x42d4):
            continue
        found = True
        with dev:
            payload = bytes(range(251)) * 4
            if dev.ping(payload) != payload:
                raise RuntimeError('CTAPHID PING mismatch')
            print('PASS: 1004-byte CTAPHID PING', flush=True)
            version = Ctap1(dev).get_version()
            if version != 'U2F_V2':
                raise RuntimeError(f'Unexpected U2F version: {version}')
            print('PASS: U2F VERSION', version, flush=True)
            info = Ctap2(dev).get_info()
            if 'FIDO_2_1' not in info.versions:
                raise RuntimeError(f'Unexpected FIDO2 versions: {info.versions}')
            print('PASS: FIDO2 GetInfo', info.versions, flush=True)
    if not found:
        raise RuntimeError('CanoKey HID not found or inaccessible')
    card = Ccid()
    try:
        atr = bytes(card.connection.getATR())
        if not atr.startswith(b'\x3b'):
            raise RuntimeError(f'Invalid ATR: {atr.hex()}')
        print('PASS: CCID power on, ATR', atr.hex(), flush=True)
        card.select('f000000000')
        print('PASS: admin version', card.apdu(bytes.fromhex('0031000000')).decode(), flush=True)
        for name, aid in [('OpenPGP', 'd27600012401'), ('PIV', 'a000000308'), ('OATH', 'a0000005272101')]:
            card.select(aid)
            print('PASS: SELECT', name, flush=True)
        if args.oath_test:
            # RFC 4226 public key. CanoKey increments the stored counter before use.
            name = b'k230d-rfc4226-test'
            name_tlv = bytes([0x71, len(name)]) + name
            key = b'12345678901234567890'
            data = name_tlv + bytes([0x73, len(key) + 2, 0x11, 6]) + key
            card.apdu(bytes([0, 1, 0, 0, len(data)]) + data)
            try:
                for counter, expected in [(1, 287082), (2, 359152)]:
                    result = card.apdu(bytes([0, 0xa2, 0, 1, len(name_tlv)]) + name_tlv)
                    if len(result) != 7 or result[:3] != bytes([0x76, 5, 6]):
                        raise RuntimeError(f'Unexpected OATH result: {result.hex()}')
                    code = int.from_bytes(result[3:], 'big') % 1000000
                    if code != expected:
                        raise RuntimeError(f'HOTP counter {counter}: expected {expected}, got {code}')
                    print(f'PASS: OATH HOTP RFC 4226 counter {counter} = {expected}', flush=True)
            finally:
                card.apdu(bytes([0, 2, 0, 0, len(name_tlv)]) + name_tlv)
    finally:
        card.close()


if __name__ == '__main__':
    main()
