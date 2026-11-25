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

## v0.3.0 Windows prebuilt setup

Windows release packaging now includes both compiled native runtimes and a small
validation/extraction program. No compiler or source build runs during Windows
setup. The ISO is still required and checked; assets are read from it during play.
Linux continues to use its explicitly labelled source installer.

The packager copies an explicit runtime/notice list rather than an existing
prepared game folder. Payload hashes and the supported-disc identity are recorded
in `release-files.json`. Original disc files, saves, generated sources and SDKs
are excluded. FFmpeg DLLs must match the pinned upstream hashes; those vendor
binaries may retain their upstream build-path strings. Local personal paths and
credentials must not enter the project-built executables or release text.

The frozen no-compile preparation path was exercised with build tools removed
from PATH and rejects an invalid disc without writing game files. Both exact packaged runtimes passed isolated 900-frame startup checks, and
the packaged desktop launcher passed D3D11/Vulkan/default selection checks. See
[prebuilt packaging](PREBUILT_RELEASES.md).

## v0.3.1 prepared-state upgrade fix

An installed v0.2.x-to-v0.3.0 update retained the launcher's prepared state while
the new manifest pointed at `game/` instead of `work/install/game/`. The runtime
stopped before boot because the new extracted BOOT path did not exist. Fresh
setup/startup checks had not covered this retained-state migration.

The runtime now validates the ISO's disc identity and executable hash first and
loads its executable directly from the verified disc when the prepared file is
absent. Existing prepared files are still hash-checked. No build tools or changes
to user saves/settings are needed. The public runtime package includes the fix
for both Windows renderers.

Regression validation for v0.3.1: both exact packaged runtimes reached 900 frames
with no prepared BOOT file and no preparation step; neither created that file.
Both rejected an invalid ISO and a mismatching existing prepared executable.
The framework tests passed, including memory/file SHA-256 equivalence and the
standard empty-input digest. This covers the retained-prepared-state failure
missed by the earlier fresh-install check.

## Cutscene and handheld audio reports

Sound breaks during cutscenes and frame-drop/audio stutter on a ROG Ally remain
under investigation. A paced local movie test and a PCM scheduling experiment
did not establish a fix for the reported event. Optional per-channel queue
telemetry is now available in source builds; the normal playback order is
unchanged. See [audio reports and diagnostic limits](AUDIO_REPORTS.md).

## Open issue work: handheld performance and European support

For issue #1, PCM blocks are copied out of guest memory in one bounded operation
on little-endian hosts, removing per-sample address resolution. Tests verify
signed stereo/mono PCM, uncached memory aliases and volume delivery. A shared
F1 low-load preset lowers render load without resetting player input/audio
preferences. Save/reload preservation and both presenter paths are tested.
The ROG Ally report remains open pending physical-device feedback.

For issue #2, a metadata-only owned-disc inspector and malformed-ELF tests are
available. No European executable has been supplied, so EU compatibility is
not enabled or claimed. See [regional support](REGIONAL_SUPPORT.md).

The v0.3.2 packaged D3D11 and Vulkan runtimes both passed a paced 1,200-frame
movie/save-load/gameplay check with SDL dummy-device audio enabled and isolated
save copies. All 45 Linux host tests and the targeted Windows PCM, preset,
settings-menu and disc-inspection checks passed. No physical Ally or European
compatibility claim follows from these checks.

## v0.4.0 European executable and languages

The owned ULES-00211 copy is now supported with its own 494-unit corpus and
strict executable identity, not a USA identity bypass. D3D11 and Vulkan reached
localized Create-A-Sim; English/French/German/Spanish/Italian labels were checked.
A new French character produced an encrypted regional save; both backends
reloaded copies and ran the opening story sequence with paced dummy-device audio.

Runtime selection follows the actual ISO. USA and EU preferences are separate,
and launcher-page Europe controls include language selection. EU remains Native
engine timing until its game-specific high-FPS hook is validated. Existing USA
features remain available. See [regional validation limits](REGIONAL_SUPPORT.md).

