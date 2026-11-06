# Display, graphics and input settings

F1 pauses the game and opens Display, Quality, Performance, Audio and Controls.
Apply affects the session; Save also writes the host settings file. Defaults
restores the draft defaults: 1920 x 1080 output and 4x native PSP rendering
(1920 x 1088 internally), preserving the original PSP aspect ratio. Existing
saved preferences remain in effect until you choose Defaults and Save. F2 toggles the FPS/frame-time overlay.

| Setting | Behavior |
| --- | --- |
| Window mode | Windowed, borderless, exclusive fullscreen. Unsupported exclusive requests fall back with a status message. |
| Output size | Desktop, presets or custom width/height; zero follows the desktop. Borderless fills the monitor. |
| Aspect | Native PSP, Auto, 4:3, 16:9, 16:10, 21:9 or 32:9. |
| Camera matching | Adapts the 3D framing and preserves proportional HUD/menu layout. Camera changes appear after resuming. |
| Internal resolution | 1x-8x fixed PSP multiples, or 50%-200% of output height with fractional targets. |
| Anisotropic filtering | Game default or 2x/4x/8x/16x, applied to filtered 3D textures; unfiltered UI is preserved. |
| Output filtering | Nearest or bilinear. |
| FPS cap | Unlimited, 30, 60, 72, 120, 144, 165, 240 presentation caps. |
| Performance | FPS, frame-time graph/range/average, output/internal sizes, VSync/cap and HDR status. |
| HDR | Off/Auto/On, paper white, peak brightness and active/fallback status. |
| Audio | Master volume and mute. |
| Controller | Deadzone, right-stick activation/inversion, button rebinding and Xbox/PlayStation/Nintendo/physical labels. |
| Keyboard/mouse | Classic/WASD presets, individual bindings, RMB camera rotation, wheel mapping, optional repeat filtering and input hints. |

## FPS and game speed

Presentation caps do not change the PSP simulation or audio clock. Gameplay runs
at its native rate (approximately 60 Hz); movies may update at 30 Hz. A 30 FPS
presentation cap reduces display updates without slowing the game. Selecting a
cap above the native rate does **not** unlock higher engine FPS. A true engine
FPS unlock requires separate game-specific timing work.

## Resolution and image detail

Output size controls the window/display area. Internal resolution improves 3D
geometry detail: 4x is 1920 x 1088 before fitting the output. Texture and HUD
artwork retain the detail present in the PSP assets. Framebuffer effects and
texture feedback retain native PSP memory compatibility.

Windows defaults to Direct3D 11; the portable build uses Vulkan through SDL3. The Xbox-specific vector
HUD and cheat toggles in the reference projects do not transfer directly to the
PSP game; they are not advertised as implemented features here.

## HDR and fullscreen hardware checks

HDR uses FP16 scRGB and converts the complete SDR image, including the menu,
to linear Rec.709 at the selected paper white, capped at the selected/display
peak. This maps existing SDR artwork into HDR output; it does not generate new
HDR highlight detail. Windows HDR must be enabled on the current display.
Exclusive fullscreen currently uses SDR; use borderless/windowed for HDR.

GPU tests cover gamma conversion, white luminance and peak clipping. SDR fallback
and hidden-window settings are tested. Physical HDR output and exclusive display
mode switching remain hardware checks for users. The menu reports fallback status.

The conversion follows [Microsoft Advanced Color guidance](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range).

## Portable Vulkan backend

The same settings UI is shared with the SDL3 Vulkan build on Linux and Windows.
On handhelds, L3 + R3 opens settings; Steam Input can also bind F1. Linux HDR is
disabled; the backend reports SDR explicitly. Wayland fullscreen requests are
subject to the compositor. See [LINUX.md](LINUX.md) for build and hardware status.

Detailed input behavior and limits: [INPUT.md](INPUT.md). Both backends now share the complete F2 graph, resolution, aspect, cap, VSync and HDR/SDR status panel.


## RecompLauncher settings

Starting with v0.2.1, the game page exposes output resolution, window mode,
internal scale, aspect/camera matching, filtering, V-Sync, presentation limits,
F2 statistics, volume/mute and common input preferences. Full rebinding and
advanced display options remain in F1. Settings apply on the next launch;
close the game before changing settings in RecompLauncher.

The launcher edits the same `[host]` INI section used by F1: LocalAppData on
Windows, XDG config on Linux. It creates defaults only for a missing file,
preserves existing bindings and does not reset preferences on each launch.
Choosing a fixed internal multiplier clears fractional scaling. Presentation
limits do not change the native engine frame rate.
