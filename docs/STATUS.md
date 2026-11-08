# Status and known limitations

## Playable preview

The current Windows build reaches live gameplay. Opening movies, Create-A-Sim,
the service-station introduction/tutorial, Paradise Place arrival, initial
Espiritu Estate interactions, refrigerator purchases and initial Buy/Design
operations have been exercised. Encrypted save/load has been checked across
fresh processes, including persistent furniture/design changes and a collected
secret. Playtesting reports smooth gameplay and resolved audio choppiness.

The v0.2.3 Windows suite has 43 automated checks covering the framework, kernel/HLE,
I/O, save crypto, GE/display/audio, controller sampling, GPU rendering, settings
persistence, input options, HDR conversion and presentation limits. The GitHub Actions workflow uses synthetic host/framework tests on Windows and Linux and does not receive a game image. Both jobs passed in the [first hosted run](https://github.com/tokenbroker/sims2-psp-recomp/actions/runs/42169377061) at commit `befbf5c`.

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
  40,684 GPU draws and 429 decoded movie frames. The host-only Linux suite passes 41 checks without an ISO; the Windows Direct3D suite
  passes 43. Windows Vulkan also has native host and renderer checks; see the release
  notes for the exact runtime exclusions.
  Physical Steam Deck/Gamescope and Fedora/Arch playtesting remains. See [LINUX.md](LINUX.md).
- Later quests and some PSP HLE/graphics operations remain unverified or incomplete.
- Physical controller/hardware combinations need user feedback; virtual SDL device tests pass.
- HDR shader conversion and SDR fallback are tested; physical HDR output and
  exclusive display switching need user hardware checks.
- Original PSP save interchange is not established.
- Fonts use Windows Arial or Linux FreeType/fontconfig substitutes rather than Sony PGF typography.
- Presentation caps and engine rate are separate. The latest source adds opt-in,
  experimental 120/240 Hz engine modes; full high-FPS gameplay remains unverified.
- Xbox-specific cheat toggles and a replacement vector HUD are not implemented.

Report reproducible issues through the repository issue tracker. Include the
commit, system/GPU, relevant F1 settings, input device, expected behavior and
actual behavior. Redact personal paths and credentials from logs. Keep disc
images, extracted game files, saves and generated game code out of public issues.

## v0.2.0 host improvements

SDL video shells retain event ownership during gamepad polling. The F2 graph and
status panel are shared across backends. F1 now includes keyboard/controller
rebinding, Classic/WASD presets, mouse camera rotation, wheel navigation, optional
D-pad repeat filtering, camera sensitivity/inversion and host input hints.
These hints do not replace native game artwork or add direct native-menu clicks.

Windows/Linux source-installer packages, a prerequisite checker, reproducible
benchmark/statistics tooling and an import-registration inventory are available.
The recorded RX 6950 XT hidden-window baseline preserved native menu timing at
1x/4x; it is not a visible gameplay FPS claim. A bounded 120 Hz wait experiment
did not increase the steady-state menu cadence and is disabled in production.
See BENCHMARKS.md and HARDWARE-VALIDATION.md for evidence and remaining limits.


## v0.2.1 launcher settings

The release manifest now exposes common graphics, audio and input controls in
RecompLauncher, using the same settings file as F1. All choices are checked
against the native settings reader/writer on both platforms. Linux manifests
retain the XDG settings location; Windows retains LocalAppData. Release contents
still require first-time local compilation from a supported USA ISO.


## v0.2.2 controller and display coverage

The L3+R3 settings shortcut now uses shared SDL controller sampling on Windows
Direct3D and SDL/Vulkan. It requires both sticks on one controller, toggles once
per press, respects focus and is suppressed during save selection. Windows
controller tests cover holding, re-pressing, focus changes and disconnects.

Local fresh Windows Vulkan host-only validation passed 39 checks, including
1x/2x/4x/8x renderer checks; the visible SDL shell was excluded from that run.
Linux shell checks pass on both X11 and isolated Weston Wayland. Windows CI
now compiles both backends, with Vulkan runtime GPU checks excluded on hosted
Windows runners that do not guarantee a Vulkan driver. This is not evidence of
physical Steam Deck, Gamescope or broad vendor-driver compatibility.


## v0.2.3 presentation statistics correction

F1/F2 now share one history that retains intervals over 250 ms; the Windows
Direct3D path previously discarded these stalls. Paused settings time and
unavailable output surfaces reset the measurement anchor. The graph reports
host presentation cadence, not engine FPS or monitor scanout. Deterministic
checks cover long stalls, pauses, clock discontinuities and ring rollover.

The first Windows Vulkan CI run compiled successfully but exposed a missing GPU
test classification: widescreen checks also instantiate the renderer. All six
Vulkan renderer/window tests now carry the `gpu` label. Hosted Windows excludes
that label; Linux continues to run those tests with software Vulkan.


## v0.2.4 distro preflight and validation

Setup checks actual H.264 and ATRAC3+ decoder availability as well as development
packages. ATRAC availability can be queried per codec; the bundled Windows FFmpeg
build includes ATRAC3+ but not baseline ATRAC3. Linux reports the installed codec
set, with baseline ATRAC3 optional. The shader compiler may be named either
`glslangValidator` or `glslang`.

The official Arch rolling container passed 43 host checks and the Wayland shell
check using llvmpipe in [this run](https://github.com/tokenbroker/sims2-psp-recomp/actions/runs/42191837434/job/127805488422).
Fedora 44's stock codec build failed the H.264 preflight; its documented CI recipe
adds RPM Fusion Free's codec supplement. With it, all 43 host checks and the Wayland
shell passed in the [Fedora run](https://github.com/tokenbroker/sims2-psp-recomp/actions/runs/42192373696/job/127807257072).
The updated local Windows suite passes 45 checks. These checks do not include a
game image or physical GPU/Deck gameplay.

## v0.2.5 installer and runtime update

- Windows dual-backend setup builds one local game-code archive and reuses it
  for D3D11 and Vulkan. Retry generation preserves unchanged source timestamps.
- The standalone launcher resolves the prepared `work/install` runtime and its
  selected renderer. Frozen setup embeds host shaders using bundled Python.
- The frozen installer reports v0.2.5 and successfully validates/extracts a local
  supported disc and generates all 575 game-code units. Generated output stays
  private; this check does not claim a timed fresh full compilation.
- Both renderers linked against a shared archive of existing local game objects
  and passed isolated 900-frame startup checks. Windows setup regression tests,
  frozen shader embedding, launcher path checks and package hash checks passed.
- This release also includes the previous high-FPS, Windows renderer selection,
  Vulkan staging/presentation and save-dialog fixes. Native timing and D3D11
  remain the Windows defaults. No complete high-FPS playthrough or physical
  Steam Deck validation is claimed.
