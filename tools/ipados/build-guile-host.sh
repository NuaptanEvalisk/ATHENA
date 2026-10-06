#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

ssh_target="${ATHENA_IPADOS_SSH_TARGET:-felix@127.0.0.1}"
ssh_port="${ATHENA_IPADOS_SSH_PORT:-2222}"
remote_developer_root="${ATHENA_IPADOS_REMOTE_DEVELOPER_ROOT:-/Users/felix/Developer}"
remote_source="${ATHENA_IPADOS_REMOTE_SOURCE:-${remote_developer_root}/ATHENA}"
jobs="${ATHENA_IPADOS_JOBS:-20}"

"${script_dir}/prepare-guile-bootstrap.sh"
bootstrap_local="${repo_root}/build_qt6/ipados-guile-bootstrap/x86_64-unknown-linux-gnu"
bootstrap_remote="${remote_developer_root}/athena-deps/host/generated/guile-bootstrap/x86_64-unknown-linux-gnu"

"${script_dir}/bootstrap-vm.sh" --quiet
"${script_dir}/sync-source-to-vm.sh"

ssh -o BatchMode=yes -o ConnectTimeout=10 -p "${ssh_port}" \
  "${ssh_target}" "mkdir -p '${bootstrap_remote}'"
rsync -a --delete -e "ssh -o BatchMode=yes -o ConnectTimeout=10 -p ${ssh_port}" \
  "${bootstrap_local}/" "${ssh_target}:${bootstrap_remote}/"

ssh -o BatchMode=yes -o ConnectTimeout=10 -p "${ssh_port}" \
  "${ssh_target}" sh -s -- \
  "${remote_developer_root}" "${remote_source}" "${jobs}" \
  "${bootstrap_remote}" <<'REMOTE'
set -eu

developer_root=$1
source_root=$2
jobs=$3
bootstrap_prebuilt=$4

export PATH="/Users/felix/.cargo/bin:/opt/local/bin:/opt/local/sbin:$PATH"
export LC_ALL=en_US.UTF-8
export LANG=en_US.UTF-8
export M4=gm4

host_root="$developer_root/athena-deps/host"
host_prefix="$host_root/runtime"
gc_build="$host_root/build/athena-bdwgc-8.2.12"
guile_source="$host_root/generated/athena-guile-3.0.10"
guile_build="$host_root/build/athena-guile-3.0.10"
artifacts="$developer_root/athena-artifacts"

banner () {
  printf '\n\n======================================================================\n'
  printf '%s\n' "$*"
  printf '======================================================================\n'
}

for tool in cmake ninja rsync autoconf automake autoreconf glibtoolize gm4 \
            pkg-config gmake clang clang++ python3; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "Missing required host tool: $tool" >&2
    exit 1
  }
done

mkdir -p "$host_prefix" "$host_root/build" "$host_root/generated" "$artifacts"

##############################################################################
# Native private BDW-GC for the Guile build compiler.
##############################################################################

banner "Configuring native private ATHENA BDW-GC for the Guile host compiler"
cmake -S "$source_root/3rdparty/athena-bdwgc" -B "$gc_build" -G Ninja \
  -DCMAKE_INSTALL_PREFIX="$host_prefix" \
  -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_FLAGS='-O2 -g -fPIC -fno-omit-frame-pointer -DGC_USE_ENTIRE_HEAP' \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DBUILD_SHARED_LIBS=OFF \
  -Dbuild_cord=OFF \
  -Dbuild_tests=OFF \
  -Denable_docs=OFF \
  -Denable_threads=ON \
  -Denable_parallel_mark=ON \
  -Denable_thread_local_alloc=ON \
  -Denable_threads_discovery=ON \
  -Denable_cplusplus=OFF \
  -Denable_throw_bad_alloc_library=OFF \
  -Denable_gcj_support=OFF \
  -Denable_large_config=ON \
  -Denable_mmap=ON \
  -Denable_munmap=OFF \
  -Denable_dynamic_loading=ON \
  -Denable_handle_fork=ON \
  -Ddisable_handle_fork=OFF \
  -Dinstall_headers=ON

banner "Building native private ATHENA BDW-GC with ${jobs} jobs (verbose)"
cmake --build "$gc_build" --parallel "$jobs" --verbose
banner "Installing native private ATHENA BDW-GC (verbose)"
cmake --install "$gc_build" --verbose

test -f "$host_prefix/lib/libgc.a" || {
  echo "Native private BDW-GC archive was not produced" >&2
  exit 1
}

##############################################################################
# Writable generated-source copy.  The canonical VM mirror remains read-only.
##############################################################################

