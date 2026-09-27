#!/bin/sh
set -eu

# Build the small ATHENA-private runtime layout needed by Intel's NPU UMD on
# openSUSE Tumbleweed releases where libze_intel_npu1 is packaged without the
# matching compiler-in-driver shared libraries. Nothing is installed system-wide.

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
target=${1:-"$repo_root/ATHENA/lib/openvino-npu"}
work=${ATHENA_NPU_SUPPORT_WORKDIR:-"$repo_root/build_qt6/athena-npu-upstream/support-setup"}

driver_version=1.38.0
archive=linux-npu-driver-v1.38.0.20260910-34487311128-ubuntu2404.tar.gz
archive_sha256=1efcd4b60c22abee751d8f2705962cbcc2a569de45c7e0e670cf08afbfcdc1d2
archive_url="https://github.com/intel/linux-npu-driver/releases/download/v1.38.0/$archive"

for tool in curl sha256sum tar ar rpm; do
  command -v "$tool" >/dev/null 2>&1 || {
    printf 'missing required tool: %s\n' "$tool" >&2
    exit 1
  }
done

installed=$(rpm -q --qf '%{VERSION}' libze_intel_npu1 2>/dev/null || true)
if [ "$installed" != "$driver_version" ]; then
  printf 'expected libze_intel_npu1 %s, found %s\n' \
    "$driver_version" "${installed:-not installed}" >&2
  exit 1
fi

umd=/usr/lib64/libze_intel_npu.so.$driver_version
if [ ! -f "$umd" ]; then
  printf 'missing Intel NPU UMD: %s\n' "$umd" >&2
  exit 1
fi
if [ ! -e /usr/lib64/libtbb.so.12 ]; then
  printf 'missing libtbb.so.12; install the Tumbleweed libtbb12 package\n' >&2
  exit 1
fi

mkdir -p "$work"
download=$work/$archive
if [ ! -f "$download" ]; then
  curl -fL "$archive_url" -o "$download"
fi
printf '%s  %s\n' "$archive_sha256" "$download" | sha256sum -c -

tmp=$(mktemp -d "$work/extract.XXXXXX")
staging=$(mktemp -d "$(dirname -- "$target")/.openvino-npu.XXXXXX")
backup=
cleanup () {
  rm -rf -- "$tmp" "$staging"
  if [ -n "${backup:-}" ] && [ -d "$backup" ] && [ ! -d "$target" ]; then
    mv -- "$backup" "$target"
  fi
}
trap cleanup EXIT HUP INT TERM

tar -xzf "$download" -C "$tmp" --wildcards './intel-driver-compiler-npu_*.deb'
deb=$(find "$tmp" -type f -name 'intel-driver-compiler-npu_*.deb' -print | head -n 1)
if [ -z "$deb" ]; then
  printf 'Intel release archive did not contain the NPU compiler package\n' >&2
  exit 1
fi

mkdir -p "$tmp/root"
ar p "$deb" data.tar.gz | tar -xzf - -C "$tmp/root"
compiler_dir=$tmp/root/usr/lib/x86_64-linux-gnu
for library in libopenvino_intel_npu_compiler_loader.so \
               libopenvino_intel_npu_compiler.so; do
  if [ ! -f "$compiler_dir/$library" ]; then
    printf 'compiler package is missing %s\n' "$library" >&2
    exit 1
  fi
done

install -m 755 -- "$umd" "$staging/libze_intel_npu.so.$driver_version"
ln -s "libze_intel_npu.so.$driver_version" "$staging/libze_intel_npu.so.1"
ln -s libze_intel_npu.so.1 "$staging/libze_intel_npu.so"
install -m 755 -- "$compiler_dir/libopenvino_intel_npu_compiler_loader.so" "$staging/"
install -m 755 -- "$compiler_dir/libopenvino_intel_npu_compiler.so" "$staging/"

mkdir -p "$(dirname -- "$target")"
if [ -e "$target" ] || [ -L "$target" ]; then
  backup=$target.old.$$
  mv -- "$target" "$backup"
fi
mv -- "$staging" "$target"
staging=
if [ -n "${backup:-}" ]; then
  rm -rf -- "$backup"
  backup=
fi

printf 'Prepared private Intel NPU support bundle: %s\n' "$target"
printf '  UMD:      libze_intel_npu.so.%s\n' "$driver_version"
printf '  Compiler: Intel NPU compiler-in-driver 1.38 release\n'

trap - EXIT HUP INT TERM
rm -rf -- "$tmp"