Final regional validation also checked all four packaged runtime paths with
stale prepared data from the other known region, encrypted save loads, and
wrong-region rejection. The European Linux host configuration passed all 49
synthetic checks. Launcher manifests were schema-checked, with independent USA
and Europe settings. Native Windows startup/UI checks and European settings/
language tests passed; a full EU playthrough remains unverified.


## v0.4.1 European feature parity

Europe now exposes the same Native/experimental 120/240 Hz engine choices as
USA. Both use the shared F1 controls, settings validation, display clock and
timestep correction; only verified regional code/data addresses differ. A
relocated instruction signature is checked before installing either high-rate
hook. Native remains the default and installs no override.

Bounded Windows D3D11 EU gameplay runs sampled the world clock at matched guest
times from 40 through 45 seconds. Native, 120 Hz and 240 Hz each advanced five
game minutes across that interval. The 240 Hz run started one minute ahead after
loading; progression matched. High-rate context fields held the requested rate
and reciprocal timestep. Scripted movement rendered in the garage. These checks
use private copied save fixtures and do not establish cross-region save support.

Both Windows regional timing/settings tests passed. All 49 Linux EU host tests
passed under Xvfb with dummy audio. Packaged D3D11/Vulkan gameplay checks cover
both regional executables at 240 Hz. The initial concurrent long EU Vulkan run
exceeded its timeout after loading the save; the outstanding USA run was stopped.
Shorter serial runs check loading and gameplay without that concurrent workload. These are bounded tests, not a complete
playthrough, audible audio validation, sustained physical-monitor FPS or physical
Steam Deck validation. Regional language content, save IDs and settings paths
remain intentionally distinct.


## v0.4.2 opening-story audio scheduling

The supplied review video identifies the opening desert-driving story sequence.
The runtime now queues ready PCM before guest-clock sleeping and allows 200 ms
of per-channel queue headroom before loading backpressure. This preserves guest
samples and avoids repeatedly inserting recovery silence. The larger permitted
queue can add buffering latency; it is not an unlimited queue.

Matched private fixture runs reached the same scene and guest sample counts.
Headless and normal hidden-window diagnostics showed reduced blocking/queue
starvation on the main continuous channel, with remaining mixed channel results.
The normal window test exercised the selected 4x scale; early headless runs did
not apply that F1 setting. Windows native audio tests and 49 Linux host tests
passed. See [audio validation and limits](AUDIO_REPORTS.md).

This is a scheduling mitigation. Physical-device audible reproduction and the
reviewer's retest are still required; the audio issue remains open. It does not
claim to fix the separate reported native access violation.


## v0.4.3 Vulkan point-list compatibility and fatal-error reports

A new point-list rendering test exposed a missing required `gl_PointSize` output
in the Vulkan vertex shader (VUID topology-08773). The corrected shader passed
point rasterization and Vulkan validation. Linux CI now requires clean validation
when the dedicated option is enabled. Windows native faults now leave a small,
module-relative crash report rather than only the last ordinary gameplay log.
Vulkan startup logs SDL and graphics-driver versions.

The reported save-to-Sim crash was not reproduced on the local RX 6950 XT;
normal off-screen swapchain runs reached gameplay before and after correction.
The Radeon 890M report remains open. See [validation and limits](VULKAN_CRASHES.md).

## v0.4.4 Vulkan failed-submission handling

The Radeon 890M report on v0.4.3 showed a fault inside `amdvlk64.dll` with no
error text. The reporter's driver build was inspected at the faulting offset:
a command was recorded into a command buffer that was no longer recording. The
same fault reproduces on the local RX 6950 XT by reusing a submitted SDL GPU
command buffer.

