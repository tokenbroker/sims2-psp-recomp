"""Check a local package's startup using a temporary, isolated memory stick."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from prepare import prepare
from setup_process import external_dll_search


def check_local_build(iso, install, output):
    iso, install, output = iso.resolve(), install.resolve(), output.resolve()
    prepare(iso, False, verify_only=True)
    probe = install / "sims2_probe.exe"
    boot = install / "game/PSP_GAME/SYSDIR/BOOT.BIN"
    if not probe.is_file() or not boot.is_file():
        raise RuntimeError("Package must contain sims2_probe.exe and prepared BOOT.BIN")
    output.mkdir(parents=True, exist_ok=True)
    state = output / "startup-state.json"
    capture = output / "startup.bmp"
    state.unlink(missing_ok=True)
    capture.unlink(missing_ok=True)
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(("SIMS2_", "PSPRECOMP_"))}
    with tempfile.TemporaryDirectory(prefix="startup-memstick-", dir=output) as memory_stick:
        # Verify the cleanup target remains inside this diagnostic output folder.
        if not Path(memory_stick).resolve().is_relative_to(output):
            raise RuntimeError("Unexpected temporary memory-stick path")
        env.update({"SIMS2_MEMSTICK": memory_stick, "SIMS2_STOP_AFTER_FRAMES": "900",
                    "SIMS2_STATE_JSON": str(state), "SIMS2_CAPTURE_LAST_BMP": str(capture)})
        try:
            with external_dll_search():
                result = subprocess.run([str(probe), str(boot), str(iso), "--max-dispatches", "unlimited"],
                                        cwd=install, env=env, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True, errors="replace", timeout=180)
        except subprocess.TimeoutExpired as error:
            partial = error.output or b""
            if isinstance(partial, bytes):
                partial = partial.decode("utf-8", errors="replace")
            (output / "startup.log").write_text(partial, encoding="utf-8")
            raise RuntimeError(f"Startup timed out; see {output / 'startup.log'}") from error
    log = output / "startup.log"
    log.write_text(result.stdout, encoding="utf-8")
    if result.returncode != 3 or "Stopped: Framebuffer diagnostic limit reached" not in result.stdout:
        raise RuntimeError(f"Startup failed (exit {result.returncode}); see {log}")
    counters = json.loads(state.read_text(encoding="utf-8"))
    if counters["frames"] != 900 or counters["gpu_draws"] <= 0 or counters["guest_us"] <= 0:
        raise RuntimeError(f"Startup did not reach the expected rendered frame; see {log}")
    bitmap = capture.read_bytes()
    if len(bitmap) < 54 or bitmap[:2] != b"BM":
        raise RuntimeError("Startup did not produce a valid framebuffer capture")
    width, height = struct.unpack_from("<ii", bitmap, 18)
    if width != 480 or abs(height) != 272:
        raise RuntimeError(f"Unexpected startup framebuffer size: {width}x{height}")
    print(f"Startup check passed: {counters['frames']} frames, {counters['gpu_draws']} draws. Evidence: {output}", flush=True)
    return counters


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--install-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    check_local_build(args.iso, args.install_dir, args.output_dir)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"Local package check failed: {error}", file=sys.stderr)
        sys.exit(1)
