# High frame rate research

Status: opt-in experimental 120/240 Hz engine modes support USA and Europe
from v0.4.1. Each regional timing hook verifies its relocated instruction signature.
Native remains the default. Earlier experiments below document the USA work.

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

## Engine timestep correction

Tracing the minute counter led to the world-clock update at 0x089D8A34. It
multiplies the timing object's delta by the game speed. That delta is written by
0x08976EE8, called from the application update at 0x08879430. Following its input
back to 0x0885F998 exposed the render-context period getter at 0x08937F00 and the
reciprocal getter at 0x08937EF0. The period remained 0.016683334 seconds after
the virtual display was accelerated, so each additional frame advanced the
world by a full native step.

The opt-in hook updates the render-context period and reciprocal together
(0x08A2FA5C and 0x08A2FA58). The existing engine tick quantizer and application
update then receive the selected frame duration. Native mode installs no hook.
Only the hash-verified USA executable is supported. This is a game-specific
correction; no Xbox hooks or generated game code are distributed.

Matched-time runs now advance five game minutes during five guest seconds at
native, 120 and 240 Hz. The high-rate scenarios have initial clock offsets of one
or two game minutes; therefore comparisons use the progression between seconds
40 and 45, not the absolute clock at 45. A one-second rightward walk and a
separate 0.2-second rightward walk produced visually comparable end positions.
Small differences remain between captures; this is not a quantified physics or
animation equivalence claim. All three cases decoded the same 429 movie frames.
Per-channel audio sample counts stayed near 44.1 kHz in the sampled gameplay
interval. These muted tests establish scheduling, not audible quality.

Additional paced SDL dummy-device smoke runs completed at both rates, submitting
audio through the actual native stream path. They do not validate audible output.
Both incurred pacing lateness, especially 240 Hz: completing 45 guest seconds
took about 49 and 71 wall seconds respectively, including startup. The 240 run
overlapped a Linux build, so these are not isolated hardware benchmarks. Correct
guest-time progression does not guarantee real-time throughput; 240 Hz is not
advertised as sustained performance on the development machine.

F1 > Display > Engine frame rate offers Native, 120 and 240. Save and restart
is required: changing a clock origin mid-session would invalidate deadlines.
F2 reports the active rate. Presentation cap and VSync remain separate limits.
Unlimited is still a presentation option only. Higher engine rates require more
CPU/GPU work; these headless request counts do not establish visible monitor FPS.

Remaining limits include frame-count-based input repeats, untested animations,
later gameplay, audible high-rate playback and physical Linux/Deck output.
The mode is explicitly experimental and can be returned to Native from F1.
There is no frame interpolation and no unlimited engine clock.

All experimental binaries, game-derived code, raw draw traces, screenshots and
save copies remain private build outputs. Aggregate initial measurements are in
[data/high-fps-research.json](data/high-fps-research.json); corrected runs are in
[data/high-fps-retimed.json](data/high-fps-retimed.json). Existing release assets
and the user's stable installation have not been replaced.
