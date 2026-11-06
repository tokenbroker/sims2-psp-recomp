# Windows Vulkan upload performance

An RX 6950 XT playtest reported choppy Vulkan output. The renderer allocated,
mapped and released a separate GPU transfer buffer for every draw. In the 4x menu
scenario, draw processing alone averaged 30.92 ms per request.

The fix uses a logical 32 MiB upload arena with aligned offsets. Uploads in one
command buffer occupy separate regions; SDL cycles its backing allocation at
submission boundaries if earlier work still uses it. Texture and vertex uploads
share the arena. This follows SDL's
[transfer-buffer cycling API](https://wiki.libsdl.org/SDL3/SDL_MapGPUTransferBuffer).
Readback buffers are retained per render target, and RGBA framebuffer rows are
copied in bulk. The SDL presenter now uses the existing bulk pixel converter
instead of doing a checked guest-memory load for every pixel. PSP framebuffer
feedback and CPU-visible copies are retained.

## Measured scope

Each run used a fresh isolated memory stick, RX 6950 XT, 4x internal resolution,
1920x1080 output settings, native engine timing, VSync off and no presentation
cap. The measured interval is the final 300 of 900 requests after fresh boot.
No compilation overlapped these menu measurements.

Mean pre-presentation game/render work fell from 36.05 to 5.65 ms per request;
p95 fell from 40.71 to 6.76 ms. Draw processing fell from 30.92 to 0.98 ms.
All runs produced the same 86 draws per measured request and byte-identical final
menu captures. Captures and game-derived binaries remain private; aggregate
measurements and hashes are in [data/vulkan-upload-performance.json](data/vulkan-upload-performance.json).

These are hidden, unpaced request measurements, not monitor FPS. A hidden SDL
swapchain adds a delay to presentation; the work measurements exclude that path.
The final change also reduces presentation-copy overhead, which this work-only
figure does not measure. The captures establish output equivalence for this
scene, not all gameplay.

## Correctness checks

The Vulkan renderer checks cover framebuffer feedback, partial copies, CPU
changes, all PSP color formats and 1x/2x/4x/8x rendering. A new test sends more
than 32 MiB of changing textures through one render sequence, forcing staging
rollover and verifying pixels from every earlier draw.

A copied-save 4x/120 Hz run rendered 2,400 requests and 185,655 draws and exited
with the expected diagnostic status. The corresponding pre-fix run had exceeded
its 240-second timeout. The new gameplay run overlapped a Linux rebuild, so its
wall time is not used as a hardware benchmark. Physical-window playback and
audible quality still benefit from user testing; this is not a high-FPS guarantee.
