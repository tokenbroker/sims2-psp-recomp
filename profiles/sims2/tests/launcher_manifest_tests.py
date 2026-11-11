import json
from pathlib import Path
import sys
import unittest
from copy import deepcopy

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from launcher_manifest import add_europe_settings


class LauncherSettingsTests(unittest.TestCase):
    def test_separate_paths_and_config_bindings(self):
        manifest = json.loads((Path(__file__).resolve().parents[1] / "launcher/recomplauncher.json").read_text())
        original = deepcopy(manifest)
        add_europe_settings(manifest)
        self.assertEqual(manifest["config"][0], original["config"][0])
        self.assertTrue(manifest["config"][1]["file"][0]["path"].endswith("/settings-europe.ini"))
        ids = [item["id"] for item in manifest["settings"]]
        self.assertEqual(len(ids), len(set(ids)))
        for item in manifest["settings"]:
            if item["id"].startswith("europe_"): self.assertEqual(item["in"], "europe")
        language = next(item for item in manifest["settings"] if item["id"] == "europe_language")
        self.assertEqual([item["set"]["host/system_language"] for item in language["choices"]], ["1", "2", "4", "3", "5"])
        once = deepcopy(manifest)
        add_europe_settings(manifest)
        self.assertEqual(manifest, once)


if __name__ == "__main__": unittest.main()
