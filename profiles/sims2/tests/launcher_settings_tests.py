"""Check every launcher choice against the real host settings parser/writer."""
import configparser
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
PROFILE = ROOT / "profiles/sims2/launcher/recomplauncher.json"


def main():
    executable = Path(sys.argv[1]).resolve()
    manifest = json.loads(PROFILE.read_text())
    config, = manifest["config"]
    assert config["format"] == "ini" and config["file"] == [{"path": "{local}/Sims2PSPRecomp/settings.ini"}]
    defaults = {k: v for row in manifest["settings"] for k, v in row["defaults"].items()}
    assert defaults == config["new"]
    assert all(k.startswith("host/") for k in defaults)
    count = 0
    with tempfile.TemporaryDirectory(prefix="sims2-launcher-settings-") as directory:
        source, target = (Path(directory) / n for n in ("input.ini", "output.ini"))

        def normalize(values, missing=False):
            if not missing:
                source.write_text("[host]\n" + "".join(k.split("/", 1)[1] + "=" + v + "\n" for k, v in values.items()))
            subprocess.run([str(executable), "--normalize-settings", str(source), str(target)], check=True)
            parsed = configparser.ConfigParser()
            parsed.read(target)
            return {"host/" + k: v for k, v in parsed["host"].items()}

        # An absent config must match launcher defaults and the actual game defaults.
        actual = normalize({}, missing=True)
        assert all(actual[k] == v for k, v in defaults.items()), "Launcher defaults diverge from F1"
        for row in manifest["settings"]:
            assert len({choice["label"] for choice in row["choices"]}) == len(row["choices"])
            for choice in row["choices"]:
                values = defaults | choice["set"]
                # Unrelated customized controls must survive a launcher graphics change.
                values |= {"host/key_0": "70", "host/pad_0": "3", "host/mouse_sensitivity": "175"}
                actual = normalize(values)
                assert all(actual[k] == v for k, v in values.items()), (row["id"], choice["label"])
                count += 1
        # Fractional F1 scaling must be cleared when a fixed launcher multiplier is selected.
        scale = next(r for r in manifest["settings"] if r["id"] == "render_scale")
        values = defaults | {"host/render_percent": "150"} | scale["choices"][1]["set"]
        assert normalize(values)["host/render_percent"] == "0"
    print(f"Validated {len(manifest['settings'])} launcher rows and {count} choices against native host settings")


if __name__ == "__main__":
    main()
