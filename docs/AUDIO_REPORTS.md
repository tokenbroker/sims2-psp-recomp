# Audio stutter reports

Cutscene sound breaks and handheld audio stutter remain open compatibility
issues. The video supplied in the report is
[The Sims 2 PSP recomp is here with the Recomp Launcher!](https://www.youtube.com/watch?v=6GDmFz8NOLw)
by Jessy Suryanegara; the user points to commentary at video time 5:12 and
15:08. The exact audio segment, active settings and release version have not
been independently verified. The video timestamp is not a game-time marker.

A separate [ROG Ally frame-drop/audio report](https://github.com/tokenbroker/sims2-psp-recomp/issues/1)
is also open. Its symptoms do not establish that it has the same cause.

## Investigation

A local paced, 4x D3D11 opening-movie run with SDL's dummy audio device recorded
empty queues on an active guest channel. An experiment submitting PCM before
waiting on guest-clock pacing did not demonstrate a clear improvement; the
existing playback order is retained. Both runs reached the same 900-frame
checkpoint, draw count and audio submission counts. This does not reproduce or
fix the reported audible event on the reviewer's device.

Source builds now log per-channel `submissions` and `empty_queue_submissions`
when `SIMS2_PROFILE=1` is enabled. These query both input PCM and converted output.
The first submission is excluded. An empty queue may be an intentional gap
between effects, resampler buffering or playback starvation. It is not an
underrun counter or proof of an audible dropout; device-side queued sound may
also still be playing. No samples are discarded or synthesized by this change.
The diagnostic queries/logging are disabled in normal play.

## Useful report details

Please include the build/version, renderer, engine rate, internal render scale,
VSync/presentation cap, device/GPU, audio output type and the exact scene/action.
A timestamped video or recording helps distinguish repeated pops, missing movie
samples, continuous slow playback and device changes. Note whether the issue
also happens at Native engine timing and 1x/2x internal rendering: this separates
some sustained rendering-load problems from decoder/device problems. Avoid
changing many settings at once when comparing.

For a source-build diagnostic run, set `SIMS2_PROFILE=1` before launching and
close the game normally after the affected scene. The profile lines are written
to the runtime log on shutdown. They become available in builds containing this
source change; the v0.3.1 installer predates them. Review logs for personal paths
before sharing; do not upload ISOs, prepared game files or saves.

The existing SDL audio test checks device/stream creation, all nine channels,
PCM conversion and queue backpressure. It does not establish stutter-free
cutscene playback across physical audio devices.
