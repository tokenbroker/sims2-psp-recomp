# Contributing

Open an issue with a reproducible problem or a focused proposal before starting
a large change. Keep fixes scoped to the affected runtime, renderer or tooling
path, and explain the observed behavior and verification in pull requests.

## Local checks

- Run the synthetic framework tests described in docs/BUILD.md.
- For host/runtime changes, run the relevant profile tests and a focused local
  scenario using your own supported disc. A full playthrough is not required.
- Run `python tools/check_public_tree.py` against staged/tracked files before pushing.
- Keep upstream and third-party license/credit notices intact.

## Public content

Commit source, configuration, documentation and synthetic tests only. Never
commit an ISO/CSO, BOOT.BIN/EBOOT.BIN, generated game code, firmware, saves,
local logs, captures, account tokens or machine-specific configuration. All
compilation/game inputs belong under ignored local output directories.

Use a GitHub noreply email for commits if you want to keep your personal address
private. Redact home paths and credentials when sharing logs in an issue.

## Bug reports

Include the commit, OS/GPU, input device, F1 settings, reproduction steps and
expected/actual behavior. Do not attach game data or a complete memory stick.
Windows HDR and exclusive-mode problems should identify the display and whether
HDR is enabled in Windows.
