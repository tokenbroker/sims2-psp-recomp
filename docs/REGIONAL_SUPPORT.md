# Regional support and European discs

[Issue #2](https://github.com/tokenbroker/sims2-psp-recomp/issues/2) requests the
European release. Runtime and installer support remains **USA ULUS-10031 only**.
No European ISO or executable is available in the current validation workspace.
A regional port cannot be verified from a disc label alone.

The runtime verifies the executable SHA-256 before calling translated functions.
The experimental timing hook and other game-specific addresses were checked
against the USA executable. Different European code needs its own generated
corpus and verification of those hooks, imports and data layout. If the exact
executable proves identical, its disc/asset differences still need testing.
Do not bypass the identity checks or rename a European disc ID to the USA one.

## Collect metadata from an owned copy

```powershell
python profiles/sims2/tools/inspect_disc.py "D:/Games/The Sims 2 Europe.iso" `
  --output out/eu-support/disc-metadata.json
```

This tool only reads the ISO and emits disc ID, executable hash/size and ELF
layout metadata. It does not extract or upload game content, include the local
ISO path in the JSON, or enable play. It does not decrypt an encrypted executable.
The metadata report can establish which European revision is being ported and
whether it shares the USA executable. Do not attach your ISO, BOOT/EBOOT, game
assets or saves to a public issue.

The inspector was checked against the owned USA copy and synthetic malformed
ELF fixtures. Those checks do not establish European compatibility. A real EU
copy remains necessary for code generation and gameplay/language validation.
