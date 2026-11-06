"""Preflight must detect missing codecs even when development libraries exist."""
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import doctor


class DoctorTests(unittest.TestCase):
    def check_linux(self, missing=()):
        find = Mock(side_effect=lambda name: 0 if name.decode() in missing else 1)
        with patch.object(doctor, "os", SimpleNamespace(name="posix")), \
             patch.object(doctor.shutil, "which", side_effect=lambda name: None if name=="glslangValidator" else "/test/"+name), \
             patch.object(doctor.subprocess, "run", return_value=SimpleNamespace(returncode=0, stdout="1.0")), \
             patch.object(doctor.ctypes.util, "find_library", return_value="test-avcodec"), \
             patch.object(doctor.ctypes, "CDLL", return_value=SimpleNamespace(avcodec_find_decoder_by_name=find)):
            return doctor.check()

    def test_new_glslang_name(self):
        self.assertTrue(self.check_linux()["ok"])

    def test_missing_required_decoders(self):
        for name in ("h264", "atrac3plus"):
            with self.subTest(decoder=name):
                result=self.check_linux({name})
                self.assertFalse(result["ok"])
                self.assertTrue(any(c["name"]=="FFmpeg decoder "+name and not c["ok"] for c in result["checks"]))

    def test_baseline_atrac_is_reported_separately(self):
        result=self.check_linux({"atrac3"})
        self.assertTrue(result["ok"])
        self.assertTrue(any(c["name"]=="FFmpeg decoder atrac3" and not c["ok"] and not c["required"] for c in result["checks"]))


if __name__=="__main__":
    unittest.main()
