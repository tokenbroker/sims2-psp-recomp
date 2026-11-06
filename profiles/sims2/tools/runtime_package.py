"""Stage optional locally compiled renderers; never contains game inputs itself."""
from pathlib import Path
import shutil

RUNTIME_FILES = ("sims2_probe.exe", "SDL3.dll", "avcodec-61.dll", "avutil-59.dll")

def stage_companion(probe, install, backend):
    if backend not in ("d3d11", "vulkan"):
        raise ValueError("Unknown companion graphics backend")
    probe, install = Path(probe), Path(install)
    sources = [probe if name == "sims2_probe.exe" else probe.parent / name for name in RUNTIME_FILES]
    for source in sources:
        if not source.is_file():
            raise RuntimeError(f"Missing companion runtime dependency: {source.name}")
    target = install / backend
    target.mkdir(parents=True, exist_ok=True)
    for source, name in zip(sources, RUNTIME_FILES):
        shutil.copy2(source, target / name)
    return target
