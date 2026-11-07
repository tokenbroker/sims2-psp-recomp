# Vulkan opaque presentation

The Windows playtest showed solid black patches on the ground and beneath light
cones. A same-save D3D11/Vulkan comparison produced identical native framebuffer
captures, and source-alpha shadow blending passed on both renderers. The defect
was in the GPU image sent to the window rather than in those captured pixels.

The PSP framebuffer's alpha channel contains game data used by blending and
stencil-like effects. Vulkan passed that texture directly to ImGui, whose normal
image blending treated this alpha as window transparency. Regions with low or
zero framebuffer alpha therefore darkened or disappeared against the output's
black background. D3D11's presentation shader already emits opaque alpha.

Vulkan now prepares a separate opaque GPU display copy. It retains the source
RGB and sets display alpha to one using the existing texture-replace shader.
The original GPU framebuffer and CPU-visible PSP memory retain their alpha.
The display copy is cached until another draw changes its source, and is released
alongside its render target when internal resolution changes.

## Regression coverage

- A new GPU readback test failed before the fix because framebuffer alpha leaked
  into presentation opacity. It now checks opaque alpha and unchanged RGB.
- Pixel checks cover 1x, 2x, 4x, 8x and fractional 1.25x presentation.
- A destination-alpha blend after presentation confirms that the game can still
  read its original alpha; ordinary shadow blending is also checked on D3D11.
- The SDL window test now presents a real GPU-rendered translucent scene rather
  than exercising only the CPU fallback image.
- Windows Vulkan and Linux Vulkan suites passed. The D3D11 renderer pixel checks
  also passed.

Native framebuffer screenshots alone could not reveal this output-only problem.
Future presentation checks must inspect the GPU display image as well as PSP
memory. User saves and game-derived binaries remain private. Selected, reviewed game
screenshots are published in the [playtest gallery](PLAYTEST_SCREENSHOTS.md).
