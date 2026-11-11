"""Metadata inspection using an independently written, synthetic ELF fixture."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from inspect_disc import inspect


class InspectionTests(unittest.TestCase):
    def fixture(self):
        boot = bytearray(100)
        boot[:6] = b"\x7fELF\x01\x01"
        struct.pack_into("<HHIII", boot, 16, 2, 8, 1, 0x08804000, 52)
        struct.pack_into("<HH", boot, 42, 32, 1)
        struct.pack_into("<IIIIIIII", boot, 52, 1, 84, 0x08804000, 0, 16, 16, 5, 4)
        return boot

    def run_inspect(self, boot):
        with tempfile.TemporaryDirectory() as folder:
            image = Path(folder) / "private-disc-location.iso"
            identity = b"ULES-00000|synthetic"
            image.write_bytes(identity.ljust(2048, b"\0") + boot)
            with patch("inspect_disc.disc_files", return_value={
                "/UMD_DATA.BIN": (0, len(identity)),
                "/PSP_GAME/SYSDIR/BOOT.BIN": (1, len(boot)),
            }):
                return inspect(image)

    def test_metadata_has_identity_without_raw_data_or_private_path(self):
        boot = self.fixture()
        data = self.run_inspect(boot)
        self.assertEqual(data["boot_sha256"], hashlib.sha256(boot).hexdigest())
        self.assertEqual(data["disc_id"], "ULES-00000")
        self.assertEqual(data["load_segments"][0]["address"], "0x08804000")
        self.assertNotIn("private-disc-location", str(data))
        self.assertTrue(all(not isinstance(value, (bytes, bytearray)) for value in data.values()))

    def test_invalid_load_extent_is_rejected(self):
        boot = self.fixture()
        struct.pack_into("<I", boot, 56, 1000)
        with self.assertRaisesRegex(ValueError, "load segment"):
            self.run_inspect(boot)

    def test_encrypted_or_non_elf_image_is_rejected(self):
        boot = self.fixture()
        boot[:4] = b"~PSP"
        with self.assertRaisesRegex(ValueError, "unencrypted"):
            self.run_inspect(boot)


if __name__ == "__main__":
    unittest.main()