The renderer kept its command buffer after a failed submission, and the runtime
flushed the renderer once more before printing the error. v0.4.4 forgets the
buffer before reporting the failure, logs the runtime error as soon as it is
caught, and skips the final flush after an error. A renderer test injects a
failed submission and checks recovery; it crashed the previous renderer.

This removes the driver fault. It does not explain why submission failed on the
890M, which no local AMD GPU reproduces. The report stays open; the new log
line names the Vulkan result. See [validation and limits](VULKAN_CRASHES.md).

## v0.4.5 Vulkan device-memory exhaustion

The 890M retest on v0.4.4 stopped with `vkQueueSubmit
VK_ERROR_OUT_OF_DEVICE_MEMORY` instead of a driver fault. Every GE block
transfer ended the current command buffer, and every submission cycled the
32 MB staging arena and 16 MB vertex buffer, so a GPU that fell behind
accumulated 48 MB per outstanding submission.

v0.4.5 does not submit for transfers that touch no render target and cycles
those buffers only when they wrap. The local integrated-GPU save-to-gameplay
run drops from 74,569 to 3,604 submissions and from 4.4 GB to 175 MB of shared
graphics memory, with an identical final frame. Not yet confirmed on the 890M.
See [validation and limits](VULKAN_CRASHES.md).

## v0.4.6 Late frames no longer slow the game

With the v0.4.5 memory fix, the Radeon 890M handheld ran Vulkan but reported
under 30 FPS, slow-motion movement and stuttering audio. Both windows presented
every guest frame and waited for the display. A presentation that blocks longer
than a guest frame (a handheld or driver frame limiter, a display slower than
the engine rate) therefore slowed the whole simulation, audio included.

v0.4.6 drops the presentation of a frame that is already late and keeps the
guest clock in real time; the picture still updates at least every 100 ms.
With a simulated 33 ms presentation the native-rate game holds 0.99x on Vulkan
and 0.98x on D3D11; with a simulated 60 Hz display at the 120 Hz engine rate it
holds 0.95x. A device too slow to simulate the chosen engine rate is still
limited by its CPU.

The log now ends with a `[pacing]` line (speed, frames skipped, average game
work and presentation time) and a `[settings]` line (resolution, render scale,
engine rate, vsync, frame cap) to tell those cases apart.

## v0.4.7 Cutscene stutter and sound breaks

A player on default settings (D3D11, 4x, VSync, Native) reported smooth
gameplay but stutter and cut-out sound in cutscenes. The opening desert story
sequence reproduces it: on a Ryzen 7 9800X3D it cost 0.68-0.89 s of host time
per second of game (gameplay afterwards: about 0.45), and in its heaviest ten
seconds 343 of 534 frames ran over the 16.7 ms budget. A slower CPU falls
behind there.

A sampling profile of that sequence found three avoidable costs:

- The generated game code is compiled at `/O1`, which does not enable compiler
  intrinsics, so the 4-byte `memcpy` in every guest load and store stayed a real
  call into the C runtime (about 13% of the game thread). MSVC builds now use a
  direct unaligned access; other compilers keep `memcpy`.
- CPU vertex lighting converted every material and light colour, and
  normalized every directional light, once per vertex. Both renderers now share
  one implementation that prepares those once per draw; the per-vertex
  arithmetic is unchanged.
- The D3D11 renderer read an environment variable on every draw.

The sequence now costs 0.40-0.52 s per second of game, and 8 of those 534 frames
run over budget. A 6,000-frame headless run is identical before and after:
guest timeline, draw counts, GE commands and the final frame.

Sound also broke on short host stalls because only one or two audio blocks
(23-46 ms) were queued ahead of the device. Sound that starts on an empty
stream is now placed behind a fixed 100 ms margin of silence, so a stall
shorter than that drains the margin instead of the sound. No samples are
dropped or stretched; audio is 100 ms later relative to the picture. The
loading backpressure limit rises by the same 100 ms.

The log now has an `[audio]` line counting real dropouts: blocks the game
queued without a gap that found the playback queue already empty.

