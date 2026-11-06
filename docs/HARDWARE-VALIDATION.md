# Hardware and compatibility validation

A passed software-renderer check is not physical Steam Deck verification. Use
this focused matrix when hardware becomes available; a full story playthrough
is not a gate for every host change.

| Environment | Evidence / next check |
| --- | --- |
| Windows / D3D11 | User gameplay/audio feedback, automated WARP/pixel/settings tests; benchmark the selected hardware adapter. |
| Windows / Vulkan | Native renderer pixel tests; full GPU gameplay session still needed. |
| Ubuntu 24.04 / Vulkan | Native 900-frame startup, FreeType/audio/gamepad tests, isolated saves; Mesa llvmpipe in WSL. |
| Wayland | Headless Weston: F1 pause/resume, F2, save cancellation and swapchain presentation. |
| X11 | Xvfb: same shell checks. |
| AMD RADV / NVIDIA / Intel Linux | Run the focused scenario on each available real GPU. |
| Steam Deck / Gamescope | Not physically verified. Check controller-only launch, L3+R3/F1, Steam Input, 1280x800, reconnect and suspend/resume. |
| Fedora / Arch | Build instructions provided; fresh distro/container and physical runtime checks still needed. |
| HDR | D3D11 conversion/fallback tested; physical output needs a capable screen. Vulkan output remains SDR. |

Focused session: boot from a fresh temporary save, navigate and cancel menus,
create/load a Sim, walk and rotate the camera, buy/place one object, save, exit,
relaunch and verify that change. Repeat input/navigation at 30 and 60 presentation
caps and with VSync on/off. Test held keys across focus loss and a controller
unplug/replug. A later quest failure should get its own reproducible regression.

For Deck, test Desktop Mode and Gaming Mode separately. Suspend for a minute,
resume, confirm audio, controls, frame pacing and save access; do not infer support
from a desktop Linux window. Never conduct destructive/save-compatibility tests
on the user's only save. Use an explicitly isolated memory stick.

Report GPU/driver, OS, backend, build commit, output/internal resolution, cap,
VSync, input layout, steps and observed behavior. Review logs before sharing;
exclude personal paths, machine names, credentials, disc files and saves.

The public import inventory in data/import-coverage.json compares 223 imports
with registrations observed in the native host. 171 have registered handlers;
52 do not. Most missing imports are network/ad-hoc functions; additional file,
module, utility and graphics APIs remain. This is a registration inventory, not
a completeness score. Registered handlers may reject unsupported modes. The
renderer also explicitly rejects framebuffer-alpha stencil tests, partial-bit
write masks and unsupported blend equations. Multiplayer and later story paths
must not be advertised as complete based on startup checks.
