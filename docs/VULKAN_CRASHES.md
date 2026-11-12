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
`SIMS2_GPU_DEBUG=1` forwards SDL GPU diagnostics to the runtime log; full Vulkan
validation additionally requires installed validation layers. Normal play does
not enable the expensive validation layers.

The issue remains open pending a reporter retest. If v0.4.3 still crashes, its
new `latest.log` should distinguish a fault in the runtime, SDL, a graphics
driver, or an injected module. D3D11 remains the reporter-confirmed workaround;
the release does not silently change a saved renderer preference.
