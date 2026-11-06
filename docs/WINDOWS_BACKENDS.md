# Windows renderer selection

Current source supports a saved renderer choice under **F1 > Display > Graphics
backend**: installation default, Direct3D 11, or experimental Vulkan. Save,
quit and relaunch through `sims2.exe`. F1 reports the active renderer. Switching
does not replace the renderer of a running process.

Windows setup defaults to `--graphics BOTH`: D3D11 is the primary runtime at the
install root; Vulkan and its DLLs are staged under `vulkan/`. Both startup paths
are checked using isolated memory sticks before setup records success. Explicit
single-backend builds remain available. Building both variants takes longer.
The published v0.2.4 installer predates this selector.

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

These hidden checks do not establish physical monitor FPS, audible playback
quality, a full playthrough or sustained high-FPS Vulkan performance. Test saves,
game-derived code, binaries and captures remain private local outputs. The local
companion build reused matching backend-independent MSVC/O1 AOT objects; normal
source setup compiles each variant independently.
