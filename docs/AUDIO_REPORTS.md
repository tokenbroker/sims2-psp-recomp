# Audio stutter reports

Cutscene sound breaks and handheld audio stutter remain open compatibility
issues. The video supplied in the report is
[The Sims 2 PSP recomp is here with the Recomp Launcher!](https://www.youtube.com/watch?v=6GDmFz8NOLw)
by Jessy Suryanegara; the user points to commentary at video time 5:12 and
15:08. Inspection of the supplied local video confirms that approximately
5:10-5:15 shows the opening desert-driving story sequence, after Create-A-Sim.
At 5:15, the subtitles report sound breaks while FPS appears steady. This is a
reproduction target for the opening in-game story sequence, rather than assuming
the report concerns the earlier logo/movie playback. The video timestamp is not
a game-time marker.

Later footage at 8:00 shows output-relative rendering enabled at 200%, a displayed
3812 x 2160 render target and 16x anisotropic filtering. At 14:00 the reviewer
selects fixed 4x (1920 x 1088) rendering. Those later settings do not establish
which settings were active at the first reported break. The exact installed
release, initial renderer and engine rate have not been confirmed. The video
establishes the reported scene and commentary; it does not establish a runtime
root cause or demonstrate that current releases fix it.

A separate [ROG Ally frame-drop/audio report](https://github.com/tokenbroker/sims2-psp-recomp/issues/1)
is also open. Its symptoms do not establish that it has the same cause.

## Investigation

A local paced, 4x D3D11 opening-movie run with SDL's dummy audio device recorded
empty queues on an active guest channel. An experiment submitting PCM before
waiting on guest-clock pacing did not demonstrate a clear improvement; the
existing playback order is retained. Both runs reached the same 900-frame
checkpoint, draw count and audio submission counts. This does not reproduce or
fix the reported audible event on the reviewer's device.

v0.3.2 and later source builds log per-channel `submissions` and `empty_queue_submissions`
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
to the runtime log on shutdown. They are included in v0.3.2; the v0.3.1 installer predates them. Review logs for personal paths
before sharing; do not upload ISOs, prepared game files or saves.

The existing SDL audio test checks device/stream creation, all nine channels,
PCM conversion and queue backpressure. It does not establish stutter-free
cutscene playback across physical audio devices.

v0.3.2 also removes per-sample memory resolution from PCM block reads and adds
**F1 > Quality > Handheld / low load**. These changes reduce avoidable host work
and provide a lower-load configuration; they do not establish that the reported
cutscene break or ROG Ally issue is fixed. European support remains separate.


## Opening-story scheduling correction (v0.4.2)

The supplied video identifies the in-game desert-driving sequence as the
reproduction target. A copied private European new-character save reproduced
repeated empty-queue submissions in that sequence using Native engine timing,
the headless D3D11 renderer, guest-clock pacing and SDL dummy audio. Early
headless runs did not apply F1 render-scale settings; they are not evidence of a
1x-versus-4x rendering comparison. The normal window path is checked separately
with a hidden window. Matched headless runs stopped at
the same 3100-frame checkpoint, decoded 429 movie frames, submitted 5,611,776
unchanged guest audio frames, and rendered the same story scene.

The runtime now submits an already-ready PCM block before waiting for its guest
clock deadline. The previous order could leave the device consuming its last
block while the next one was held back by a host sleep. The per-channel loading
backpressure budget increases from 100 to 200 ms so short queued bursts do not
unnecessarily stall all guest threads. Backpressure still preserves every sample
and retains its stopped-device timeout. No recovery silence, sample dropping,
resampling-speed override or guest-timing change is introduced.

A temporary experiment padding every drained stream reduced queue-empty counts
but inserted repeated silence; it was rejected. A startup-only padding experiment
was also removed. Neither padding implementation is in the release.

The selected correction improved queue continuity in the controlled headless run, but
empty submissions remained. Empty queues are not an audible-underrun counter,
and dummy-device tests cannot establish that the reviewer's sound breaks are
fixed. Actual-device/reviewer retesting remains required; the report stays open.
Private traces, screenshots, video and save fixtures are excluded from publication.


The matching hidden-window 4x runs exercised 3097 output presentations and 2677
scaled presentations each. Channel 6 empty submissions changed from 614 to 393;
channel 4 changed from 165 to 183, and channel 5 stayed at 2. Backpressure waits
changed from 972 to 241. The mixed channel result is a reason to describe this as
a scheduling mitigation, not proof of an audible fix. Windows native audio tests
and all 49 Linux host tests passed with the final source change.
