#!/bin/sh
# Build the node allocator, optionally using an already downloaded tarball.
set -eu
commit=1ed67b5156a763441d2ec3c20221a7b3cd866129 # v3.7.0
sha256=a207a0802234ff9d21e7511eee52375e6299fa2ac7b25eb4ce26b579a7735c8c
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
archive=${1:-"$work/source.tar.gz"}
if [ "$#" -eq 0 ]; then
  curl -fsSL --retry 3 "https://codeload.github.com/microsoft/mimalloc/tar.gz/$commit" -o "$archive"
fi
printf '%s  %s\n' "$sha256" "$archive" | sha256sum -c -
mkdir "$work/src"
tar xf "$archive" -C "$work/src" --strip-components=1
cmake -S "$work/src" -B "$work/build" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/opt/mimalloc -DCMAKE_INSTALL_LIBDIR=lib \
  -DMI_INSTALL_TOPLEVEL=ON -DMI_OPT_ARCH=OFF -DMI_BUILD_STATIC=OFF \
  -DMI_BUILD_OBJECT=OFF -DMI_BUILD_TESTS=OFF
cmake --build "$work/build" -j16
cmake --install "$work/build"
install -Dm644 "$work/src/LICENSE" /opt/mimalloc/share/doc/mimalloc/LICENSE
