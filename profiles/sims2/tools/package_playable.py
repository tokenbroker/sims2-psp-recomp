"""Package prebuilt Windows runtimes; never include disc files or generated sources.

Unlike the source installer, this release contains compiled native game code.
Players still supply the exact supported ISO; preparation only validates/extracts.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile

PROFILE = Path(__file__).resolve().parents[1]
ROOT = PROFILE.parents[1]
RUNTIME = ("sims2_probe.exe", "SDL3.dll", "avcodec-61.dll", "avutil-59.dll")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default="0.3.2")
    parser.add_argument("--d3d11", type=Path, required=True)
    parser.add_argument("--vulkan", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    dependencies = json.loads((PROFILE / "config/dependencies.json").read_text())
    codec_hashes = {Path(entry["path"]).name: entry["sha256"]
                    for entry in dependencies["ffmpeg"]["files"]
                    if entry["path"].startswith("bin/")}
    for folder in (args.d3d11, args.vulkan):
        probe = (folder / "sims2_probe.exe").read_bytes()
        if not probe.startswith(b"MZ") or b"Host-test build has no game corpus" in probe:
            raise RuntimeError("A production native runtime is required, not a synthetic host-test build")
        for name in ("avcodec-61.dll", "avutil-59.dll"):
            if hashlib.sha256((folder / name).read_bytes()).hexdigest() != codec_hashes[name]:
                raise RuntimeError(f"Codec dependency differs from the pinned upstream binary: {name}")
    work, output = args.work_dir.resolve(), args.output.resolve()
    for path in (work, output):
        if path == ROOT or ROOT.is_relative_to(path) or (
                path.is_relative_to(ROOT) and not path.is_relative_to(ROOT / "out")):
            parser.error("Use output/work paths under out/ or outside the checkout")
    work.mkdir(parents=True, exist_ok=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        sys.executable, "-m", "PyInstaller", "--noconfirm", "--onedir", "--console",
        "--name", "sims2_setup", "--distpath", str(work / "frozen"),
        "--workpath", str(work / "pyinstaller"), "--specpath", str(work),
        "--add-data", str(PROFILE / "config/identity.json") + ";config",
        str(PROFILE / "tools/prepare.py")], check=True)
    payload = {}

    def put(name, data):
        if name in payload:
            raise RuntimeError(f"Duplicate release file: {name}")
        payload[name] = data

    for name in ("sims2.exe", *RUNTIME):
        put(name, (args.d3d11 / name).read_bytes())
    for name in RUNTIME:
        put("vulkan/" + name, (args.vulkan / name).read_bytes())
    frozen = work / "frozen/sims2_setup"
    for path in frozen.rglob("*"):
        if path.is_file():
            put(path.relative_to(frozen).as_posix(), path.read_bytes())
    manifest = json.loads((PROFILE / "launcher/recomplauncher.json").read_text())
    manifest["game"]["label"] = "Your USA The Sims 2 PSP ISO (ULUS-10031). No compiler required."
    manifest["prepare"]["label"] = "Verify your USA disc and prepare local game files (no compilation)"
    manifest["prepare"]["timeout"] = 120
    put("recomplauncher.json", (json.dumps(manifest, indent=2) + "\n").encode())
    put("LICENSE.txt", (ROOT / "LICENSE").read_bytes())
    put("THIRD_PARTY.md", (PROFILE / "THIRD_PARTY.md").read_bytes())
    put("notices/dependencies.json", (PROFILE / "config/dependencies.json").read_bytes())
    put("notices/Python-LICENSE.txt", (Path(sys.base_prefix) / "LICENSE.txt").read_bytes())
    # Retain dependency notices, not development SDKs, user files or game assets.
    notices = {
        "imgui": "LICENSE.txt", "SDL3": "LICENSE.txt", "tiny_aes": "UNLICENSE",
        "bigmission": "LICENSE", "yakumo": "LICENSE", "ffmpeg": "COPYING.LGPLv2.1",
    }
    for component, name in notices.items():
        put(f"notices/{component}/{name}", (PROFILE / "third_party" / component / name).read_bytes())
    import importlib.metadata
    distribution = importlib.metadata.distribution("PyInstaller")
    license_path = next(p for p in distribution.files if str(p).endswith("licenses/COPYING.txt"))
    put("notices/PyInstaller-COPYING.txt", Path(distribution.locate_file(license_path)).read_bytes())
    put("README.txt", (
        f"THE SIMS 2 PSP - v{args.version} PREBUILT WINDOWS PREVIEW\n"
        "Extract this whole ZIP and import its folder into RecompLauncher. Select your own USA ULUS-10031 ISO.\n"
        "Setup verifies the disc and prepares local game files. No compiler, CMake, Visual Studio or separate Python installation is required.\n"
        "This download includes compiled native game code. It includes no original PSP executable, disc image, game assets or saves.\n"
        "Keep your ISO available: game data is read from it during play.\n"
        "F1 or L3+R3 opens settings; F2 opens performance statistics. D3D11 is the default; Vulkan requires Save and restart.\n"
        "Settings/saves stay under %LOCALAPPDATA%/Sims2PSPRecomp. Updating the runtime does not replace them.\n"
        "Native engine timing is the default. Vulkan/high engine rates remain experimental.\n"
        "Source and build instructions: https://github.com/tokenbroker/sims2-psp-recomp\n"
        "Not affiliated with EA, Maxis or Sony. Project licensing does not grant rights to third-party game content.\n"
    ).encode())
    forbidden = {".iso", ".cso", ".bin", ".elf", ".obj", ".lib", ".pdb", ".cpp", ".hpp"}
    for name in payload:
        if Path(name).suffix.lower() in forbidden or "generated_unit_" in name:
            raise RuntimeError(f"Unexpected source/disc payload: {name}")
    identity = json.loads((PROFILE / "config/identity.json").read_text())
    receipt = {"format": 1, "kind": "prebuilt-native-runtime", "version": args.version,
               "disc_id": identity["disc_id"], "boot_sha256": identity["boot_sha256"],
               "files": [{"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                         for name, data in sorted(payload.items())]}
    put("release-files.json", (json.dumps(receipt, indent=2) + "\n").encode())
    temporary = output.with_suffix(output.suffix + ".partial")
    with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, data in sorted(payload.items()):
            archive.writestr(name, data)
    temporary.replace(output)
    print(f"Prebuilt Windows runtime: {output.name}; {len(payload)} files")


if __name__ == "__main__":
    main()
