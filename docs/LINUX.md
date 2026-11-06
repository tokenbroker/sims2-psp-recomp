# Native Linux, Wayland, X11 and Steam Deck

The x86-64 portable runtime uses SDL3 windows/input/audio and SDL GPU's **Vulkan**
backend. It runs natively; Wine/Proton is not required. The Direct3D 11 Windows
build remains available. Both renderers use the same F1 settings UI.

## Build dependencies

Ubuntu 24.04 / Debian-family systems:

```sh
sudo apt install build-essential cmake ninja-build pkg-config python3 \
  glslang-tools libvulkan-dev mesa-vulkan-drivers libavcodec-dev libavutil-dev \
  libfreetype-dev libfontconfig1-dev fonts-liberation \
  libwayland-dev libxkbcommon-dev libdecor-0-dev libx11-dev libxext-dev \
  libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxtst-dev \
  libasound2-dev libpulse-dev libdrm-dev libgbm-dev libegl1-mesa-dev libudev-dev
```

Fedora-family systems need a C++20 compiler, CMake, Ninja, Python, pkgconf,
`glslang`, `vulkan-loader-devel`, Vulkan GPU drivers, `SDL3-devel`,
`freetype-devel`, `fontconfig-devel`, and FFmpeg development packages providing
`libavcodec.pc` and `libavutil.pc`. Package availability/codecs depend on the
repositories enabled on that installation. Check with:

```sh
pkg-config --modversion libavcodec libavutil freetype2 fontconfig
```

Arch-family systems use `base-devel cmake ninja python pkgconf glslang
vulkan-icd-loader sdl3 ffmpeg freetype2 fontconfig ttf-liberation` plus their GPU's
Vulkan driver, for example `vulkan-radeon` on AMD or `vulkan-intel` on Intel.
These package names describe the expected development environment; Fedora and
Arch builds still require validation on those distributions.

If SDL3 3.4+ is unavailable, CMake builds the hash-pinned official SDL 3.4.20
source archive. Install the Wayland/X11/audio development packages above first
so those SDL drivers are enabled. The build summary lists detected drivers.

## Build and run

Use your own supported USA ULUS-10031 ISO:

```sh
./setup.sh '/path/to/The Sims 2 USA.iso' --jobs 2
./out/sims2-linux/install/run.sh '/path/to/The Sims 2 USA.iso'
```

The first build compiles the recompiler and all locally generated game functions.
Keep its workspace to reuse completed work. On low-memory devices use `--jobs 1`.
A workspace on a native Linux filesystem is preferable to a mounted Windows drive.
No game input, generated code or playable binary belongs in the public repository.
The install folder includes a Linux `recomplauncher.json` for folder import; its
import/Play flow still needs a Linux RecompLauncher check. The install folder remains private. Runtime libraries from the operating system,
including FFmpeg, Vulkan and fontconfig, must remain installed; this is not a
universal AppImage or Flatpak.

To select a desktop protocol explicitly:

```sh
SDL_VIDEO_DRIVER=wayland ./out/sims2-linux/install/run.sh '/path/to/game.iso'
SDL_VIDEO_DRIVER=x11 ./out/sims2-linux/install/run.sh '/path/to/game.iso'
```

Usually leave this unset and let SDL choose. Wayland's compositor owns fullscreen
and display-mode requests; a requested exclusive mode may become compositor
fullscreen. Window positioning and monitor mode changes are not assumed to be
available. Suspended/minimized swapchains skip presentation and continue processing
events. Input is neutral when the game lacks keyboard focus.

Settings: `$XDG_CONFIG_HOME/Sims2PSPRecomp/settings.ini`, falling back to
`~/.config/Sims2PSPRecomp/settings.ini`.
Saves: `$XDG_DATA_HOME/Sims2PSPRecomp/memstick`, falling back to
`~/.local/share/Sims2PSPRecomp/memstick`. `SIMS2_SETTINGS_PATH` and
`SIMS2_MEMSTICK` override these locations.

## Steam Deck / SteamOS

The Vulkan/SDL path supports a controller-only way to open settings: **click both
sticks (L3 + R3)**. F1 also works, including a Steam Input binding to F1. The same
Display, Quality, Performance, Audio and Controls tabs are used on desktop and
handheld. D-pad/left-stick navigation and the south face button select ImGui
controls; the left stick/d-pad and physical face-button positions drive the PSP.
A 1280 x 800 output preset is included. Begin at native or 2x internal resolution,
60 FPS presentation cap and the aspect ratio you prefer, then tune on hardware.

Build using a development container or another compatible Linux installation
rather than disabling SteamOS's read-only system. Add the resulting `sims2` binary
as a non-Steam game with quoted arguments for the prepared BOOT.BIN and your ISO;
or add `run.sh` and pass the quoted ISO path. Select a Gamepad Steam Input layout.
Use the native launch path without forcing Proton. Required runtime dependencies
must also be available inside the environment Steam launches it in. A Steam Linux
Runtime/Flatpak distribution package is not provided yet.

**Physical Steam Deck, Gamescope and suspend/resume testing is outstanding.** The
current validation uses Ubuntu/WSL, Mesa llvmpipe, a headless Wayland compositor
and Xvfb. Those results verify rendering and the shell, not handheld performance.

## Feature coverage and verification

- Vulkan pixel tests pass at 1x, 2x, 4x and 8x, including textures, depth/blending,
  alpha rejection, framebuffer transfers and streaming vertex uploads.
- All 37 Linux checks pass, including FreeType fonts, SDL audio, virtual gamepads,
  widescreen framing, render scaling and the SDL shell.
- F1 pause/resume, output presentation at 1280 x 800, 2x internal scaling and
  save-dialog cancellation pass on X11 and headless Wayland.
- The native Linux game passes the 900-frame startup check: 40,684 rendered GE
  draws, 429 movie frames and a visible menu selection highlight.
- Windows D3D11 remains the existing tested gameplay path; Windows Vulkan also
  passes the renderer pixel checks at 1x, 2x, 4x and 8x.
- Linux HDR is disabled and reported as SDR. Windows HDR controls remain in D3D11.
- Presentation caps do not unlock the PSP simulation above its native timing.

Reference architecture: the [Bustin' Out port](https://github.com/abilliontokens/bustinout-recomp)
and its XboxXBERecomp SDL/Vulkan runtime. Its Xbox GPU implementation is not used
for the PSP GE renderer. SDL GPU documentation: [GPU API](https://wiki.libsdl.org/SDL3/CategoryGPU).
