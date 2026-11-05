# Third-party sources and reference projects

## Bigmission Recomp (MIT)

Source: https://github.com/jackgllghr/bigmission-recomp

Pinned reference revision: `7630758036ecaf841f4bb6732bfe6cd080a27902`.

Adapted from `profiles/sarusaru/` at that revision:

- `host/startup_hle.*`: namespace changed; save-state friendship removed.
- `host/kernel/early_kernel.*` and `guest_clock.hpp`: namespace changed;
  snapshot/restore methods removed because this framework revision does not
  provide the required snapshot boundary API. Scheduler, stack allocator,
  partition blocks, callback and early ThreadMan behavior retained.
- `tests/startup_hle_tests.cpp` and `tests/early_kernel_tests.cpp`: namespace changed.
- The module-start argument, return-stub and memory reservation approach in
  `host/main.cpp` follows that project's `host/session.cpp`.
- `host/umd/*`, `io/*`, `platform/documents.*`, `text/shift_jis.*`, `rtc/*`,
  `modules/*`, `kernel/sync.*`, `kernel/interrupts.*`, `ge/*`, `display/*`,
  `audio/audio.*`, `sas/*`, `ctrl/*` and their corresponding
  tests/synthetic ISO helper: namespaces and diagnostic environment prefixes
  renamed, save-state friendship removed. Sims 2 changes add RTC UTC ticks and
  a daylight-saving-aware local offset, memory-stick callback registration,
  legacy network-module IDs, system settings, and module identification from
  PSP headers at archive sector extents. Module load state does not imply that
  the codec/font exports are implemented.
- The reusable framework scratchpad mapping follows this project's
  `src/guest_memory.cpp` and `include/psprecomp/guest_memory.hpp` at the same
  revision, with added bounds checks and an independent regression test.
- `host/save_data/*`, `utility/dialog.hpp`, `utility/savedata_dialog.*`,
  `audio/atrac.*` and their tests are adapted from the same reference (including
  its Yakumo-derived AES wrapper, crypto and SFO handling). Namespaces and
  environment variables were renamed and save-state friendship removed.
  Changes add a Sims 2 storage location, native slot selection, Windows atomic
  file replacement, save-path validation, and ATRAC metadata queries. Decoder
  failures stop explicitly rather than synthesize silence. Interactive save
  operations no longer automatically select the first slot.

Copyright (c) 2026 PSPRecomp contributors. The complete MIT license is retained
in `third_party/bigmission/LICENSE` and applies to these adapted components.
Sims 2 compatibility is being tested independently; the source project's working
game status does not establish compatibility for this profile.

## Yakumo graphics state (MIT, through Bigmission)

`host/gpu/ge_state.*`, `triangle_indices.hpp`, and graphics state tests originate
from https://github.com/TeamGDB/Yakumo at revision
`39ac05f1a166703430d18826bc5877f4feb6b511`, as documented by Bigmission.
The related `patch.hpp` and `geometry_outline.hpp` helpers are from Bigmission.
Namespaces were changed; the GE state machine decodes draw calls independently
of the D3D11 rasterizer attached by this profile. Full MIT notices are retained
in `third_party/yakumo/LICENSE`.

`host/gpu/texture_decode.*` and its tests are adapted from Bigmission's pinned
tree. Texture cache keys now hash all stored rows and palette entries and include
palette-window state. The D3D11 backend in `native_renderer.*` is independently
implemented against the decoded GE state and Microsoft's APIs. Its per-vertex
lighting equations are adapted from Bigmission/Yakumo's MIT `gpu/shaders/ge.vert`.
No Vulkan renderer source or game-specific graphics patches were imported.
The backend writes render targets back to guest memory for presentation and
GE transfers; native GPU pixel tests use the Windows WARP device.

## Behavioral specifications

The tiny-AES-c sources used by the AES wrapper are retained under
`third_party/tiny_aes`, including their Unlicense notice, through Bigmission's
pinned source tree. https://github.com/kokke/tiny-AES-c

New FPL, system-parameter and legacy module bindings use PSPSDK's public API/NID
definitions, and FPL error/size observations in pspautotests. No implementation
source from those projects is incorporated:

- https://github.com/pspdev/pspsdk/tree/master/src/user
- https://github.com/pspdev/pspsdk/tree/master/src/utility
- https://github.com/hrydgard/pspautotests/tree/master/tests/threads/fpl

The native Windows font substitute is independently written against PSPSDK's
font ABI and Microsoft's GDI API documentation. It rasterizes the installed
Arial font, does not bundle font data, and differs from Sony PGF typography.
Its library allocation/free callbacks execute the game's own registered AOT
functions with bounded dispatch and a guard against thread switching.

- https://github.com/pspdev/pspsdk/blob/master/src/font/pspfont.h
- https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getglyphoutlinew