banner "Refreshing generated Guile build-source copy from the read-only Linux mirror"
mkdir -p "$guile_source"
chmod -R u+w "$guile_source" 2>/dev/null || true
rsync -a --delete \
  "$source_root/3rdparty/athena-guile/" \
  "$guile_source/"
chmod -R u+w "$guile_source"

# Restore Guile's official 64-bit little-endian bootstrap mechanism with
# freshly generated objects from the same ATHENA private Guile source line.
# These are build artifacts, not copied Linux runtime caches.  The selector
# prebuilt/64-bit-little-endian is an upstream symlink to this directory.
mkdir -p "$guile_source/prebuilt/x86_64-unknown-linux-gnu/ice-9"
rm -rf "$guile_source/prebuilt/x86_64-unknown-linux-gnu/ice-9"
mkdir -p "$guile_source/prebuilt/x86_64-unknown-linux-gnu/ice-9"
rsync -a --delete \
  "$bootstrap_prebuilt/ice-9/" \
  "$guile_source/prebuilt/x86_64-unknown-linux-gnu/ice-9/"
cp "$bootstrap_prebuilt/ATHENA-BOOTSTRAP-MANIFEST.txt" \
  "$guile_source/prebuilt/x86_64-unknown-linux-gnu/.athena-bootstrap-manifest"

banner "Generating Guile autotools files in the build-source copy"
(
  cd "$guile_source"
  sh -x ./autogen.sh
)

test -x "$guile_source/configure" || {
  echo "Guile autoreconf did not produce configure" >&2
  exit 1
}

##############################################################################
# Same-version native Guile used only as GUILE_FOR_BUILD.
##############################################################################

export PKG_CONFIG_PATH="$host_prefix/lib/pkgconfig:/opt/local/lib/pkgconfig"
export CPPFLAGS="-I$host_prefix/include -I/opt/local/include"
export LDFLAGS="-L$host_prefix/lib -L/opt/local/lib"
export CC=/usr/bin/clang
export CFLAGS='-O2 -g -std=gnu17 -fno-omit-frame-pointer'

guile_config_signature='cc=/usr/bin/clang|cstd=gnu17|nojit|nolto|shared'
if [ -f "$guile_build/Makefile" ]; then
  old_signature=$(cat "$guile_build/.athena-host-config" 2>/dev/null || true)
  if [ "$old_signature" != "$guile_config_signature" ]; then
    banner "Resetting Guile host build after compiler/configuration change"
    rm -rf "$guile_build"
  fi
fi
mkdir -p "$guile_build"

banner "Configuring native patched Guile 3.0.10 build compiler"
(
  cd "$guile_build"
  "$guile_source/configure" \
    --prefix="$host_prefix" \
    --enable-mini-gmp \
    --disable-jit \
    --disable-nls \
    --disable-lto \
    --with-threads=pthreads \
    --with-bdw-gc=bdw-gc \
    --with-libunistring-prefix=/opt/local
)
printf '%s\n' "$guile_config_signature" >"$guile_build/.athena-host-config"

banner "Building native patched Guile 3.0.10 with ${jobs} jobs (verbose)"
gmake -C "$guile_build" -j"$jobs" V=1

banner "Installing native patched Guile 3.0.10 (verbose)"
gmake -C "$guile_build" install V=1

guile="$host_prefix/bin/guile"
test -x "$guile" || {
  echo "Native GUILE_FOR_BUILD was not installed: $guile" >&2
  exit 1
}

banner "Validating native GUILE_FOR_BUILD"
"$guile" --version | sed -n '1,3p'
version=$(
  "$guile" -c \
    '(format #t "~a.~a.~a" (major-version) (minor-version) (micro-version))'
)
printf 'Guile compiler version: %s\n' "$version"
[ "$version" = 3.0.10 ] || {
  echo "GUILE_FOR_BUILD must be exactly 3.0.10" >&2
  exit 1
}

"$guile" -c '
  (use-modules (system base compile) (system base target) (system vm elf))
  (let* ((triplet "aarch64-apple-darwin")
         (bv (with-target triplet
               (lambda ()
                 (compile (quote (lambda (x) x))
                          #:warning-level 0
                          #:to (quote bytecode)))))
         (elf (parse-elf bv)))
    (format #t "cross-bytecode target=~a word-size=~a byte-order=~a~%"
            triplet (elf-word-size elf) (elf-byte-order elf)))'

printf '%s\n' "$guile" >"$artifacts/guile-for-build.path"
echo "Native GUILE_FOR_BUILD ready: $guile"
REMOTE
