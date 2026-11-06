"""Build a RecompLauncher source-setup candidate without game data or AOT code."""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import zipfile


PROFILE = Path(__file__).resolve().parents[1]
ROOT = PROFILE.parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "out/sims2-bootstrap-candidate")
    parser.add_argument("--work-dir", type=Path, default=ROOT / "out/sims2-bootstrap-build")
    args = parser.parse_args()
    output, work = args.output_dir.resolve(), args.work_dir.resolve()
    for folder in (output, work):
        if folder == ROOT or ROOT.is_relative_to(folder) or (
                folder.is_relative_to(ROOT) and not folder.is_relative_to(ROOT / "out")):
            parser.error("Use separate output/work folders under out/ or outside the checkout")
        if folder == ROOT / "out" or folder.is_relative_to(ROOT / "out/sims2-desktop-launcher"):
            parser.error("Do not overwrite the existing launcher folder")
    if output == work or output.is_relative_to(work) or work.is_relative_to(output):
        parser.error("Output and packaging work directories must be separate")
    output.mkdir(parents=True, exist_ok=True)
    work.mkdir(parents=True, exist_ok=True)
    payload = set()

    def put(name, data):
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts or path.parts[0] == "work":
            raise RuntimeError(f"Invalid bootstrap payload path: {name}")
        destination = output.joinpath(*path.parts)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.is_file() or destination.read_bytes() != data:
            destination.write_bytes(data)
        payload.add(name)

    source_archive = work / "source-kit.zip"
    subprocess.run([sys.executable, str(PROFILE / "tools/package_sources.py"),
                    "--output", str(source_archive)], check=True)
    with zipfile.ZipFile(source_archive) as archive:
        manifest = json.loads(archive.read("source-kit.json"))
        for entry in manifest["files"]:
            data = archive.read(entry["path"])
            if hashlib.sha256(data).hexdigest() != entry["sha256"]:
                raise RuntimeError(f"Source-kit hash mismatch: {entry['path']}")
            put("source/" + entry["path"], data)
        put("source/source-kit.json", archive.read("source-kit.json"))
    subprocess.run([
        sys.executable, "-m", "PyInstaller", "--noconfirm", "--onedir", "--console",
        "--name", "sims2_setup", "--distpath", str(work / "frozen"),
        "--workpath", str(work / "pyinstaller"), "--specpath", str(work),
        "--add-data", str(PROFILE / "config/identity.json") + ";config",
        str(PROFILE / "tools/build_from_iso.py")
    ], check=True, stderr=subprocess.STDOUT)
    frozen = work / "frozen/sims2_setup"
    for path in frozen.rglob("*"):
        if path.is_file():
            put(path.relative_to(frozen).as_posix(), path.read_bytes())
    cmake = shutil.which("cmake")
    if not cmake:
        raise RuntimeError("CMake is required to package the native frontend")
    native = work / "native"
    subprocess.run([cmake, "-S", str(PROFILE / "launcher"), "-B", str(native),
                    "-G", "Visual Studio 17 2022", "-A", "x64"], check=True)
    subprocess.run([cmake, "--build", str(native), "--config", "Release",
                    "--parallel", "2", "--", "/nodeReuse:false"], check=True)
    put("sims2_launcher.exe", (native / "Release/sims2_launcher.exe").read_bytes())
    launcher = json.loads((PROFILE / "launcher/recomplauncher.json").read_text())
    launcher["game"]["label"] = "USA ULUS-10031 ISO. First setup requires CMake and Visual Studio 2022 C++ tools."
    launcher["start"]["program"] = ["sims2_launcher.exe"]
    launcher["start"]["args"] = ["{install}/work/install/game/PSP_GAME/SYSDIR/BOOT.BIN", "{game}"]
    launcher["prepare"] = {"program": ["sims2_setup.exe"],
                           "args": ["{game}", "--workspace", "{install}/work"],
                           "label": "Build and verify from your USA disc (first setup can take a while)",
                           "timeout": 7200}
    launcher["exit_codes"]["1"] = "Setup or runtime failed. Setup details: work/setup.log or setup-error.log. Game details: %LOCALAPPDATA%/Sims2PSPRecomp/logs/latest.log."
    put("recomplauncher.json", (json.dumps(launcher, indent=2) + "\n").encode())
    put("LICENSE.txt", (ROOT / "LICENSE").read_bytes())
    put("notices/Python-LICENSE.txt", (Path(sys.base_prefix) / "LICENSE.txt").read_bytes())
    distribution = importlib.metadata.distribution("PyInstaller")
    license_path = next(path for path in distribution.files if str(path).endswith("licenses/COPYING.txt"))
    put("notices/PyInstaller-COPYING.txt", Path(distribution.locate_file(license_path)).read_bytes())
    put("README.txt", (
        "THE SIMS 2 PSP — v0.2.1 SOURCE INSTALLER PREVIEW\n"
        "Import this folder into RecompLauncher and select your USA ULUS-10031 ISO.\n"
        "Setup bundles Python. CMake 3.21+ and Visual Studio 2022 C++/Windows SDK must be installed.\n"
        "The first optimized build can take a long time; progress is retained in work/setup.log.\n"
        "Early validation errors are retained in setup-error.log beside this executable.\n"
        "Code generation, compilation and startup verification run before setup succeeds.\n"
        "Re-run setup to reuse completed build work. Stop setup to stop its compiler children.\n"
        "This candidate excludes game data and generated game code. Setup creates both under work/.\n"
        "Keep the prepared work/ folder private. The release archive excludes it even when repackaging.\n"
        "Playable preview. Later gameplay and hardware combinations may expose issues; report reproducible problems.\n"
        "Third-party notices: source/profiles/sims2/THIRD_PARTY.md, source/profiles/sims2/third_party, notices/.\n"
    ).encode("utf-8"))
    entries = []
    for name in sorted(payload):
        if "generated_unit_" in name or PurePosixPath(name).name in {"BOOT.BIN", "EBOOT.BIN", "sims2_probe.exe", "sims2.exe"}:
            raise RuntimeError(f"Game-derived file cannot enter bootstrap archive: {name}")
        data = (output / name).read_bytes()
        entries.append({"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    receipt = (json.dumps({"format": 1, "kind": "launcher-source-bootstrap", "files": entries}, indent=2) + "\n").encode()
    put("bootstrap-files.json", receipt)
    archive_path = output.with_name(output.name + ".zip")
    temporary = archive_path.with_suffix(".zip.partial")
    with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name in sorted(payload):
            archive.write(output / name, name)
    temporary.replace(archive_path)
    print(f"Bootstrap candidate: {output}")
    print(f"Archive: {archive_path} ({len(payload)} files; private work/ excluded)")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError, StopIteration) as error:
        print(f"Bootstrap packaging failed: {error}", file=sys.stderr)
        sys.exit(1)
