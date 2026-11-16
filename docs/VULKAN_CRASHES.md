# Vulkan save-to-gameplay crash investigation

[Issue #1](https://github.com/tokenbroker/sims2-psp-recomp/issues/1#issuecomment-6823138861)
reports a v0.4.1 Windows crash after selecting a save and Sim on Radeon 890M.
D3D11 works on that device and selecting Native engine timing does not help.
This narrows the report to the Vulkan path; it does not identify the faulting DLL.

## Reproduction and concrete correction

Copied private USA garage-save fixtures completed the transition on the local
RX 6950 XT, using Native engine timing, 4x scaling and a normal SDL swapchain
shown off-screen. Both the original and corrected runs reached gameplay.
Vulkan validation layers were explicitly enabled. The corrected run reported
no validation errors. This is not a reproduction on the reporter's 890M.

Earlier fully hidden SDL tests could skip swapchain rendering. The off-screen
normal-window checks exercised real presentation; temporary off-screen helpers
are not included in the release.

A separate point-list GPU regression test exposed a definite Vulkan API violation:
`VUID-VkGraphicsPipelineCreateInfo-topology-08773`. The renderer supports PSP
points, but its vertex shader did not write `gl_PointSize`. This is required for
point-list pipelines in the supported Vulkan configuration; see the
[SDL GPU topology requirements](https://wiki.libsdl.org/SDL3/SDL_GPUPrimitiveType).
A permissive driver may still rasterize a default-sized point, so a pixel test
alone did not reliably detect the violation on the local AMD device.

v0.4.3 writes a point size of 1.0 in the vertex shader. The same point pipeline
and pixel test then passed with no validation errors. Ubuntu CI now installs
Vulkan validation layers and enables `SIMS2_GPU_VALIDATION_TESTS`, which fails
on validation errors even when the driver returns success. This is a verified
Vulkan compatibility correction, not confirmation that it caused issue #1.

## If a native crash persists

Windows runtimes now record a fatal exception code, faulting module basename,
module-relative offset, and access type/address in the inherited `latest.log`.
A child-process test triggers a real access violation and verifies the report.
No memory dump, guest memory, save contents, environment, or full module path is
captured by the fatal-error handler. Existing runtime logs can contain local
save paths; review/redact those paths before posting a log publicly.

Vulkan startup also records the SDL version and GPU driver name/version/info.

## v0.4.3 retest: fault inside the AMD driver

The reporter's v0.4.3 log shows `module=amdvlk64.dll offset=0x3eee20 access=read
address=0x10` after the save loads, with no stop report and no error text. The
point-size correction did not address this crash.

### What the fault is

The reporter's driver build (`amdvlk64.dll` 9.2.10.416, from the public
Adrenalin 26.10.1 package) was inspected at that offset. The faulting
instruction reads the current position of a command buffer's scratch allocator
while preparing an image layout transition. The allocator pointer is null. The
driver sets that pointer when a command buffer begins recording and clears it
when the buffer ends or is reset. The fault therefore means a command was
recorded into a command buffer that was not recording.

This reproduces on the local RX 6950 XT (driver 2.0.353): recording a blit into
an SDL GPU command buffer that has already been submitted faults in
`amdvlk64.dll` with the same `access=read address=0x10`. The offset differs
because the driver build differs.

### How the runtime got there

`NativeRenderer::Impl::submit()` cleared its command-buffer pointer only after a
successful submission. When `SDL_SubmitGPUCommandBuffer` failed, it threw and
kept the pointer. `main.cpp` then caught the error and called
`renderer.flush()` before printing anything, which recorded a framebuffer blit
into the already-submitted buffer. The driver fault replaced the real error in
the log. No other path that reuses a submitted command buffer was found in the
renderer, the window, or SDL 3.4.20's Vulkan backend.

This explains the crash signature. It does not yet explain why the submission
failed on the 890M in the first place (for example a lost device or an
out-of-memory result). Local AMD GPUs complete the same save-to-gameplay
transition, including the integrated GPU at the 120 Hz engine rate.

### Changes

- `submit()` forgets the command buffer before reporting a failed submission.
- A runtime error is written to the log as `[runtime] ...` as soon as it is
  caught, and the final renderer flush is skipped after an error.
- `SIMS2_TEST_FAIL_SUBMIT=N` reports the Nth submission as failed. The renderer
  test uses it to check recovery; it crashed the previous renderer on AMD. The
  desktop launcher clears the variable.
- The fatal-error line also names the SDL GPU call in progress (`operation=`),
  the renderer phase (`vulkan-draw`, `vulkan-flush`, `vulkan-presentation`,
  `vulkan-output`), thread ids, and the numeric GE pipeline key of the current
  draw (`detail=`). These are labels, enumerations and flags only.

With these changes the same failure should end with a readable
`Submit Vulkan commands: <Vulkan result>` line instead of a driver fault. That
line is what identifies the underlying 890M failure.

`SIMS2_GPU_DEBUG=1` forwards SDL GPU diagnostics to the runtime log; full Vulkan
validation additionally requires installed validation layers. Normal play does
not enable the expensive validation layers.

The issue remains open pending a reporter retest with a build that contains
these changes. Its `latest.log` should show the Vulkan result behind the failed
submission. D3D11 remains the reporter-confirmed workaround;
the release does not silently change a saved renderer preference.

## v0.4.4 retest: device memory exhausted

The reporter's v0.4.4 log no longer faults in the driver. It stops with
`Submit Vulkan commands: vkQueueSubmit VK_ERROR_OUT_OF_DEVICE_MEMORY` right
after the save loads, on the 890M at the 120 Hz engine rate.

### Cause

Two renderer behaviours combined:

- Every GE block transfer ended the current command buffer, even when the
  transfer touched no render target. Gameplay issues dozens of these per frame:
  the local save-to-gameplay run made 74,569 submissions for 2,100 frames.
- Every submission restarted the 32 MB staging arena and the 16 MB vertex buffer
  at offset zero and asked SDL to cycle them. SDL allocates a new backing buffer
  whenever the previous one is still referenced by a submission in flight.

A GPU that falls behind therefore accumulates 48 MB per outstanding submission.
On the local AMD integrated GPU the same run rose from about 250 MB to 1.4 GB
dedicated plus 4.4 GB shared graphics memory when gameplay started. It did not
fail locally; the 890M ran out.

### Changes

- A block transfer that touches no render target no longer submits.
- The staging arena and vertex buffer keep filling across submissions and are
  cycled only when they wrap.

The same local run now makes 3,604 submissions and peaks at 168 MB dedicated
plus 175 MB shared, with a byte-identical final frame and guest state. A 4x
run with Vulkan validation layers reports no errors. The renderer test checks
that an unrelated transfer does not submit.

This has not been confirmed on the 890M.
