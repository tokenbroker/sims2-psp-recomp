"""Generate, compile and prepare a local RecompLauncher folder from a USA ISO.

Requires Windows, CMake and Visual Studio 2022 C++/Windows SDK. Source-mode
packaging uses Python/PyInstaller; the frozen launcher bootstrap bundles Python
and stages the runtime directly. Generated code and binaries stay in --workspace.
The resulting install folder contains game-derived code and must not be shared.
"""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import time

from prepare import prepare
from check_local_build import check_local_build
from setup_process import external_dll_search, own_process_job


FROZEN = getattr(sys, "frozen", False)
PROFILE = (Path(sys.executable).parent / "source/profiles/sims2" if FROZEN
           else Path(__file__).resolve().parents[1]).resolve()
ROOT = PROFILE.parents[1]


def report_progress(fraction, message):
    message = str(message).replace("\r", " ").replace("\n", " ")
    print(f"progress {max(0.0, min(1.0, fraction)):.4f} {message}", flush=True)


class CompilationProgress:
    def __init__(self, total, emit=report_progress):
        self.total = max(1, total)
        self.highest = -1
        self.reported = -9
        self.emit = emit

    def __call__(self, line):
        match = re.fullmatch(r"\s*generated_unit_(\d+)\.cpp\s*", line)
        if not match:
            return
        self.highest = max(self.highest, min(self.total - 1, int(match[1])))
        if self.highest - self.reported >= 8 or self.highest == self.total - 1:
            self.reported = self.highest
            self.emit(0.25 + 0.64 * (self.highest + 1) / self.total,
                      f"Compiling game code (unit {self.highest + 1} of {self.total})")


def report_failure(error):
    report_progress(0, f"Setup failed: {error}")
    if FROZEN:
        try:
            (Path(sys.executable).parent / "setup-error.log").write_text(
                f"Source setup failed: {error}\n", encoding="utf-8")
        except OSError:
            pass  # The original error remains available on stdout/stderr.
    print(f"Source setup failed: {error}", file=sys.stderr)


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


@contextmanager
def workspace_lock(workspace):
    import msvcrt
    # The OS releases this lock if setup crashes; a stale file cannot block a retry.
    with (workspace / "setup.lock").open("a+b") as lock:
        lock.seek(0, 2)
        if lock.tell() == 0:
            lock.write(b"\0")
            lock.flush()
        lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError as error:
            raise RuntimeError(f"Another setup is already using {workspace}") from error
        try:
            yield
        finally:
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)


