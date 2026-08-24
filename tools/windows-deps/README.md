# ATHENA Windows 64-bit cross dependency tree

This directory contains the dependency helpers and MinGW-w64 toolchain used by
ATHENA's experimental native Windows build.

## Current status

The Windows build is temporarily unavailable while ATHENA's vendored private
Guile 3 runtime is ported to MinGW-w64. ATHENA no longer supports Guile 1.8,
and the previous Guile 1.8 cross-build helper has been removed rather than kept
as a misleading fallback.

`build-athena-win64-release.sh` therefore fails immediately with an explicit
diagnostic. The other dependency helpers remain because they are still the
target prefix used by the eventual private-runtime port.

The Windows port is ready to resume when all of the following are true:

- `3rdparty/athena-guile` builds for `x86_64-w64-mingw32` using a native
  same-version `GUILE_FOR_BUILD`;
- the generated target runtime includes `libathena-guile`, its Scheme standard
  library, and its compiled standard library;
- `ATHENA.bin` links only to that runtime and passes the native module/runtime
  regression tests under Wine;
- packaging installs the private runtime without consulting host or target
  system Guile metadata.

Do not restore `guile-1.8`, `GUILE18_SOURCE_DIR`, or a `guile-1.8.pc` check to
work around this boundary.

## Compiler and prefix policy

- Windows cross builds use `x86_64-w64-mingw32-gcc` and
  `x86_64-w64-mingw32-g++`.
- Use a dedicated target prefix, normally `build_windows/prefix`.
- Never expose Linux host Qt, libraries, or pkg-config metadata to the target
  build.
- Generic dependency helpers in this directory may still be used to populate
  that prefix while the private Guile port is developed.
