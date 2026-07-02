#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

prefix="${1:-${ATHENA_WIN64_PREFIX:-}}"
build_dir="${2:-${repo_root}/build_win64}"

if [[ -z "${prefix}" ]]; then
  echo "usage: $0 /path/to/x86_64-w64-mingw32-prefix [build-dir]" >&2
  exit 2
fi

prefix="$(cd -- "${prefix}" && pwd)"

mkdir -p "${prefix}/include/sys"
cat > "${prefix}/include/Lmcons.h" <<'EOF'
#include <lmcons.h>
EOF

cat > "${prefix}/include/sys/wait.h" <<'EOF'
#ifndef ATHENA_WIN64_SYS_WAIT_H
#define ATHENA_WIN64_SYS_WAIT_H

#include <errno.h>
#include <sys/types.h>

#ifndef fork
#define fork() (-1)
#endif

static inline pid_t
waitpid (pid_t pid, int* status, int options) {
  (void) pid;
  (void) status;
  (void) options;
  errno= ECHILD;
  return (pid_t) -1;
}

#endif
EOF

cat > "${prefix}/include/sys/resource.h" <<'EOF'
#ifndef ATHENA_WIN64_SYS_RESOURCE_H
#define ATHENA_WIN64_SYS_RESOURCE_H

#include <errno.h>

typedef unsigned long long rlim_t;

struct rlimit {
  rlim_t rlim_cur;
  rlim_t rlim_max;
};

#ifndef RLIMIT_NOFILE
#define RLIMIT_NOFILE 0
#endif

#ifndef RLIMIT_STACK
#define RLIMIT_STACK 1
#endif

static inline int
getrlimit (int resource, struct rlimit* limit) {
  (void) resource;
  (void) limit;
  errno= ENOSYS;
  return -1;
}

static inline int
setrlimit (int resource, const struct rlimit* limit) {
  (void) resource;
  (void) limit;
  errno= ENOSYS;
  return -1;
}

#endif
EOF

cat > "${prefix}/include/athena-win64-compat.h" <<EOF
#ifndef ATHENA_WIN64_COMPAT_H
#define ATHENA_WIN64_COMPAT_H

#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#ifdef __cplusplus
#include <string>
#ifndef CP_UTF8
#define CP_UTF8 65001
#endif
static inline int
unsetenv (const char* name) {
  return _putenv_s (name, "");
}
extern "C" __declspec(dllimport) int __stdcall
MultiByteToWideChar (unsigned int CodePage, unsigned long dwFlags,
                     const char* lpMultiByteStr, int cbMultiByte,
                     wchar_t* lpWideCharStr, int cchWideChar);
template<class T>
static inline std::wstring
texmacs_utf8_to_wide (const T& utf8_str) {
  int size= N (utf8_str);
  if (size <= 0) return std::wstring ();
  const char* data= as_charp (utf8_str);
  int n= MultiByteToWideChar (CP_UTF8, 0, data, size, NULL, 0);
  if (n <= 0) return std::wstring ();
  std::wstring out ((size_t) n, L'\0');
  MultiByteToWideChar (CP_UTF8, 0, data, size, &out[0], n);
  return out;
}
#endif

#ifndef localtime_r
static inline struct tm*
athena_win64_localtime_r (const time_t* timep, struct tm* result) {
  return localtime_s (result, timep) == 0 ? result : 0;
}
#define localtime_r(timep, result) athena_win64_localtime_r ((timep), (result))
#endif

#endif
EOF

export ATHENA_WIN64_PREFIX="${prefix}"
export PKG_CONFIG_LIBDIR="${prefix}/lib/pkgconfig:${prefix}/lib64/pkgconfig:${prefix}/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="${prefix}"
export PKG_CONFIG_PATH=

win64_c_flags="-Drandom=rand -Dsrandom=srand -DGNUTLS_STATIC -include ${prefix}/include/athena-win64-compat.h"
win64_cxx_flags="${win64_c_flags} -fpermissive"
win64_linker_flags="-static-libstdc++ -static-libgcc"
win64_standard_libraries="-Wl,--start-group ${prefix}/lib/ggml-cpu.a ${prefix}/lib/libhogweed.a ${prefix}/lib/libnettle.a ${prefix}/lib/libtasn1.a ${prefix}/lib/libgmp.a -latomic -lcrypt32 -lncrypt -lbcrypt -ladvapi32 -ldbghelp -lucrt -Wl,--end-group -lkernel32 -luser32 -lgdi32 -lwinspool -lshell32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32"

cmake -S "${repo_root}" -B "${build_dir}" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${script_dir}/athena-win64-mingw-toolchain.cmake" \
  -DATHENA_WIN64_PREFIX="${prefix}" \
  -DCMAKE_C_FLAGS="${win64_c_flags}" \
  -DCMAKE_CXX_FLAGS="${win64_cxx_flags}" \
  -DCMAKE_EXE_LINKER_FLAGS="${win64_linker_flags}" \
  -DCMAKE_SHARED_LINKER_FLAGS="${win64_linker_flags}" \
  -DCMAKE_C_STANDARD_LIBRARIES="${win64_standard_libraries}" \
  -DCMAKE_CXX_STANDARD_LIBRARIES="${win64_standard_libraries}" \
  -DATHENA_GUI=Qt6 \
  -DUSE_KF6_KIO_FILE_DIALOGS=OFF \
  -DCMAKE_BUILD_TYPE=Release
