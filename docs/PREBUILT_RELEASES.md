# Prebuilt Windows releases

Starting with v0.3.0, the Windows release contains precompiled native game code.
Players supply their supported USA ULUS-10031 ISO. The preparation program checks
the disc ID and exact BOOT.BIN hash and extracts the required local files. Game
assets are read from the user's ISO. The setup program does not invoke a compiler,
CMake, a recompiler or a network dependency installer.

This differs from the v0.2.x source installers, which generated and compiled the
game during first setup. Downloading and ISO verification still take time; the
launcher may perform its own full-disc verification before calling our prepare
program. No instant-install timing is promised.

The Git repository remains source-only and excludes generated sources, binaries,
disc files and saves. The Windows **release artifact** intentionally includes
compiled native game code and independent host/dependency binaries. It contains
no original BOOT/EBOOT, ISO/CSO, game assets, saves, generated C++ sources, object
files, debug databases or development SDKs. Project licensing covers the project's
own code and does not grant rights to third-party game content.

## Packaging

Build the optimized D3D11 runtime from a supported local disc. Configure Vulkan
with the same local AOT archive and matching toolchain/configuration. Then run:

```powershell
out/release-python/Scripts/python.exe profiles/sims2/tools/package_playable.py `
  --d3d11 out/playable-release/d3d11/profiles/sims2/Release `
  --vulkan out/playable-release/vulkan/profiles/sims2/Release `
  --work-dir out/playable-release/packaging `
  --output out/releases/0.3.0/Sims2PSP-win64-setup-0.3.0.zip
```

The packager uses an explicit payload list. It freezes the validation/extraction
program, adds both runtimes, the launcher manifest and dependency notices, and
writes per-file SHA-256 hashes. It does not copy the game's prepared folder or
the build workspace. Existing private workspace contents cannot enter the ZIP
through a recursive copy of an install directory.

Validate the frozen setup against a supported local ISO and an invalid image,
then check startup and rendering with both packaged backends using isolated
settings and memory sticks. Check the ZIP inventory, hashes and personal-path
or credential scans before publication. Never upload validation logs or saves.

Linux remains a source installer in v0.3.0. A portable native Linux package needs
its own runtime-dependency and distribution compatibility validation.

## Upgrades from source installers

v0.3.0 fresh preparation used `game/`, while the older source installer used
`work/install/game/`. RecompLauncher can retain its prepared flag across updates
and skip the new preparation step. v0.3.1 fixes the resulting missing-BOOT error:
the runtime verifies the disc ID and executable SHA-256, then loads that
executable directly from the ISO if the prepared copy is absent. An existing
prepared copy must still match the supported disc. No compilation or save
migration is required. The same fallback covers a prebuilt install whose local
extracted files were removed; the ISO must remain available.