The MPEG ring/context layout and elementary-stream buffer management follow
the framework's existing MIT VCS bindings in `profiles/vcs/host/vcs_profile.cpp`;
the Sims 2 MPEG module adds resumable guest ring callbacks and stream demux.
`host/movie/psmf_demuxer.*`, `avc_decoder.*` and `host/audio/atrac_decoder.*`
are adapted from Bigmission/Yakumo at the revisions above, with namespace and
build macro changes and save-state friendship removed. These use FFmpeg's
H.264 and ATRAC3plus decoders. The shared FFmpeg libraries and import headers
come from `profiles/sims2/third_party/ffmpeg`; the LGPL 2.1 notice is retained in
`third_party/ffmpeg/COPYING.LGPLv2.1`. The local diagnostic package includes
the required avcodec and avutil DLLs. `host/movie/color.cpp` adapts the color
equations and pixel packing from Bigmission's `psmf_player.cpp`, with crop and
bounds checks added. The native Windows framebuffer window and keyboard input
are independently implemented using Win32/GDI. The native PCM sink uses one SDL3 playback device with nine bound streams, following
Bigmission's MIT-licensed audio architecture. Gamepad polling uses SDL3 gamepad mappings.

The integer packing instructions follow PSPDev's VFPU behavior documentation:
https://pspdev.github.io/vfpu-docs/#vi2ucq
Color packing follows its VT4444/VT5551/VT5650 descriptions. The volatile-memory
implementation is independently written against PSPSDK's API and the hardware
observations in `pspautotests/tests/power/volatile`:
https://github.com/pspdev/pspsdk/blob/master/src/user/pspsuspend.h
https://github.com/hrydgard/pspautotests/tree/master/tests/power/volatile
Texture matrix coordinate generation and projective sampling follow PSP hardware observations:
https://www.ppsspp.org/docs/psp-hardware/gpu/ge-vertex-pipeline/
https://www.ppsspp.org/docs/psp-hardware/gpu/raster-pipeline/
No PPSSPP implementation code is incorporated.

GE SIGNAL SYNC behavior was implemented from the PSP hardware behavior documented
by PPSSPP's developers, with an independent callback/order regression test:
https://www.ppsspp.org/docs/psp-hardware/gpu/display-lists/

The MPEG CSC argument layout was checked against Jpcsp's public HLE ABI;
no implementation source from that GPL project is incorporated:
https://github.com/jpcsp/jpcsp/blob/master/src/jpcsp/HLE/modules/sceMpeg.java

## God of War PSP Recomp (reference only)

Source: https://github.com/DrummingBird1/gow-psp-recomp

Reference revision: `afc345b4033e0f5a1487617832b407ac85b9ad03`.

This is a separate C runtime/recompiler under GPL-3.0. Its build-on-user-machine
distribution, staged runtime milestones and portable toolchain documentation are
architecture references. No source from that project is incorporated here.

Neither project's own launcher establishes compatibility with recomplauncher.com.

## SDL3

SDL 3.4.20 is fetched from its official SDK under its zlib license (third_party/SDL3/LICENSE.txt).
Official SDK: https://github.com/libsdl-org/SDL/releases/tag/release-3.4.20
Archive: SDL3-devel-3.4.20-VC.zip
SHA256: 1ec3009655ee37b1f73e52b3b120acc5afd8abaf9532f3303b1bf1dfe91b5969
The x64 runtime DLL is required beside sims2_probe.exe.

## Dear ImGui

The F1 settings overlay uses Dear ImGui v1.91.9b (commit f5befd2d29e66809cd1110a152e375a7f1981f06), including its Win32 and Direct3D 11 backends. MIT license: `third_party/imgui/LICENSE.txt`. Source: https://github.com/ocornut/imgui.

## Dependency distribution

This Git repository retains license notices but does not contain the downloaded
SDL/FFmpeg SDK headers, import libraries or runtime DLLs. The dependency fetcher
uses SHA-256 pins in config/dependencies.json. Its FFmpeg 7.1 files match the
public PSPRecomp framework revision named in that manifest; LGPL source is
available at https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz . Local game packages
include the required shared libraries and these notices.

## Portable host additions

The SDL3 and SDL GPU ImGui backends are from the same MIT-licensed ImGui commit
listed above. The portable renderer uses SDL GPU's Vulkan driver and independently
implements PSP GE draws using the same vertex and lighting rules as our D3D11
backend. Host GLSL shaders are compiled by glslang at build time.
Linux links the installed FFmpeg, FreeType and fontconfig libraries. Their licenses
and sources are supplied by the distribution packages; the repository does not
vendor those libraries. SDL's fallback source archive is pinned by SHA-256 in
CMake. The abilliontokens SDL/Vulkan ports informed platform behavior; their Xbox
GPU/runtime source is not incorporated into this PSP renderer.
