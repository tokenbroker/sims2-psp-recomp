#!/bin/sh
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
python3 "$base/profiles/sims2/tools/doctor.py"
if [ "$#" -eq 0 ]; then
  if command -v zenity >/dev/null 2>&1; then iso=$(zenity --file-selection --title='Select your USA The Sims 2 PSP ISO') || exit 1
  elif command -v kdialog >/dev/null 2>&1; then iso=$(kdialog --getopenfilename "$HOME" '*.iso' --title 'Select your USA The Sims 2 PSP ISO') || exit 1
  else printf 'Path to your USA The Sims 2 PSP ISO: '; IFS= read -r iso; fi
  set -- "$iso"
fi
exec python3 "$base/build.py" "$@"
