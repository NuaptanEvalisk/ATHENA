#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ "$#" -gt 0 ]; then
  build_arg=$1
  shift
else
  build_arg=build_qt6
fi
case "$build_arg" in
  /*) build_dir=$build_arg ;;
  *)  build_dir=$repo_root/$build_arg ;;
esac

runtime_dir=$repo_root/ATHENA
mkdir -p "$runtime_dir/bin" "$runtime_dir/lib"

tmp=
cleanup () {
  if [ -n "${tmp:-}" ]; then rm -f -- "$tmp"; fi
}
trap cleanup EXIT HUP INT TERM

atomic_install () {
  src=$1
  dst=$2
  mode=$3

  if [ -f "$dst" ] && cmp -s -- "$src" "$dst"; then
    printf 'unchanged %s\n' "$dst"
    return
  fi

  tmp=$(mktemp "$(dirname -- "$dst")/.$(basename -- "$dst").tmp.XXXXXX")
  install -m "$mode" -- "$src" "$tmp"
  mv -f -- "$tmp" "$dst"
  tmp=
  printf 'deployed  %s\n' "$dst"
}

atomic_symlink () {
  target=$1
  dst=$2

  if [ -L "$dst" ] && [ "$(readlink -- "$dst")" = "$target" ]; then
    printf 'unchanged %s\n' "$dst"
    return
  fi

  tmp=$(mktemp "$(dirname -- "$dst")/.$(basename -- "$dst").tmp.XXXXXX")
  rm -f -- "$tmp"
  ln -s -- "$target" "$tmp"
  mv -f -- "$tmp" "$dst"
  tmp=
  printf 'deployed  %s\n' "$dst"
}

deploy_library () {
  src=$1
  [ -e "$src" ] || [ -L "$src" ] || return
  dst=$runtime_dir/lib/$(basename -- "$src")
  if [ -L "$src" ]; then
    atomic_symlink "$(readlink -- "$src")" "$dst"
  elif [ -f "$src" ]; then
    atomic_install "$src" "$dst" 755
  fi
}

atomic_install "$build_dir/src/ATHENA.bin" "$runtime_dir/bin/ATHENA.bin" 755

if [ -f "$build_dir/src/ATHENA-Watchdog" ]; then
  atomic_install "$build_dir/src/ATHENA-Watchdog" \
    "$runtime_dir/bin/ATHENA-Watchdog" 755
fi

if [ -f "$build_dir/src/athena-codex-bridge" ]; then
  atomic_install "$build_dir/src/athena-codex-bridge" \
    "$runtime_dir/bin/athena-codex-bridge" 755
fi

for src in "$build_dir"/x64/lib/libqt6advanceddocking*.so.*; do
  deploy_library "$src"
done

for src in "$build_dir"/x64/lib/libqt6advanceddocking*.so; do
  deploy_library "$src"
done

for src in "$@"; do
  deploy_library "$src"
done

trap - EXIT HUP INT TERM
