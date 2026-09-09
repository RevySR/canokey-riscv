#!/usr/bin/env python3
"""Check Console's admin handshake via PC/SC or WebUSB control transfers.

--settings also verifies the factory administrator PIN (123456) once and reads
the configuration. Do not use that flag after changing the administrator PIN.
"""
import argparse
import re
import time
from host_check import Ccid


class WebUsb:
    def __init__(self):
        import usb.core
        import usb.util
        self.util = usb.util
        self.dev = usb.core.find(idVendor=0x20a0, idProduct=0x42d4)
        if self.dev is None:
            raise RuntimeError('CanoKey USB device not found')
        interfaces = [i for i in self.dev.get_active_configuration()
                      if (i.bInterfaceClass, i.bInterfaceSubClass, i.bInterfaceProtocol) == (255, 255, 255)]
        if len(interfaces) != 1:
            raise RuntimeError('Expected one WebUSB vendor interface')
        self.interface = interfaces[0].bInterfaceNumber
        self.util.claim_interface(self.dev, self.interface)
        product = self.util.get_string(self.dev, self.dev.iProduct)
        if product != 'CanoKey K230D Zero':
            raise RuntimeError(f'Unexpected USB product name: {product}')
        header = bytes(self.dev.ctrl_transfer(0x80, 6, 0x0f00, 0, 5, timeout=2000))
        if len(header) != 5 or header[1] != 15:
            raise RuntimeError('Invalid BOS header')
        bos = bytes(self.dev.ctrl_transfer(0x80, 6, 0x0f00, 0, int.from_bytes(header[2:4], 'little'), timeout=2000))
        webusb_uuid = bytes.fromhex('38b60834a909a0478bfda0768815b665')
        if webusb_uuid not in bos:
            raise RuntimeError('WebUSB platform capability missing')
        url = bytes(self.dev.ctrl_transfer(0xc0, 1, 1, 2, 255, timeout=2000))
        if url[2:] != b'\x01console.canokeys.org':
            raise RuntimeError(f'Unexpected WebUSB URL descriptor: {url!r}')
        print('PASS: BOS and WebUSB landing URL', flush=True)

    def close(self):
        self.util.release_interface(self.dev, self.interface)
        self.util.dispose_resources(self.dev)
        # CanoKey intentionally holds the shared APDU buffer for two seconds.
        time.sleep(2.1)

    def apdu(self, command):
        self.dev.ctrl_transfer(0x41, 0, 0, self.interface, command, timeout=2000)
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            status = bytes(self.dev.ctrl_transfer(0xc1, 2, 0, self.interface, 1, timeout=2000))
            if status == b'\0':
                response = bytes(self.dev.ctrl_transfer(0xc1, 1, 0, self.interface, 1340, timeout=2000))
                if response[-2:] != b'\x90\0':
                    raise RuntimeError(f'APDU {command[:4].hex()} status {response[-2:].hex()}')
                return response[:-2]
            time.sleep(0.01)
        raise RuntimeError('WebUSB APDU response timeout')

    def select(self, aid):
        data = bytes.fromhex(aid)
        return self.apdu(bytes([0, 0xa4, 4, 0, len(data)]) + data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--transport', choices=['ccid', 'webusb'], default='ccid')
    parser.add_argument('--settings', action='store_true')
    args = parser.parse_args()
    card = Ccid() if args.transport == 'ccid' else WebUsb()
    try:
        card.select('f000000000')
        serial = card.apdu(bytes.fromhex('0032000000'))
        if len(serial) != 4:
            raise RuntimeError('Invalid Console serial response')
        version = card.apdu(bytes.fromhex('0031000000')).decode('utf-8')
        # Exact prefix rule used by Console FirmwareVersion.parse.
        match = re.match(r'^(\d+)(?:\.(\d+))?(?:\.(\d+))?', version.strip())
        if match is None:
            raise RuntimeError(f'Console parses this version as 0.0.0: {version}')
        parsed = tuple(int(n or 0) for n in match.groups())
        if not (3, 0, 0) <= parsed < (3, 1, 0):
            raise RuntimeError(f'Expected Core 3.0.x / Console function-set v4, got {version}')
        model = card.apdu(bytes.fromhex('0031010000')).decode('utf-8')
        if not model.startswith('CanoKey K230D Zero ('):
            raise RuntimeError(f'Unexpected Console model: {model}')
        card.apdu(bytes.fromhex('0032010000'))  # optional chip ID may be empty
        nfc = card.apdu(bytes.fromhex('0014000000'))
        if nfc != b'\0':
            raise RuntimeError('Expected a one-byte NFC-disabled response')
        print(f'PASS: {args.transport} Console handshake, version={version}, model={model}, NFC=off', flush=True)
        if args.settings:
            card.apdu(bytes.fromhex('0020000006313233343536'))
            config = card.apdu(bytes.fromhex('0042000000'))
            if len(config) < 5:
                raise RuntimeError('Console v4 configuration is truncated')
            print('PASS: Console settings read with v4 configuration layout', flush=True)
    finally:
        card.close()


if __name__ == '__main__':
    main()
