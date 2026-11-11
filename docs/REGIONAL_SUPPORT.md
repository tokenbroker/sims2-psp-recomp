# Supported regions

v0.4.0 adds the verified European executable requested in
[issue #2](https://github.com/tokenbroker/sims2-psp-recomp/issues/2).

| Disc | Languages | Engine timing |
| --- | --- | --- |
| USA ULUS-10031 | English | Native; experimental 120/240 Hz |
| Europe ULES-00211 | English, French, German, Spanish, Italian | Native only |

Exact executable SHA-256 identities are recorded in
[identity.json](../profiles/sims2/config/identity.json). Other revisions, regions
and modified executables are rejected. Both regions use independent translated
code; this is not a disc-label bypass.

## Play and change language

The prebuilt Windows release includes D3D11 and Vulkan for both regions. Import
the whole extracted folder into RecompLauncher and choose your supported ISO.
The frontend reads the actual disc identity and selects its runtime. The runtime
then independently validates the disc identity and executable SHA-256 before
executing translated functions.

For Europe, use **F1 > Display > Game language**, Save and restart. The launcher
page also exposes separate **Europe:** controls. USA retains `settings.ini`;
Europe uses `settings-europe.ini`. Existing USA input/audio/graphics preferences
are not overwritten. Each game uses its own PSP save identity: `ULUS10031` or
`ULES00211`. Cross-region save interchange is not established.

Europe currently uses Native engine timing. Its settings cannot activate the
USA-specific high-FPS hook at unrelated addresses. Display resolution/scaling,
aspect/camera choices, controls, audio and renderer switching remain available.

A prepared BOOT copy from the other supported region is tolerated only after
validating the selected ISO: the runtime loads that ISO's executable instead.
An unknown/mismatching prepared executable is still rejected. This permits
region switching without relying on the launcher's cached preparation flag.

## Source builds and inspection

The ISO-to-build scripts select the region from the verified identity and build
that region's corpus. Use a separate workspace for each region to retain both
build caches. Manual European CMake builds require `-DSIMS2_REGION=EUROPE` and
the matching generated directory/archive; never reuse a USA AOT archive for EU.
Linux remains a source build; the Windows game checks do not establish a Linux
EU playthrough or physical Steam Deck compatibility.

For metadata-only inspection of an owned disc:

```powershell
python profiles/sims2/tools/inspect_disc.py "D:/Games/The Sims 2 Europe.iso" `
  --output out/eu-support/disc-metadata.json
```

This reads the ISO and emits only compatibility metadata, not game files or the
local ISO path. Never publish disc images, original executables or saves.

## Validation scope

- The owned European disc was identified as ULES-00211 and generated 494 units.
- All five languages reached Create-A-Sim; screenshots were checked for the
  corresponding localized labels, including accented text.
- D3D11 and Vulkan booted the European game and rendered localized menus.
- A French Create-A-Sim flow created an encrypted regional save. Both renderers
  decrypted/reloaded copies and ran the opening story sequence with paced audio.
- Language persistence, Native-only EU timing and separate settings paths were
  checked, including a Linux settings test. Regional routing was checked against
  synthetic malformed ISOs and both owned discs.

These are bounded regional smoke checks, not a complete European playthrough.
The handheld/audio issue remains separate and is not claimed fixed here.
