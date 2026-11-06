# High frame rate research

Status: experimental investigation, not a released engine unlock.

The F1 frame cap controls presentation pacing. Selecting 240 or unlimited does
not by itself make the PSP game simulate and draw 240 distinct frames per second.

## A working PSP precedent

[God of War PSP recomp](https://github.com/DrummingBird1/gow-psp-recomp)
implements a configurable display clock. Its
[display HLE](https://github.com/DrummingBird1/gow-psp-recomp/blob/afc345b4033e0f5a1487617832b407ac85b9ad03/runtime/rt_hle.c)
changes vblank waits, count and framebuffer pacing together. Its source explains
that both supported engines advance logic using elapsed time. This is a useful
PSP static recompilation precedent, although it uses a different runtime and
game engine. The implementation cannot establish Sims 2 timing correctness.

## Consistent display clock experiment

The earlier single-wait experiment in BENCHMARKS.md stayed at native cadence.
A subsequent private experiment changed the clock consistently: vblank timestamp
and inverse count, reported display frequency, horizontal count and vblank
duration. Three fixed-rate binaries were built from the same source revision.
Each fresh-boot run requested 900 frames, using an uncapped, hidden D3D11 surface.

The final 300 requests reached 59.94006, 120 and 240 requests per guest second,
respectively. All three decoded 429 movie frames. Each final menu request
submitted 86 draws. These are guest render-request measurements, not physical
monitor FPS or proof of correct gameplay speed. Unpaced wall time is deliberately
excluded from performance claims.

## Gameplay timing fails the naive clock change

Each case used its own copy of the same saved game. Scripted input loaded the
save at guest second 30 and held the stick upward from seconds 42 to 44. Captures
were stopped at matched guest timestamps rather than matched frame counts.

At guest second 45 the native case displayed Sunday 7:52 PM, the 120 Hz case
8:06 PM and the 240 Hz case 8:36 PM. Character/camera positions also differed.
This is direct evidence that changing display timing alone changes gameplay
progression. It is not a safe high-FPS feature.

A second matched capture at guest second 40 showed 7:47 PM, 7:56 PM and 8:16 PM.
Over the following five guest seconds, the clock therefore advanced 5, 10 and
20 game minutes. The tested 120/240 Hz variants advanced this clock at twice and
four times the native rate. This does not establish the exact multiplier for
every subsystem. Audio correctness was not tested in these muted runs.

The next implementation step is to identify and retime frame-dependent world,
movement and animation updates, while preserving audio and scheduler timing.
Alternatively, extra rendered frames could interpolate between native simulation
updates; that requires separate rendering work and is not implemented.
An unlimited guest clock would not solve either requirement.

All experimental binaries, game-derived code, raw draw traces, screenshots and
save copies remain private build outputs. Original clock sources were restored
and the native production binary rebuilt. Production timing and release assets
were not changed by this investigation. Aggregate measurements are recorded in
[data/high-fps-research.json](data/high-fps-research.json).
