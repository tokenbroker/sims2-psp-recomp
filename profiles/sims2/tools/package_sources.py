"""Package the framework, Sims 2 host and development dependencies, without game code."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import zipfile


PROFILE = Path(__file__).resolve().parents[1]
ROOT = PROFILE.parents[1]
SOURCES = {".cpp", ".hpp", ".c", ".h", ".vert", ".frag"}
NATIVE_DEPENDENCIES = {
    "profiles/sims2/third_party/SDL3/lib/x64/SDL3.lib",
    "profiles/sims2/third_party/SDL3/lib/x64/SDL3.dll",
    "profiles/sims2/third_party/ffmpeg/lib/avcodec.lib",
    "profiles/sims2/third_party/ffmpeg/lib/avutil.lib",
    "profiles/sims2/third_party/ffmpeg/bin/avcodec-61.dll",
    "profiles/sims2/third_party/ffmpeg/bin/avutil-59.dll",
}
START_HERE = """The Sims 2 PSP source development kit

Windows x64 requirements: Python 3.11+, CMake 3.21+, Visual Studio 2022 with
C++/Windows SDK. SDL3 and FFmpeg development dependencies are included.

From this extracted folder:
  python -m venv out/packaging
  out/packaging/Scripts/python.exe -m pip install PyInstaller==6.22.3
  python profiles/sims2/tools/build_from_iso.py "C:/path/to/The Sims 2 USA.iso" --package-python out/packaging/Scripts/python.exe

Setup requires your USA ULUS-10031 ISO and checks its executable hash. The
first optimized compile can take a long time. Logs and generated files stay
under out/sims2-from-iso. Re-run the command to reuse completed compiler work.
After success, import out/sims2-from-iso/install into RecompLauncher using
Library > Bring Your Recomps > Pick a Folder > Not in the list: add it anyway.

The source kit excludes game files and game-derived code. The completed local
install folder contains both and must remain private. This is a development
build with partial gameplay validation, not a self-contained release installer.
For native Linux Vulkan builds, see docs/LINUX.md and run python3 build.py with your ISO.
See profiles/sims2/README.md and THIRD_PARTY.md for coverage and notices.
"""


def source_files():
    files = {
        "CMakeLists.txt", "LICENSE", "README.md",
        "tools/analyzer_main.cpp", "tools/codegen_main.cpp", "tools/dump_function.cpp", "tools/check_public_tree.py",
        "profiles/sims2/CMakeLists.txt", "profiles/sims2/README.md",
        "profiles/sims2/THIRD_PARTY.md", "profiles/sims2/config/identity.json", "profiles/sims2/config/dependencies.json",
        "build.py", "requirements-build.txt", "CONTRIBUTING.md", "setup.sh", "launch.sh",
        "profiles/sims2/launcher/recomplauncher.json",
        "profiles/sims2/launcher/CMakeLists.txt",
        "profiles/sims2/third_party/SDL3/LICENSE.txt",
    } | NATIVE_DEPENDENCIES
    for name in ("build_from_iso.py", "check_local_build.py", "prepare.py",
                 "package_local.py", "package_playable.py", "package_sources.py", "runtime_package.py", "setup_process.py", "package_bootstrap.py", "fetch_dependencies.py", "build_linux.py", "embed_spirv.py", "benchmark.py", "doctor.py", "package_release.py", "coverage_report.py"):
        files.add("profiles/sims2/tools/" + name)
    for directory in ("include", "src", "tests", "profiles/sims2/host", "profiles/sims2/tests",
                      "profiles/sims2/third_party/SDL3/include",
                      "profiles/sims2/third_party/ffmpeg/include"):
        suffixes = SOURCES | {".py"} if directory.endswith("tests") else SOURCES
        files.update(path.relative_to(ROOT).as_posix() for path in (ROOT / directory).rglob("*")
                     if path.is_file() and path.suffix in suffixes)
    for directory in ("bigmission", "yakumo", "tiny_aes", "imgui"):
        files.update(path.relative_to(ROOT).as_posix()
                     for path in (PROFILE / "third_party" / directory).rglob("*") if path.is_file())
    files.add("profiles/sims2/third_party/ffmpeg/COPYING.LGPLv2.1")
    files.update(path.relative_to(ROOT).as_posix() for path in (ROOT / "docs").glob("*.md"))
    files.update(path.relative_to(ROOT).as_posix() for path in (ROOT / "docs/data").glob("*.json"))
    files.update(path.relative_to(ROOT).as_posix() for path in (ROOT / "docs/images").glob("*.png"))
    for name in files:
        path = ROOT / name
        if not path.is_file() or not path.resolve().is_relative_to(ROOT):
            raise RuntimeError(f"Source-kit input missing or outside the checkout: {name}")
        parts = Path(name).parts
        if any(part.lower() in {"game", "generated", "analysis", "savedata", "captures", "out"} for part in parts):
            raise RuntimeError(f"Game/output directory cannot enter the source kit: {name}")
        if path.suffix.lower() in {".exe", ".obj", ".iso", ".cso", ".elf", ".bin"}:
            raise RuntimeError(f"Game/build artifact cannot enter the source kit: {name}")
        if path.suffix.lower() in {".dll", ".lib"} and name not in NATIVE_DEPENDENCIES:
            raise RuntimeError(f"Unexpected native binary in source kit: {name}")
    return sorted(files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "out/sims2-source-kit.zip")
    args = parser.parse_args()
    output = args.output.resolve()
    if output.is_relative_to(ROOT) and not output.is_relative_to(ROOT / "out"):
        parser.error("Place the source kit under out/ or outside the checkout")
    if output.suffix.lower() != ".zip":
        parser.error("--output must name a .zip file")
    files = source_files()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".partial")
    manifest = {"format": 1, "kind": "source-development-kit", "files": []}
    with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name in files:
            data = (ROOT / name).read_bytes()
            archive.writestr(name, data)
            manifest["files"].append({"path": name, "bytes": len(data),
                                      "sha256": hashlib.sha256(data).hexdigest()})
        readme = START_HERE.encode("utf-8")
        manifest["files"].append({"path": "START_HERE.txt", "bytes": len(readme),
                                  "sha256": hashlib.sha256(readme).hexdigest()})
        archive.writestr("source-kit.json", json.dumps(manifest, indent=2) + "\n")
        archive.writestr("START_HERE.txt", readme)
    temporary.replace(output)
    print(f"Source kit: {output} ({len(manifest['files'])} payload files)")
    print("Framework/host sources and third-party development files only; generate game code from your own ISO.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        print(f"Source packaging failed: {error}", file=sys.stderr)
        sys.exit(1)
