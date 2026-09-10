"""Offline checks for image validation, FEL selection and RNG recovery."""

import contextlib
import hashlib
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import install_boot  # noqa: E402
import load_firmware  # noqa: E402
import read_nor  # noqa: E402


def boot_image():
    spl = bytearray(512)
    spl[4:12] = b"eGON.BT0"
    struct.pack_into("<II", spl, 12, 0x5F0A6C39, len(spl))
    struct.pack_into("<I", spl, 12, sum(struct.unpack("<128I", spl)) & 0xFFFFFFFF)
    app = bytes(range(256))
    header = struct.pack("<8I", 0x31464B43, 1, len(app), 0x40010000, zlib.crc32(app), 0, 0, 0)
    image = spl + b"\xff" * (0x10000 - len(spl)) + header + app
    return image + b"\xff" * (-len(image) % 4096)


class ImageTests(unittest.TestCase):
    def test_valid_image(self):
        install_boot.validate_image(boot_image())

    def test_reject_corruption(self):
        for offset in (4, 12, 0x10000, 0x10004, 0x1000C, 0x10010, 0x10020):
            with self.subTest(offset=offset):
                image = boot_image()
                image[offset] ^= 1
                with self.assertRaises(ValueError):
                    install_boot.validate_image(image)

    def test_reject_invalid_lengths(self):
        for length in (0, 0x70000, 0x2000):
            with self.subTest(length=length):
                image = boot_image()
                struct.pack_into("<I", image, 0x10008, length)
                with self.assertRaises(ValueError):
                    install_boot.validate_image(image)
        for size in (0, 64, 0x10000, len(boot_image()) - 1):
            with self.subTest(size=size), self.assertRaises(ValueError):
                install_boot.validate_image(boot_image()[:size])


class FelTests(unittest.TestCase):
    def test_backup_rejects_other_chip_before_writes(self):
        with patch.object(sys, "argv", ["read_nor.py", "reader.bin", "backup.bin", "--length", "4096"]):
            with patch.object(load_firmware, "run", return_value=b"ID=0x00185900(V851S)") as loader:
                with patch.object(read_nor, "run") as reader, self.assertRaises(SystemExit):
                    read_nor.main()
                loader.assert_called_once_with("version")
                reader.assert_not_called()


