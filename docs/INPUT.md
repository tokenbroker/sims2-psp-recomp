# Keyboard, mouse and controller input

F1 > Controls contains Classic and WASD keyboard presets, individual keyboard
bindings, controller button bindings, mouse settings and optional input hints.
Apply changes the session; Save persists them. Keyboard presets leave controller
bindings intact. Defaults resets the complete host configuration.

The Classic preset preserves the existing X/Z/A/S face buttons and I/J/K/L stick.
The WASD preset uses WASD movement, Space to interact, Escape to cancel, F/G for
Square/Triangle, Q/E for camera shoulders and Enter/Tab for Start/Select. Arrow
keys navigate. Select a binding button and press a supported key; Delete unbinds
and Escape cancels capture. F1/F2 remain host shortcuts. Duplicate keys may drive
multiple PSP actions, so choose bindings deliberately.

Hold the right mouse button and drag horizontally to rotate the camera. This
emulates the PSP L/R buttons. Sensitivity changes drag distance; pulse length
changes how long the button is held. Inversion applies to the mouse and optional
right-stick camera. Right-stick activation controls the threshold; the PSP camera
still receives digital shoulders, not a new analog axis.

The wheel emits short D-pad up/down pulses by default. Both directions can be
rebound to another PSP button. Wheel navigation operates through the game's own
selection logic; the native game menus do not yet support direct cursor picking.
There is no invented mouse zoom binding where a PSP zoom action is unverified.

Optional held D-pad repeat filtering has an initial 350 ms delay and configurable
80-500 ms subsequent intervals. It is **off by default** because D-pad holds can
also be gameplay actions. Analog movement is not filtered.

Input hints show the configured keyboard/controller interact and cancel buttons,
movement bindings and camera controls. Controller labels can use Xbox/Steam Deck,
PlayStation, Nintendo or physical-position names. This host overlay does not replace the
PSP artwork or original button glyphs inside game menus.

On Windows Direct3D and SDL/Vulkan, L3+R3 opens/closes settings. Both sticks must
be pressed on the same controller; release before pressing again. Steam Input may bind F1 too. The active
window owns SDL events; gamepad polling preserves them. Focus loss and settings /
save dialogs neutralize game input and require held controls to be released
before re-arming. Mouse-wheel input from those dialogs does not leak into gameplay.

Automated coverage uses synthetic key/mouse snapshots and SDL virtual gamepads:
custom mappings, deadzone, inversion, mouse pulse expiry, wheel expiry, repeat
intervals, focus gating, settings roundtrip, hot-plug/disconnect and event retention.
Physical device and Steam Input layout checks remain part of the hardware matrix.

The settings shortcut is focus-gated and is suppressed during the save selector.
Holding it does not repeat, and returning focus while holding it does not open
the panel. The shortcut is independent of gameplay button rebinding.
