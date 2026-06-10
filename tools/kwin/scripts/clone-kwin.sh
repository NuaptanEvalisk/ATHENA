#!/usr/bin/env bash
set -euo pipefail

KWIN_ATHENA_ROOT="${KWIN_ATHENA_ROOT:-$HOME/data/Software/KDE}"
KWIN_ATHENA_SRC="${KWIN_ATHENA_SRC:-$KWIN_ATHENA_ROOT/kwin-athena-src}"
KWIN_ATHENA_REPO="${KWIN_ATHENA_REPO:-https://invent.kde.org/plasma/kwin.git}"
KWIN_ATHENA_TAG="${KWIN_ATHENA_TAG:-v6.6.5}"

mkdir -p "$KWIN_ATHENA_ROOT"

if [ -d "$KWIN_ATHENA_SRC/.git" ]; then
  echo "KWin source already exists: $KWIN_ATHENA_SRC"
  echo "Expected tag: $KWIN_ATHENA_TAG"
  exit 0
fi

git clone --branch "$KWIN_ATHENA_TAG" --depth 1 "$KWIN_ATHENA_REPO" "$KWIN_ATHENA_SRC"