Paced runs of the sequence (4,300 frames, Windows audio device, hidden window):

| Build | CPU | Frames not shown | Speed | Dropouts |
|---|---|---|---|---|
| v0.4.6 | full | 882 | 0.985 | 237 empty-queue blocks |
| v0.4.7, no margin | full | 173 | 1.000 | 1 (91 ms) |
| v0.4.7 | full | 188 | 1.000 | 0 |
| v0.4.6 | capped to 0.6 core | 1,229 | 0.891 | 422 empty-queue blocks |
| v0.4.7, no margin | capped to 0.6 core | 295 | 0.990 | 36 (1.6 s total) |
| v0.4.7 | capped to 0.6 core | 328 | 0.989 | 12 (0.5 s total) |

v0.4.6 has no dropout counter; its empty-queue count is the older, looser
measure. The CPU cap is a Windows job-object limit, a harsher, burstier stand-in
for a handheld than a uniformly slower CPU. It is not a measurement on the
reporter's device.

## v0.4.8 Vulkan frames wait for the whole frame's GPU work

A v0.4.7 log from the Radeon 890M handheld (Vulkan, 1280x720, 2x render
scale) reported speed 0.78, 229 of 678 frames late, and 84 audio dropouts
(8.9 s) over the boot movie, a save load and a heavy scene (91,359 draws, about
135 per frame on average).

The Vulkan renderer recorded every draw into one command buffer and submitted it
only at the framebuffer readback that ends each frame, then waited on a fence.
The GPU therefore started each frame only once the CPU had finished it, and the
frame cost the CPU time plus the GPU time. In the opening story scene the
readback took 30-37% of real time on Vulkan with the discrete GPU and 21-24%
with the integrated one, against 3-5% on D3D11.

- The Vulkan renderer now submits every 8 draws. The staging arena and vertex
  buffer already keep filling across submissions (v0.4.5), so this allocates
  nothing: peak GPU memory stays 187 MB dedicated and 217 MB shared on the
  integrated GPU. `SIMS2_TEST_SUBMIT_DRAWS=0` restores one submission per frame
  for measurement; the desktop launcher clears it.
- Both renderers convert draws of 384 or more vertices on up to three worker
  threads. Each vertex is converted on its own from read-only draw state, so the
  result is identical; vertex conversion time falls by about a quarter. Smaller
  cutoffs measured slower. `SIMS2_TEST_VERTEX_THREADS=0` converts on one thread.

Opening story scene, 3,600 frames, visible off-screen window, paced:

| GPU | Build | Readback, heaviest 10 s | Frames not shown | Speed | Dropouts |
|---|---|---|---|---|---|
| RX 6950 XT | v0.4.7 behaviour | 0.30 | 1,271 | 0.975 | 29 (380 ms) |
| RX 6950 XT | v0.4.8 | 0.10 | 790 | 0.996 | 5 (59 ms) |
| Integrated Radeon, 2 CU | v0.4.7 behaviour | 0.21 | 1,107 | 0.996 | 8 (69 ms) |
| Integrated Radeon, 2 CU | v0.4.8 | 0.08 | 574 | 1.000 | 1 (6 ms) |

"v0.4.7 behaviour" is the v0.4.8 build with both test hooks set to 0. A
6,000-frame headless run on each backend is identical to v0.4.7: guest
timeline, draw counts, GE commands and the final frame.

## v0.4.9 Vulkan frames no longer wait for the GPU

A v0.4.8 log from the same Radeon 890M handheld (Vulkan, 1280x720, 2x render
scale, about 85 s of mostly gameplay) still reported speed 0.78: 2,111 of 3,114
frames not shown, 246 audio dropouts (10.7 s) and 22.4 ms of work per frame
against a 16.7 ms budget. The same machine runs D3D11 at full speed, so the
cost is in the Vulkan path, not the CPU.

