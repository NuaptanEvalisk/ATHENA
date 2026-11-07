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
list_tmp=
math_stage=
math_previous=
cleanup () {
  if [ -n "${tmp:-}" ]; then rm -f -- "$tmp"; fi
  if [ -n "${list_tmp:-}" ]; then rm -f -- "$list_tmp"; fi
  if [ -n "${math_stage:-}" ]; then rm -rf -- "$math_stage"; fi
  if [ -n "${math_previous:-}" ]; then rm -rf -- "$math_previous"; fi
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

# Build (if out of date) and atomically replace the release math icon set.
# These are shipped immutable assets, never files in ~/.ATHENA/system/cache.
cmake --build "$build_dir" --target athena_math_icons
math_source=$build_dir/generated/math-icons-v3
math_parent=$runtime_dir/misc/images
math_dest=$math_parent/math-icons-v3
test -s "$math_source/manifest.json"
mkdir -p "$math_parent"
math_stage=$(mktemp -d "$math_parent/.math-icons-v3.new.XXXXXX")
cp -a -- "$math_source/." "$math_stage/"
if [ -d "$math_dest" ]; then
  math_previous=$(mktemp -d "$math_parent/.math-icons-v3.old.XXXXXX")
  rmdir -- "$math_previous"
  mv -- "$math_dest" "$math_previous"
fi
mv -- "$math_stage" "$math_dest"
math_stage=
if [ -n "$math_previous" ]; then
  rm -rf -- "$math_previous"
  math_previous=
fi
printf 'deployed  %s\n' "$math_dest"

if [ "$(uname -s)" = Linux ]; then
  python3 "$repo_root/tools/install-local-executable.py" \
    "$build_dir/src/ATHENA.bin" "$runtime_dir/bin/ATHENA.bin"
else
  atomic_install "$build_dir/src/ATHENA.bin" "$runtime_dir/bin/ATHENA.bin" 755
fi

if [ -f "$build_dir/src/ATHENA-Watchdog" ]; then
  atomic_install "$build_dir/src/ATHENA-Watchdog" \
    "$runtime_dir/bin/ATHENA-Watchdog" 755
fi

if [ -f "$build_dir/src/athena-codex-bridge" ]; then
  atomic_install "$build_dir/src/athena-codex-bridge" \
    "$runtime_dir/bin/athena-codex-bridge" 755
fi

if [ -f "$build_dir/src/athena-rag-npu-worker" ]; then
  atomic_install "$build_dir/src/athena-rag-npu-worker" \
    "$runtime_dir/bin/athena-rag-npu-worker" 755
fi

# The Vault shaping cache is now built by ATHENA's ADS Cache manager. Retire
# the old standalone executable on local upgrades.
rm -f "$runtime_dir/bin/athena-shaping-cache-builder"

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
