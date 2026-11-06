"""Fetch pinned, hash-checked Windows development dependencies. No game data."""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import urllib.request
import zipfile

PROFILE = Path(__file__).resolve().parents[1]
ROOT = PROFILE.parents[1]

def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()

def download(url, destination, expected):
    if destination.is_file() and digest(destination) == expected:
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    pending = destination.with_name(destination.name + ".download")
    try:
        request = urllib.request.Request(url, headers={"User-Agent": "sims2-psp-recomp-dependency-setup"})
        with urllib.request.urlopen(request, timeout=60) as source, pending.open("wb") as output:
            shutil.copyfileobj(source, output)
        if digest(pending) != expected:
            raise RuntimeError(f"Dependency checksum mismatch: {destination.name}")
        pending.replace(destination)
    finally:
        pending.unlink(missing_ok=True)

def ensure_dependencies(verify_only=False):
    manifest = json.loads((PROFILE / "config/dependencies.json").read_text(encoding="utf-8"))
    sdl = manifest["sdl"]
    archive = ROOT / "out/dependencies" / f"SDL3-devel-{sdl['version']}-VC.zip"
    if verify_only:
        if not archive.is_file() or digest(archive) != sdl["sha256"]:
            raise RuntimeError("SDL SDK archive missing or changed; run fetch_dependencies.py first")
    else:
        print("Checking SDL3 development SDK", flush=True)
        download(sdl["url"], archive, sdl["sha256"])
    sdl_files = 0
    with zipfile.ZipFile(archive) as sdk:
        for item in sdk.infolist():
            if item.is_dir():
                continue
            parts = PurePosixPath(item.filename).parts
            if len(parts) < 2 or parts[0] != f"SDL3-{sdl['version']}":
                continue
            relative = PurePosixPath(*parts[1:])
            if ".." in relative.parts:
                raise RuntimeError("Unsafe SDK archive entry")
            name = relative.as_posix()
            if not (name.startswith("include/") or name in {"LICENSE.txt", "lib/x64/SDL3.lib", "lib/x64/SDL3.dll"}):
                continue
            destination = PROFILE / "third_party/SDL3" / relative
            data = sdk.read(item)
            matches = destination.is_file() and hashlib.sha256(destination.read_bytes()).digest() == hashlib.sha256(data).digest()
            if not matches:
                if verify_only:
                    raise RuntimeError(f"SDL dependency missing or changed: {name}")
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(data)
            sdl_files += 1
    if sdl_files < 3:
        raise RuntimeError("SDL SDK archive has an unexpected layout")
    ffmpeg = manifest["ffmpeg"]
    def fetch(entry):
        relative = PurePosixPath(entry["path"])
        if relative.is_absolute() or ".." in relative.parts:
            raise RuntimeError("Unsafe codec manifest entry")
        destination = PROFILE / "third_party/ffmpeg" / relative
        if verify_only:
            if not destination.is_file() or digest(destination) != entry["sha256"]:
                raise RuntimeError(f"Codec dependency missing or changed: {relative}")
        else:
            download(ffmpeg["base_url"] + relative.as_posix(), destination, entry["sha256"])
    print("Checking FFmpeg development SDK", flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        list(pool.map(fetch, ffmpeg["files"]))
    print(f"Dependencies verified: {sdl_files} SDL files, {len(ffmpeg['files'])} FFmpeg files", flush=True)

def ensure_shader_tools(verify_only=False):
    """Optional Windows shader compiler, authenticated by the release archive hash."""
    spec = json.loads((PROFILE / "config/dependencies.json").read_text())["glslang"]
    archive = ROOT / "out/dependencies" / f"glslang-{spec['version']}-windows-x86_64-release.zip"
    if verify_only:
        if not archive.is_file() or digest(archive) != spec["sha256"]:
            raise RuntimeError("Shader compiler archive missing or changed")
    else:
        download(spec["url"], archive, spec["sha256"])
    with zipfile.ZipFile(archive) as source:
        data = source.read("bin/glslang.exe")
    target = ROOT / "out/dependencies/glslang/bin/glslang.exe"
    matches = target.is_file() and hashlib.sha256(target.read_bytes()).digest() == hashlib.sha256(data).digest()
    if verify_only and not matches:
        raise RuntimeError("Shader compiler missing or changed")
    if not verify_only and not matches:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    print(f"Shader compiler verified: glslang {spec['version']}", flush=True)
    return target


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-only", action="store_true", help="Check existing inputs without downloading")
    parser.add_argument("--shader-tools", action="store_true", help="Also fetch the pinned Windows Vulkan shader compiler")
    args = parser.parse_args()
    try:
        ensure_dependencies(args.verify_only)
        if args.shader_tools:
            ensure_shader_tools(args.verify_only)
    except (OSError, ValueError, RuntimeError) as error:
        raise SystemExit(f"Dependency setup failed: {error}")