At the end of every frame the Vulkan renderer copied the rendered framebuffer
back to guest memory and stopped the game until the GPU had finished the whole
frame. How long that takes depends on the GPU's scheduling and power state, not
on how much there is to draw: on the development PC it measured about 1 ms per
frame on an idle GPU and 6-10 ms per frame while the same GPU was busy with
other work. The game does not need those pixels at that moment. A 3,114-frame
USA run with the readback removed entirely executes the identical GE command
stream.

- **Readbacks no longer block the frame.** `NativeRenderer::flush_frame` submits
  the frame with a copy of the target at PSP resolution and returns. The pixels
  are copied into guest memory when the GPU has finished them, normally at the
  next frame. The copy is completed at once, as before, when something uses
  those pixels first: a block transfer that touches the buffer, a texture read
  from it, the next draw into it, a render-scale change, a frame capture, or
  `flush()`. A buffer drawn again before its previous copy arrived waits for
  that copy, so the game never runs more than its buffers ahead of the GPU.
- **Guest writes survive a late copy.** The renderer keeps what guest memory
  held when the readback started and replaces only pixels that still hold that
  value, so pixels the game writes in between stay on top, as they would have
  after an immediate copy. One case differs: a pixel the game rewrites with the
  value it already had takes the rendered value. Code that reads the framebuffer
  directly from guest memory, without a transfer or a texture, sees a frame that
  is one frame old until the copy arrives. The game's boot, story scene and
  gameplay never do this.
- **Consecutive draws share one upload and one render pass.** A pass per draw
  switched the render target between its attachment and sampling layouts and
  stalled for a vertex copy at every draw. Draws to the same target are now
  queued and recorded together; anything that reads or replaces the target, or
  submits, records them first.
- **Fewer submissions.** The renderer submits every 32 draws instead of every 8.
  With no wait at the end of the frame, extra submissions only add scheduling
  round trips. A gameplay frame takes about 5 submissions instead of 15.
- The exit log gains a `[renderer]` line with per-frame draw recording time,
  readback time and time spent waiting for the GPU, on both backends.
- `[savedata]` log lines and save errors name the slot as `PSP/SAVEDATA/...`
  instead of the full host path, which included the user's home folder.

Test hooks, cleared by the desktop launcher: `SIMS2_TEST_SYNC_READBACK=1` waits
for every frame, `SIMS2_TEST_BATCH=0` records one pass per draw, and
`SIMS2_TEST_SUBMIT_DRAWS` sets the submission interval. A run with
`SIMS2_CAPTURE_BMP` waits for every frame, because a per-frame capture reads the
frame from guest memory.

USA boot, intro movie, save load and gameplay, 3,114 frames at the reporter's
settings, visible off-screen window, paced, idle GPU:

| GPU | Build | GPU wait per frame | Readback per frame | Late frames | Submissions |
|---|---|---|---|---|---|
| RX 6950 XT | v0.4.8 behaviour | 1,062 us | 1,140 us | 77 | 48,339 |
| RX 6950 XT | v0.4.9 | 5 us | 106 us | 15 | 14,850 |
| Integrated Radeon, 2 CU | v0.4.9 | 5 us | 104 us | 13 | 14,853 |

"v0.4.8 behaviour" is the v0.4.9 build with the three hooks set. Peak GPU memory
on the integrated GPU is unchanged (113 MB dedicated, 158 MB shared, against
113 and 159). The final frame of that run is byte-identical with and without
the hooks and on both GPUs. A 6,000-frame run of the European opening story
scene matches the shipped v0.4.8 build exactly: per-frame guest timeline, draw
counts, GE commands and the final frame, with work per frame falling from
13.1 ms to 6.7 ms. These are measurements on a desktop with an idle GPU. They do
not reproduce the handheld's slowdown, whose GPU wait the v0.4.8 log does not
break out; the new `[renderer]` line does.

## v0.4.10 Late frames stay visible; D3D11 frames no longer wait for the GPU

