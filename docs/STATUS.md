# Status and known limitations

## Playable preview

The current Windows build reaches live gameplay. Opening movies, Create-A-Sim,
the service-station introduction/tutorial, Paradise Place arrival, initial
Espiritu Estate interactions, refrigerator purchases and initial Buy/Design
operations have been exercised. Encrypted save/load has been checked across
fresh processes, including persistent furniture/design changes and a collected
secret. Playtesting reports smooth gameplay and resolved audio choppiness.

The project has 39 local automated checks covering the framework, kernel/HLE,
I/O, save crypto, GE/display/audio, controller sampling, GPU rendering, settings
persistence, input options, HDR conversion and presentation limits. The documented CI template uses synthetic framework tests and does not receive a game image.

Focused rendering checks include native, 2x/4x/8x and fractional targets;
texture-matrix menu highlights; framebuffer transfers/feedback; ultrawide and
narrow-aspect HUD placement; and GPU anisotropic sampler binding. A 900-frame
startup check at a 30 FPS presentation cap preserved guest timing and draw count
while reducing 3D presentations.

A full playthrough is not a release gate. Later defects should be reported with
reproduction steps and checked with focused regressions.

## Known limits

- Windows x64 / Direct3D 11 is the established playable build. The new SDL/Vulkan
  backend compiles on Linux; renderer/audio/gamepad tests and the X11/Wayland F1
  shell checks pass. The native Linux game passes a 900-frame startup check with
  40,684 GPU draws and 429 decoded movie frames. All 37 Linux checks pass; the
  original 39 Windows checks and four Windows Vulkan renderer checks pass too.
  Physical Steam Deck/Gamescope and Fedora/Arch playtesting remains. See [LINUX.md](LINUX.md).
- Later quests and some PSP HLE/graphics operations remain unverified or incomplete.
- Physical controller/hardware combinations need user feedback; virtual SDL device tests pass.
- HDR shader conversion and SDR fallback are tested; physical HDR output and
  exclusive display switching need user hardware checks.
- Original PSP save interchange is not established.
- Fonts use Windows Arial or Linux FreeType/fontconfig substitutes rather than Sony PGF typography.
- FPS caps do not unlock the PSP game engine above its native timing.
- Xbox-specific cheat toggles and a replacement vector HUD are not implemented.

Report reproducible issues through the repository issue tracker. Include the
commit, system/GPU, relevant F1 settings, input device, expected behavior and
actual behavior. Redact personal paths and credentials from logs. Keep disc
images, extracted game files, saves and generated game code out of public issues.
