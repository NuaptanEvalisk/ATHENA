#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PATCH_DIR="$(cd -- "$SCRIPT_DIR/../patches" && pwd)"
KWIN_ATHENA_ROOT="${KWIN_ATHENA_ROOT:-$HOME/data/Software/KDE}"
KWIN_ATHENA_SRC="${KWIN_ATHENA_SRC:-$KWIN_ATHENA_ROOT/kwin-athena-src}"

if [ ! -d "$KWIN_ATHENA_SRC/.git" ]; then
  echo "Missing KWin source checkout: $KWIN_ATHENA_SRC" >&2
  echo "Run tools/kwin/scripts/clone-kwin.sh first." >&2
  exit 1
fi

cd "$KWIN_ATHENA_SRC"

for patch in "$PATCH_DIR"/*.patch; do
  [ -e "$patch" ] || continue
  if git apply --check "$patch"; then
    echo "Applying $(basename "$patch")"
    git apply "$patch"
  else
    echo "Skipping $(basename "$patch") because it does not apply cleanly." >&2
    echo "Check whether it is already applied or the upstream source changed." >&2
  fi
done