A player on a Core i5-10300H / GTX 1650 laptop (Windows 11, default settings)
reported under 10 FPS, never reaching 60. No log was attached.

Since v0.4.6 a frame that was already late was not presented, for up to 100 ms
of game time, whatever made it late. That helps when presenting blocks (a frame
limiter or a display slower than the engine). When the host is late because of
game work, a skipped presentation saves only its own time, about 0.2 ms on
D3D11, and the next frame is just as late. While the host stayed behind, the
picture updated once per 100 ms of game time: at most 10 FPS. The development
PC's own default-settings D3D11 log already had 1,616 of 6,000 frames hidden
with presentations averaging 213 us.

- **Late frames stay visible.** When presentations have recently averaged less
  than a quarter of a frame period (about 4 ms at 60 Hz), at most every other
  late frame is skipped, so the picture keeps at least half the game's frame
  rate. Expensive presentations are skipped as before. The pacing unit test
  covers both cases.
- **D3D11 frames no longer wait for the GPU**, as Vulkan has not since v0.4.9.
  The frame's framebuffer copy is queued and lands in guest memory when the GPU
  has finished it, normally at the next frame, and is completed at once when a
  transfer, a framebuffer texture, the next draw into that buffer, a render-scale
  change or `flush()` needs it. Pixels the game writes in between stay on top.
  The window shows the displayed buffer from its GPU copy at every scale, and the
  `[renderer]` line now reports D3D11 GPU waits. `SIMS2_TEST_SYNC_READBACK=1`
  restores the old wait for measurement; the desktop launcher clears it.
- **Laptops ask for the discrete GPU.** The runtime exports `NvOptimusEnablement`
  and `AmdPowerXpressRequestHighPerformance`. D3D11 uses the default adapter,
  and a hybrid-graphics laptop renders a program without a driver profile on its
  integrated GPU. A per-program choice in NVIDIA Control Panel still applies.
  Whether the reporter's game ran on the integrated GPU is not known; the
  `[graphics] Direct3D 11 adapter:` log line names it.

USA save load and gameplay, D3D11 at default settings (1920x1080, 4x, VSync),
3,114 frames, paced hidden window, RX 6950 XT:

| Case | Build | Presented | Shown per second | Speed | Dropouts | Readback per frame |
|---|---|---|---|---|---|---|
| Full CPU | v0.4.9 | 3,045 | 45.8 | 1.000 | 2 (373 ms) | 1,010 us |
| Full CPU | v0.4.10 | 3,099 | 46.6 | 1.000 | 1 (346 ms) | 158 us |
| CPU capped to 0.25 core | v0.4.9 | 2,261 | 22.6 | 0.652 | 41 (11.3 s) | 2,738 us |
| CPU capped to 0.25 core | v0.4.10 | 2,619 | 26.9 | 0.676 | 33 (8.0 s) | 2,329 us |
| 33 ms presentation | v0.4.10 | 961 | 14.4 | 0.999 | 1 (398 ms) | 153 us |

"Shown per second" is presentations over the run's wall time, startup included.
The CPU cap is the job-object stand-in used for v0.4.7: harsher and burstier than
a slower 4-core laptop, not a measurement on the reporter's hardware. Under it
v0.4.10 shows more frames, runs faster and loses less sound. An intermediate
build that never skipped cheap late frames showed 3,053 frames under the same cap
but lost more sound than v0.4.9 (10.3 s against 6.1 s in that pair of runs), so
skipping every other one is kept. The 33 ms case simulates a blocking display;
skipping still keeps the game at full speed. The Radeon 890M v0.4.8 log in issue
#1 averaged 5.8 ms per presentation, above the threshold, so that handheld keeps
skipping as before.

A headless 3,114-frame run is identical to v0.4.9: guest time, GE commands, draw
counts and the final frame. The GPU wait on a laptop GPU is not measured here.
