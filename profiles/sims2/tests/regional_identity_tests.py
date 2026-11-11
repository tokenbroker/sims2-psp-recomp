"""Regional identity selection with synthetic owned-disc stand-ins, not game code."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import prepare


class IdentityTests(unittest.TestCase):
    def check_identity(self, disc, contents, expected, mismatch=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "config").mkdir()
            usa, eu = b"\x7fELFsynthetic-USA", b"\x7fELFsynthetic-Europe"
            config = {"disc_id": "ULUS-10031", "boot_sha256": hashlib.sha256(usa).hexdigest(),
                      "variants": [{"disc_id": "ULES-00211", "boot_sha256": hashlib.sha256(eu).hexdigest()}]}
            (root / "config/identity.json").write_text(json.dumps(config))
            identity = (disc + "|synthetic").encode()
            image = root / "input.iso"
            image.write_bytes(identity.ljust(2048, b"\0") + contents)
            with patch.object(prepare, "PROFILE", root), patch.object(prepare, "disc_files", return_value={
                "/UMD_DATA.BIN": (0, len(identity)), "/PSP_GAME/SYSDIR/BOOT.BIN": (1, len(contents))}):
                if mismatch:
                    with self.assertRaises(ValueError): prepare.prepare(image, False, verify_only=True)
                else:
                    self.assertEqual(prepare.prepare(image, False, verify_only=True)["disc_id"], expected)
            self.assertEqual(set(root.iterdir()), {root / "config", image})

    def test_both_matching_regions(self):
        self.check_identity("ULUS-10031", b"\x7fELFsynthetic-USA", "ULUS-10031")
        self.check_identity("ULES-00211", b"\x7fELFsynthetic-Europe", "ULES-00211")

    def test_disc_label_cannot_unlock_other_regions_code(self):
        self.check_identity("ULES-00211", b"\x7fELFsynthetic-USA", None, mismatch=True)
        self.check_identity("ULUS-10031", b"\x7fELFsynthetic-Europe", None, mismatch=True)

    def test_unsupported_disc(self):
        self.check_identity("OTHER-0000", b"\x7fELFsynthetic-USA", None, mismatch=True)


if __name__ == "__main__":
    unittest.main()
