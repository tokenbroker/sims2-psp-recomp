"""Build a LOCAL desktop game folder for RecompLauncher.

Run with a Python environment containing PyInstaller. This contains generated
game code in sims2_probe.exe and is not a redistributable game release.
"""
from pathlib import Path
import argparse
import json
import shutil
import subprocess
import sys
from runtime_package import stage_companion
from launcher_manifest import europe_language_setting

profile = Path(__file__).resolve().parents[1]
root = profile.parents[1]
output = root / "out/sims2-desktop-launcher"
work = root / "out/sims2-setup-build"
parser = argparse.ArgumentParser(description=__doc__)
optimized = root / "out/sims2-optimized/profiles/sims2/Release/sims2_probe.exe"
parser.add_argument("--probe", type=Path,
                    default=optimized if optimized.is_file() else root / "out/sims2/profiles/sims2/Release/sims2_probe.exe",
                    help="Native probe to package, including an optimized build")
parser.add_argument("--output-dir", type=Path, default=output,
                    help="Local launcher folder; contains generated game code")
parser.add_argument("--work-dir", type=Path, default=work,
                    help="PyInstaller build workspace")
parser.add_argument("--vulkan-probe", type=Path, help="Optional Vulkan runtime to install alongside a D3D11 primary")
parser.add_argument("--d3d11-probe", type=Path, help="Optional D3D11 runtime to install alongside a Vulkan primary")
parser.add_argument("--region", choices=("USA", "EUROPE"), default="USA")
args = parser.parse_args()
probe = args.probe.resolve()
output = args.output_dir.resolve()
work = args.work_dir.resolve()
print(f"Packaging runtime: {probe}", flush=True)
if not probe.is_file():
    raise SystemExit("Build sims2_probe before packaging")
output.mkdir(parents=True, exist_ok=True)
work.mkdir(parents=True, exist_ok=True)
subprocess.run([
    sys.executable, "-m", "PyInstaller", "--noconfirm", "--onefile", "--console",
    "--name", "sims2_setup", "--distpath", str(output), "--workpath", str(work / "work"),
    "--specpath", str(work), "--add-data", str(profile / "config/identity.json") + ";config",
    str(profile / "tools/prepare.py")
], check=True, stderr=subprocess.STDOUT)
shutil.copy2(probe, output / probe.name)
desktop = probe.parent / "sims2.exe"
if not desktop.is_file():
    raise SystemExit("Build the sims2 desktop target before packaging")
shutil.copy2(desktop, output / desktop.name)
for name in ("avcodec-61.dll", "avutil-59.dll", "SDL3.dll"):
    shutil.copy2(probe.parent / name, output / name)
for backend, companion in (("vulkan", args.vulkan_probe), ("d3d11", args.d3d11_probe)):
    if companion:
        stage_companion(companion.resolve(), output, backend)
manifest = json.loads((profile / "launcher/recomplauncher.json").read_text())
if args.region == "EUROPE":
    manifest["config"][0]["file"] = [{"path": "{local}/Sims2PSPRecomp/settings-europe.ini"}]
    manifest["config"][0]["new"].update({"host/system_language": "1", "host/engine_fps": "0"})
    manifest["settings"].append(europe_language_setting("main"))
(output / "recomplauncher.json").write_text(json.dumps(manifest, indent=2) + "\n")
shutil.copy2(root / "LICENSE", output / "LICENSE.txt")
shutil.copy2(profile / "THIRD_PARTY.md", output / "THIRD_PARTY.md")
shutil.copytree(profile / "third_party", output / "third_party", dirs_exist_ok=True)
(output / "README.txt").write_text(
    "LOCAL DEVELOPMENT BUILD. PARTIAL GAMEPLAY VALIDATION. DO NOT PUBLISH.\n"
    "Contains AOT game code generated from your own disc.\n"
    "Import through Library > Bring Your Recomps > Pick a Folder > Not in the list: add it anyway.\n"
    "Select your USA ULUS-10031 ISO. Setup validates the disc and BOOT.BIN.\n"
    "Play runs sims2.exe with no console window or diagnostic run limit.\n"
    "Runtime logs: %LOCALAPPDATA%/Sims2PSPRecomp/logs/latest.log\n"
    "Opening movies, Create-A-Sim, the service-station story, Paradise Place arrival, initial estate ghost encounters and house Buy/Design operations have been tested. SDL3 audio and gamepad input are enabled.\n"
    "Encrypted save/load has been tested in the service station and Paradise Place. Later gameplay and a full playthrough remain unverified. Launcher backs up the declared savedata folder.\n"
    "F1: display/audio settings (Apply or Save); F2: performance overlay. Settings: %LOCALAPPDATA%/Sims2PSPRecomp/settings.ini. Output resolution controls window size; internal resolution (1x-8x) controls 3D detail. FPS caps, HDR controls, anisotropic filtering, custom resolutions, percentage render scale, aspect/camera matching and controller options are available in F1. Presentation caps and experimental 120/240 engine rates are separate. Engine rate and Windows graphics backend changes require Save and restart. Optional Vulkan/D3D11 companion runtimes must be installed.\n"
    "Set SIMS2_MUTE=1 to disable Windows audio playback.\n"
    "Keyboard: arrows=d-pad, Enter=Start, Backspace=Select, X/Z/S/A=Cross/Circle/Triangle/Square, Q/E=L/R, I/J/K/L=stick.\n"
    "SDL3 controllers: left stick=analog, d-pad=d-pad, South/East/West/North=Cross/Circle/Square/Triangle, shoulders=L/R, Menu=Start, View=Select. Hot-plug supported; focus the game window.\n"
)
print(f"Local desktop build folder: {output}")
