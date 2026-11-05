"""Build The Sims 2 PSP from your own supported USA ISO."""
from pathlib import Path
import runpy
import sys
import os

if __name__ == "__main__":
    tools = Path(__file__).resolve().parent / "profiles/sims2/tools"
    sys.path.insert(0, str(tools))
    runpy.run_path(str(tools / ("build_from_iso.py" if os.name == "nt" else "build_linux.py")), run_name="__main__")
