# Windows renderer selection

Current source supports a saved renderer choice under **F1 > Display > Graphics
backend**: installation default, Direct3D 11, or experimental Vulkan. Save,
quit and relaunch through `sims2.exe`. F1 reports the active renderer. Switching
does not replace the renderer of a running process.

The v0.3.0 prebuilt Windows ZIP includes both renderers without local compilation.
Source setup defaults to `--graphics BOTH`: D3D11 is the primary runtime at the
install root; Vulkan and its DLLs are staged under `vulkan/`. Both startup paths
are checked using isolated memory sticks during source setup and release validation. Explicit
single-backend builds remain available. Building both variants takes longer.
The selector is included starting with v0.2.5; v0.2.4 predates it.

`SIMS2_GRAPHICS_BACKEND=d3d11`, `vulkan`, or `auto` overrides the saved choice for
one launch. `auto` means the installation's primary renderer. This also provides
a recovery route when a saved choice cannot start. Missing runtimes produce an
explicit error; the launcher does not silently select a different renderer.

## Local validation

- Windows D3D11: 45 host/renderer checks passed.
- Windows Vulkan: 43 host/renderer checks passed, including 1x/2x/4x/8x pixel
  checks and the SDL settings window.
- Linux: 43 regression checks passed after the shared settings changes.
- A real Windows launcher check confirmed saved Vulkan selection and explicit
  D3D11/default overrides, without opening desktop windows.
- Vulkan startup reached 900 requests, 40,684 draws and 429 decoded movie frames
  on an RX 6950 XT. The menu highlight was visually checked.
- A copied-save native-rate Vulkan run reached 1,200 requests and 75,040 draws,
  rendered the estate scene after scripted movement, and exited with the expected
  diagnostic status. Its final resource report counted 2 render targets, 92
  textures and 10 pipelines.
- A separate 120 Hz run rendered the saved world and reached 2,400 requests,
  but did not exit within its 240-second budget. It is recorded as a timeout,
  not a passing gameplay check. High-rate Vulkan performance/teardown remains
  experimental. Begin with native engine timing when testing this renderer.

The subsequent [Vulkan performance fix](VULKAN_PERFORMANCE.md) removes per-draw
upload allocations and excessive pixel-copy overhead. The 4x/120 Hz saved-game
scenario now completes and exits cleanly. This supersedes the timeout as a
current result; physical-window performance still needs user feedback.

The [opaque presentation fix](VULKAN_PRESENTATION.md) also prevents PSP framebuffer
alpha from turning shadows and light cones into black patches in Vulkan output.

These hidden checks do not establish physical monitor FPS, audible playback
quality, a full playthrough or sustained high-FPS Vulkan performance. Test saves,
game-derived code, binaries and captures remain private local outputs. The local
companion build reused matching backend-independent MSVC/O1 AOT objects; normal
source setup compiles each variant independently.

v0.4.0 includes both renderers for USA and Europe. Renderer selection is saved
per region. The frontend routes European ISOs to `europe/`, with its Vulkan
companion under `europe/vulkan/`. Native EU timing is independent of the saved
USA experimental engine rate. Game-language changes require Save and restart.
