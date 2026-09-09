#!/usr/bin/env python3
"""Generate PIV RSA keys and verify signing/decryption using yubico-piv-tool.

Overwrites slot 9e. Requires the default PIV management key and slot policy.
"""
import argparse
from datetime import datetime, timedelta, timezone
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, rsa
from cryptography.x509.oid import NameOID
from smartcard.System import readers

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--destructive', action='store_true', required=True,
                    help='allow overwriting the test device PIV slot 9e')
parser.add_argument('--bits', type=int, choices=(2048, 3072, 4096), nargs='+', default=[3072, 4096])
parser.add_argument('--timeout', type=float, default=180, help='seconds per operation')
args = parser.parse_args()
if args.timeout <= 0:
    parser.error('--timeout must be positive')
if not shutil.which('yubico-piv-tool'):
    parser.error('yubico-piv-tool is required')
matched = [str(r) for r in readers() if 'canokey' in str(r).lower()]
if len(matched) != 1:
    raise RuntimeError(f'Expected exactly one CanoKey reader, found {len(matched)}')
command = ['yubico-piv-tool', '-r', matched[0], '-s', '9e']

def run(*options):
    start = time.monotonic()
    result = subprocess.run(command+list(options), capture_output=True, text=True, timeout=args.timeout)
    if result.returncode:
        raise RuntimeError(result.stderr.strip())
    return time.monotonic()-start

with tempfile.TemporaryDirectory(prefix='k230d-piv-rsa-') as directory:
    path = Path(directory)
    for bits in args.bits:
        pubfile, certfile = path/'public.pem', path/'certificate.pem'
        generation = run('-a', 'generate', '-A', f'RSA{bits}', '-o', str(pubfile))
        public = serialization.load_pem_public_key(pubfile.read_bytes())
        if not isinstance(public, rsa.RSAPublicKey) or public.key_size != bits:
            raise RuntimeError('Generated public key has the wrong algorithm or size')
        # The certificate only supplies the generated public key to the host
        # tests. It is signed by a temporary issuer, not installed on the card.
        issuer = ec.generate_private_key(ec.SECP256R1())
        name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'K230D PIV test')])
        now = datetime.now(timezone.utc)
        cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
                .public_key(public).serial_number(x509.random_serial_number())
                .not_valid_before(now-timedelta(minutes=1)).not_valid_after(now+timedelta(days=1))
                .sign(issuer, hashes.SHA256()))
        certfile.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
        signing = run('-a', 'test-signature', '-i', str(certfile))
        decryption = run('-a', 'test-decipher', '-i', str(certfile))
        print(f'PASS: RSA{bits}: generate {generation:.3f}s, '
              f'sign/verify {signing:.3f}s, encrypt/decrypt {decryption:.3f}s', flush=True)
