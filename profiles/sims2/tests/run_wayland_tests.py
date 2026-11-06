"""Run SDL/Vulkan shell regressions on an isolated headless Wayland compositor."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()
    if os.name != "posix" or not shutil.which("weston"):
        parser.error("Linux and Weston are required; install the weston package")
    with tempfile.TemporaryDirectory(prefix="sims2-wayland-") as directory:
        runtime = Path(directory)
        runtime.chmod(0o700)
        env = os.environ.copy()
        for name in ("DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET"):
            env.pop(name, None)
        env.update(XDG_RUNTIME_DIR=str(runtime), WAYLAND_DISPLAY="sims2-test",
                   SDL_VIDEO_DRIVER="wayland", SDL_AUDIO_DRIVER="dummy",
                   SIMS2_SETTINGS_PATH=str(runtime / "settings.ini"))
        log = runtime / "weston.log"
        compositor = subprocess.Popen(
            ["weston", "--backend=headless", "--renderer=pixman", "--no-config",
             "--idle-time=0", "--width=1920", "--height=1080", "--socket=sims2-test",
             "--log=" + str(log)], env=env,
            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 10
            while not (runtime / "sims2-test").is_socket():
                if compositor.poll() is not None:
                    raise RuntimeError("Weston exited before creating its Wayland socket")
                if time.monotonic() >= deadline:
                    raise RuntimeError("Weston did not become ready in 10 seconds")
                time.sleep(0.05)
            subprocess.run(["ctest", "--test-dir", str(args.build_dir.resolve()),
                            "--no-tests=error", "-R", "^sims2_sdl_window$", "--verbose"],
                           env=env, check=True, timeout=60)
        except Exception:
            if log.exists():
                print(log.read_text(errors="replace"), flush=True)
            raise
        finally:
            if compositor.poll() is None:
                compositor.terminate()
                try:
                    compositor.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    compositor.kill()
                    compositor.wait()


if __name__ == "__main__":
    main()
