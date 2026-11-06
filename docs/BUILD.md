# Building The Sims 2 PSP Recomp

For native Linux/Vulkan, Wayland/X11 and Steam Deck setup, see [LINUX.md](LINUX.md).

## Prerequisites

The game runtime currently targets Windows 10/11 x64 and Direct3D 11. Install:

1. Python 3.11 or newer, with the Python launcher available as `py`.
2. CMake 3.21 or newer, available on PATH.
3. Visual Studio 2022 or Build Tools 2022, including Desktop development with C++
   and the Windows SDK.
4. A plain ISO9660 dump of your own **USA ULUS-10031** PSP disc.

Other regions/executables are not supported. Setup checks the disc ID and exact
BOOT.BIN SHA-256 listed in `profiles/sims2/config/identity.json`. A checksum is an
identifier, not game data. No executable decryption is performed.

## Local build

From the repository root in PowerShell:

```powershell
py -3.11 -m venv .venv
.venv\Scripts\python.exe -m pip install -r requirements-build.txt
.venv\Scripts\python.exe build.py "D:\Games\The Sims 2 USA.iso"
```

`build.py` runs the profile's ISO-to-build setup. It fetches the pinned dependencies
in `config/dependencies.json`, validates the disc, extracts only the required
local inputs, builds the recompiler, generates AOT units and compiles the game.
The native output is optimized; startup validation uses an isolated temporary
memory stick, not the player's save folder.

Keep the build workspace after interruption and rerun the same command to reuse
completed compilation. The default workspace is `out/sims2-from-iso`; override it
with `--workspace`, and limit parallel compiler work with `--jobs` (default 4).
Use `build.py --help` for all options. `--generate-only` stops after code generation.
`--runtime-only` omits the local setup executable and launcher packaging; it is
primarily for the frozen source installer and development checks.

The normal build requires PyInstaller from `requirements-build.txt` because it
also creates the local disc-validation setup executable. Python is bundled when
building the optional source-installer package, but Windows C++ build tools and
CMake are still required on the machine that compiles the game.

## Run

Import `out/sims2-from-iso/install` in RecompLauncher using **Bring Your Recomps**,
then select the same supported ISO. The included manifest configures Play,
first-run validation and the save-backup folder.

For a direct launch without RecompLauncher:

```powershell
$env:SIMS2_WINDOW = "1"
& .\out\sims2-from-iso\install\sims2.exe `
  ".\out\sims2-from-iso\install\game\PSP_GAME\SYSDIR\BOOT.BIN" `
  "D:\Games\The Sims 2 USA.iso"
```

This starts the visible game and uses the ordinary local settings/save folders.
A headless diagnostic limit is not a substitute for this interactive launch.

## Dependencies

SDL3 3.4.20 is downloaded from its official GitHub release, with its archive hash
verified. The pinned FFmpeg 7.1 SDK files are fetched from the public PSPRecomp
commit recorded in `dependencies.json`; every file is verified by SHA-256.
Licenses remain in `profiles/sims2/third_party`. Downloaded headers/libraries/DLLs
are ignored by Git.

To fetch or verify dependencies separately:

```powershell
.venv\Scripts\python.exe profiles/sims2/tools/fetch_dependencies.py
.venv\Scripts\python.exe profiles/sims2/tools/fetch_dependencies.py --verify-only
```

The fetcher does not download a game, BIOS or firmware.

## Tests

Framework checks do not need a disc or the downloaded game-runtime dependencies:

```powershell
cmake -S . -B out/core
cmake --build out/core --config Release --parallel 2
ctest --test-dir out/core -C Release --output-on-failure
```

Profile tests need locally generated code and the Windows SDK/dependencies:

```powershell
cmake -S . -B out/profile-tests -G "Visual Studio 17 2022" -A x64 `
  -DPSPRECOMP_PROFILE=sims2 `
  -DSIMS2_GENERATED_DIR="$PWD/out/sims2-from-iso/generated"
```

Build the required `sims2_*_tests` targets before running CTest. Do not build the
`sims2` target merely to relink a host-only test: that also builds every generated
AOT unit. The GitHub Actions workflow in `.github/workflows/build.yml` runs Windows/Linux
source/publication checks and synthetic tests without access to a disc. It runs
on pushes, pull requests and manual dispatch. Runtime changes should receive focused checks; exhaustive
playthrough testing is not required for each change.

## Optional source-installer packaging

After fetching dependencies and installing `requirements-build.txt`:

```powershell
.venv\Scripts\python.exe profiles/sims2/tools/package_bootstrap.py
```

The resulting source-installer archive excludes game inputs and generated code,
even if its local `work/` directory already contains a prepared game. It bundles
Python and source/development files; users supply their own ISO and C++ build tools.
Only distribute a reviewed source-installer archive. Never distribute a prepared
`work/`, `install/`, `generated/`, memory stick or playable development folder.

## Optional Windows Vulkan build

Install the Vulkan SDK (providing `glslangValidator` on PATH), then use
`build.py "D:\Games\The Sims 2 USA.iso" --graphics VULKAN --workspace out/sims2-windows-vulkan`.
This builds the SDL/Vulkan shell with the same F1 menu. Use a separate workspace
from the D3D11 build. Windows defaults to D3D11; switching backends currently
requires choosing the build, rather than changing a live renderer in F1.

## Source-installer releases

The Windows setup zip carries Python, the host/recompiler sources, development
SDKs and a host-only launcher. Import it into RecompLauncher and select the USA
ISO. Its first prepare step compiles locally and validates startup with an
isolated memory stick. The Linux setup archive carries source, setup.sh and
launch.sh plus a launcher manifest; it needs the listed system development tools
and compiles locally too. Both builds force host-test and timing-research modes
off for the game. Normal window close returns success; bounded diagnostic stops
remain distinct error/status codes.

Public assets contain no BOOT.BIN, ISO, saves, generated AOT code or playable game
binary. Do not upload prepared work/ or out/ directories. Release filenames use
win64/linux-x64 setup labels because RecompLauncher excludes assets whose names
contain source/debug/symbols; release notes explicitly identify them as source
installers. Shell scripts are shipped with LF endings and executable permissions.

Synthetic host checks can run without a disc:

```sh
cmake -S . -B out/host-tests -DPSPRECOMP_PROFILE=sims2 -DSIMS2_HOST_TESTS_ONLY=ON
cmake --build out/host-tests --config Release --parallel 2
ctest --test-dir out/host-tests -C Release --output-on-failure
```

On Linux install the listed dependencies and run display tests under Xvfb or a
headless compositor. The empty registration fixture cannot run a game and is
never used by ISO setup. `.github/workflows/build.yml` is the active Windows/Linux
workflow; `docs/ci-example.yml` retains a matching example.