class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.work = Path(self.directory.name)
        self.image = boot_image()
        (self.work / "firmware.img").write_bytes(self.image)
        for name in ("f101-nor-install.bin", "f101-psram-init.bin"):
            (self.work / name).write_bytes(b"target")
        self.flash = bytearray(b"\xff" * 0x1000000)
        self.flash[0xFC0000:] = b"\x5a" * 0x40000
        self.memory = {}
        self.operations = []
        self.fail_at = None
        with (self.work / "backup.bin").open("wb") as backup:
            backup.truncate(len(self.flash))

    def run_fel(self, *args):
        if args[0] == "version":
            return b"ID=0x00193700(F101)"
        op, address = args[0], int(args[1], 0)
        if op == "write":
            self.memory[address] = Path(args[2]).read_bytes()
        elif op == "read":
            Path(args[3]).write_bytes(self.memory[address][:args[2]])
        elif op == "exec" and address == 0x28000:
            self.assertEqual(self.memory[0x27F00], bytes(16))
            self.memory[0x27F00] = struct.pack("<4I", 0x5053524D, 0x1000000, 0, 0)
        elif op == "exec" and address == 0x20000:
            magic, command, offset, length, _ = struct.unpack("<5I", self.memory[0x27F00])
            self.assertEqual(magic, 0x49313046)
            self.assertTrue(offset + length <= 0x80000 or 0xFBE000 <= offset < offset + length <= 0xFC0000)
            if command in (1, 2):
                self.operations.append((command, offset, length))
                if self.fail_at == len(self.operations):
                    self.memory[0x27F10] = b"\xff" * 4
                    return b""
            if command == 1:
                self.flash[offset:offset + length] = b"\xff" * length
            elif command == 2:
                data = self.memory[0x40000000]
                self.assertEqual(len(data), length)
                for i, value in enumerate(data):
                    self.flash[offset + i] &= value
            elif command == 3:
                self.memory[0x40000000] = bytes(self.flash[offset:offset + length])
            else:
                self.fail(f"Unexpected Flash command {command}")
            self.memory[0x27F10] = bytes(4)
        else:
            self.fail(f"Unexpected FEL command {args}")
        return b""

    def install(self, recover=False):
        argv = ["install_boot.py", str(self.work / "firmware.img"), "--backup", str(self.work / "backup.bin")]
        if recover:
            argv.append("--recover-rng")
        with patch.object(sys, "argv", argv), contextlib.redirect_stdout(io.StringIO()):
            with patch.object(install_boot, "run", side_effect=self.run_fel):
                with patch.object(load_firmware, "run", side_effect=self.run_fel):
                    install_boot.main()

    def assert_seed_valid(self):
        record = self.flash[0xFBE000:0xFBE060]
        self.assertEqual(struct.unpack_from("<3I", record), (0x31474E52, 0, 0xFFFFFFFF))
        self.assertEqual(record[60:92], hashlib.sha256(record[4:8] + record[12:60]).digest())
        self.assertEqual(self.flash[0xFBF000:0xFC0000], b"\xff" * 4096)

    def test_preserve_existing_journal(self):
        self.flash[0xFBE000:0xFC0000] = b"\xa5" * 8192
        before = bytes(self.flash)
        self.install()
        self.assertEqual(self.flash[:len(self.image)], self.image)
        self.assertEqual(self.flash[len(self.image):], before[len(self.image):])
        self.assertFalse(list(self.work.glob("seed-journal-before-*.bin")))

    def test_provision_blank_journal(self):
        self.install()
        self.assert_seed_valid()

    def test_explicit_recovery_preserves_credentials(self):
        self.flash[0xFBE000:0xFC0000] = b"\xa5" * 8192
        before = bytes(self.flash)
        self.install(recover=True)
        self.assert_seed_valid()
        self.assertEqual(self.flash[0xFC0000:], before[0xFC0000:])
        self.assertEqual(self.flash[len(self.image):0xFBE000], before[len(self.image):0xFBE000])
        backup = next(self.work.glob("seed-journal-before-*.bin"))
        self.assertEqual(backup.read_bytes(), before[0xFBE000:0xFC0000])
        self.assertEqual(backup.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.operations[:4], [(1, 0xFBE000, 4096), (1, 0xFBF000, 4096),
                                               (2, 0xFBE004, 92), (2, 0xFBE000, 4)])

    def test_failed_recovery_stops_before_firmware_write(self):
        self.flash[0xFBE000:0xFC0000] = b"\xa5" * 8192
        before = bytes(self.flash)
        for fail_at in (1, 2, 3, 4):
            with self.subTest(fail_at=fail_at):
                self.flash[:] = before
                self.operations.clear()
                self.fail_at = fail_at
                with self.assertRaises(RuntimeError):
                    self.install(recover=True)
                self.assertEqual(self.flash[:0xFBE000], before[:0xFBE000])
                self.assertEqual(self.flash[0xFC0000:], before[0xFC0000:])
                self.assertNotEqual(self.flash[0xFBE000:0xFBE004], struct.pack("<I", 0x31474E52))

    def test_bad_image_never_contacts_device(self):
        self.image[0x10020] ^= 1
        (self.work / "firmware.img").write_bytes(self.image)
        with patch.object(install_boot, "initialize_psram") as init, contextlib.redirect_stderr(io.StringIO()):
            with patch.object(install_boot, "run") as run:
                argv = ["install_boot.py", str(self.work / "firmware.img"), "--backup", str(self.work / "backup.bin")]
                with patch.object(sys, "argv", argv), self.assertRaises(SystemExit):
                    install_boot.main()
                run.assert_not_called()
                init.assert_not_called()


if __name__ == "__main__":
    unittest.main()
