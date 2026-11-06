#!/bin/sh
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ ! -x "$base/out/sims2-linux/install/run.sh" ]; then
  echo 'Finish source setup with ./setup.sh and your supported ISO before Play.' >&2
  exit 1
fi
exec "$base/out/sims2-linux/install/run.sh" "$@"