def run(command, log, on_line=None):
    command = [str(part) for part in command]
    print("Running: " + subprocess.list2cmdline(command), flush=True)
    log.write("\n$ " + subprocess.list2cmdline(command) + "\n")
    log.flush()
    # Stream progress to the setup console while retaining full compiler errors.
    with external_dll_search():
        child = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True, errors="replace")
    with child:
        try:
            for line in child.stdout:
                print(line, end="", flush=True)
                log.write(line)
                log.flush()
                if on_line:
                    on_line(line)
            code = child.wait()
        except BaseException:
            subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           creationflags=subprocess.CREATE_NO_WINDOW)
            child.wait()
            raise
    log.flush()
    if code:
        raise RuntimeError(f"Command exited with {code}; see {log.name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", action="version", version="sims2-source-setup 0.1.0-dev")
    parser.add_argument("iso", type=Path)
    parser.add_argument("--workspace", type=Path, default=ROOT / "out/sims2-from-iso")
    parser.add_argument("--jobs", type=int, default=4, help="Parallel build projects (1..16)")
    parser.add_argument("--package-python", type=Path, default=Path(sys.executable),
                        help="Python executable with PyInstaller installed")
    parser.add_argument("--generate-only", action="store_true",
                        help="Validate/extract and generate code without compiling the game")
    parser.add_argument("--runtime-only", action="store_true", default=FROZEN,
                        help="Stage the runtime without building another setup executable")
    parser.add_argument("--graphics", choices=("D3D11", "VULKAN"), default="D3D11")
    args = parser.parse_args()
    if FROZEN:
        try:
            (Path(sys.executable).parent / "setup-error.log").unlink(missing_ok=True)
        except OSError:
            pass
    if not 1 <= args.jobs <= 16:
        parser.error("--jobs must be 1..16")
    workspace = args.workspace.resolve()
    # The source checkout and its existing playable output must remain separate.
    if workspace == ROOT or ROOT.is_relative_to(workspace) or (
            workspace.is_relative_to(ROOT) and not workspace.is_relative_to(ROOT / "out")):
        parser.error("Choose a workspace under out/ or outside the source checkout")
    if workspace == ROOT / "out" or workspace.is_relative_to(ROOT / "out/sims2-desktop-launcher"):
        parser.error("Choose a separate build workspace, not the existing launcher folder")
    iso = args.iso.resolve()
    # Reject unsupported/truncated discs before creating a build directory.
    report_progress(0, "Checking your USA disc")
    identity = prepare(iso, False, verify_only=True)
    report_progress(0.03, "Disc verified; checking build tools")
    if os.name != "nt":
        raise RuntimeError("This setup path currently supports Windows x64 only")
    cmake = shutil.which("cmake")
    if not cmake:
        raise RuntimeError("CMake 3.21 or newer must be installed and available on PATH")
    if not FROZEN:
        from fetch_dependencies import ensure_dependencies
        ensure_dependencies()
    for dependency in (PROFILE / "third_party/SDL3/include/SDL3/SDL.h",
                       PROFILE / "third_party/SDL3/lib/x64/SDL3.lib",
                       PROFILE / "third_party/SDL3/lib/x64/SDL3.dll",
                       PROFILE / "third_party/ffmpeg/include/libavcodec/avcodec.h",
                       PROFILE / "third_party/ffmpeg/lib/avcodec.lib",
                       PROFILE / "third_party/ffmpeg/lib/avutil.lib",
                       PROFILE / "third_party/ffmpeg/bin/avcodec-61.dll",
                       PROFILE / "third_party/ffmpeg/bin/avutil-59.dll"):
        if not dependency.is_file():
            raise RuntimeError(f"Required development dependency is missing: {dependency}")
    package_python = args.package_python.resolve()
    if not args.generate_only and not args.runtime_only:
        check = subprocess.run([str(package_python), "-c", "import PyInstaller"],
                               capture_output=True, text=True)
        if check.returncode:
            raise RuntimeError("Install PyInstaller in --package-python's environment before building")
    workspace.mkdir(parents=True, exist_ok=True)
    framework = workspace / "framework"
    build = workspace / "build"
    generated = workspace / "generated"
    game = workspace / "game"
    install = workspace / "install"
    started = time.monotonic()
    with own_process_job(), workspace_lock(workspace), (workspace / "setup.log").open("w", encoding="utf-8") as log:
        print(f"Verified {identity['disc_id']}. Build log: {log.name}", flush=True)
        report_progress(0.05, "Configuring the recompiler")
        run([cmake, "-S", ROOT, "-B", framework, "-G", "Visual Studio 17 2022", "-A", "x64",
             "-DPSPRECOMP_PROFILE=", "-DPSPRECOMP_BUILD_TESTS=OFF",
             "-DPSPRECOMP_BUILD_PROFILE_TESTS=OFF", "-DPSPRECOMP_MSVC_MP_JOBS=1"], log)
        report_progress(0.08, "Building the recompiler")
        run([cmake, "--build", framework, "--config", "Release", "--target", "psp_recomp",
             "--parallel", args.jobs, "--", "/nodeReuse:false"], log)
        report_progress(0.15, "Generating game code from your disc")
        prepare(iso, False, game, report_dir=workspace / "analysis")
        run([framework / "Release/psp_recomp.exe", game / "PSP_GAME/SYSDIR/BOOT.BIN",
             "--auto", generated, identity["load_base"], identity["unit_span_bytes"]], log)
        units = list(generated.glob("generated_unit_*.cpp"))
        if not units or not (generated / "generated_registry.cpp").is_file():
            raise RuntimeError("The recompiler did not produce a complete game corpus")
        if args.generate_only:
            print(f"Generated {len(units)} units in {generated}. Game compilation was not requested.")
            report_progress(1, "Code generation complete")
            return
        report_progress(0.22, "Configuring the game build")
        run([cmake, "-S", ROOT, "-B", build, "-G", "Visual Studio 17 2022", "-A", "x64",
             "-DPSPRECOMP_PROFILE=sims2", "-DPSPRECOMP_BUILD_TESTS=OFF",
             "-DPSPRECOMP_BUILD_PROFILE_TESTS=OFF", "-DPSPRECOMP_MSVC_MP_JOBS=1",
             "-DSIMS2_OPTIMIZE_AOT=ON", f"-DSIMS2_GRAPHICS_BACKEND={args.graphics}", f"-DSIMS2_GENERATED_DIR={generated}"], log)
        report_progress(0.25, "Compiling game code; completed work is reused")
        run([cmake, "--build", build, "--config", "Release", "--target", "sims2",
             "--parallel", args.jobs, "--", "/nodeReuse:false"], log, CompilationProgress(len(units)))
        probe = build / "profiles/sims2/Release/sims2_probe.exe"
        receipt_path = install / "local-build.json"
        # Once package files begin changing, an earlier success receipt is stale.
        receipt_path.unlink(missing_ok=True)
        report_progress(0.90, "Preparing the compiled game")
        if args.runtime_only:
            install.mkdir(parents=True, exist_ok=True)
            for name in ("sims2_probe.exe", "sims2.exe", "SDL3.dll", "avcodec-61.dll", "avutil-59.dll"):
                shutil.copy2(probe.parent / name, install / name)
            prepare(iso, False, install / "game", report_dir=workspace / "analysis")
        else:
            run([package_python, PROFILE / "tools/package_local.py", "--probe", probe,
                 "--output-dir", install, "--work-dir", workspace / "packaging"], log)
            run([install / "sims2_setup.exe", iso, "--output-dir", install / "game"], log)
        report_progress(0.95, "Verifying game startup with an isolated save location")
        counters = check_local_build(iso, install, workspace / "validation")
        receipt = {
            "format": 1, "disc_id": identity["disc_id"],
            "boot_sha256": identity["boot_sha256"], "generated_units": len(units),
            "generator_sha256": digest(framework / "Release/psp_recomp.exe"),
            "runtime_sha256": digest(install / "sims2_probe.exe"),
            "startup_smoke_frames": counters["frames"],
            "elapsed_seconds": round(time.monotonic() - started, 2),
        }
        pending_receipt = receipt_path.with_suffix(".json.partial")
        pending_receipt.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        pending_receipt.replace(receipt_path)
    print(f"{'Compiled runtime' if args.runtime_only else 'Local launcher folder'} ready: {install}", flush=True)
    print("Contains game-derived code and inputs. Do not publish this folder.", flush=True)
    report_progress(1, "The Sims 2 PSP is ready")


if __name__ == "__main__":
    # RecompLauncher reads setup output as UTF-8, including Windows path errors.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired) as error:
        report_failure(error)
        sys.exit(1)
    except KeyboardInterrupt:
        print("Setup interrupted. Re-run it to reuse completed build work.", file=sys.stderr)
        sys.exit(130)
