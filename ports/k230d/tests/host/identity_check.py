#!/usr/bin/env python3
"""Check that USB/admin/OpenPGP/PIV expose the same chip-derived serial."""
import argparse
import json
import usb.core
import usb.util
from host_check import Ccid

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--expected-id', help='expected full SHA-256 identity in hex')
parser.add_argument('--output', help='write public identity results as JSON')
args=parser.parse_args()
card=Ccid()
try:
    card.select('f000000000')
    serial=card.apdu(bytes.fromhex('0032000000'))
    chip=card.apdu(bytes.fromhex('0032010000'))
    if len(serial)!=4 or len(chip)!=32 or serial!=chip[:4]:
        raise RuntimeError('Admin identity size or digest prefix mismatch')
    if args.expected_id and chip!=bytes.fromhex(args.expected_id):
        raise RuntimeError('Chip identity changed or derivation mismatch')
    card.select('d27600012401')
    aid=card.apdu(bytes.fromhex('00ca004f00'))
    if len(aid)!=16 or aid[10:14]!=serial:
        raise RuntimeError('OpenPGP AID serial mismatch')
    card.select('a000000308000010000100')
    if card.apdu(bytes.fromhex('00f8000000'))!=serial:
        raise RuntimeError('PIV serial mismatch')
finally:
    card.close()
dev=usb.core.find(idVendor=0x20a0,idProduct=0x42d4)
if dev is None:
    raise RuntimeError('USB device missing')
try:
    usb_serial=usb.util.get_string(dev,dev.iSerialNumber)
    if usb_serial!=serial.hex().upper():
        raise RuntimeError('USB serial mismatch')
finally:
    usb.util.dispose_resources(dev)
report={'serial':usb_serial,'derived_chip_id':chip.hex().upper(),
        'usb_admin_openpgp_piv_match':True}
print(json.dumps(report,indent=2))
if args.output:
    from pathlib import Path
    Path(args.output).write_text(json.dumps(report,indent=2)+'\n')
