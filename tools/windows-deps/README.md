# ATHENA Windows 64-bit cross dependency tree

This directory contains the dependency helpers and MinGW-w64 toolchain used by
ATHENA's experimental native Windows build.

## Current status

The application now uses native C++ commands on every platform. There is no
embedded Scheme runtime, bytecode or private interpreter dependency to cross
compile or package.

The current Windows port and release assembly remain unvalidated.
`configure-athena-win64.sh PREFIX BUILD_DIR` configures the existing MinGW-w64
toolchain; build the `ATHENA.bin` target explicitly and validate it under Wine.
`build-athena-win64-release.sh` remains an explicit failure until the current
native dependency closure and Windows release packaging have been verified.

## Compiler and prefix policy

- Windows cross builds use `x86_64-w64-mingw32-gcc` and
  `x86_64-w64-mingw32-g++`.
- Use a dedicated target prefix, normally `build_windows/prefix`.
- Never expose Linux host Qt, libraries, or pkg-config metadata to the target
  build.
- Generic dependency helpers in this directory populate that target prefix.
