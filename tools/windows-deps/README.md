# ATHENA Windows 64-bit cross dependency tree

This directory contains opt-in helpers for cross-compiling ATHENA for 64-bit
Windows from Linux.  Nothing here is included by the normal Linux build unless
the scripts or toolchain file are passed explicitly to CMake.

## Compiler policy

- Native Linux builds may continue to use Intel `icx`/`icpx`.
- Windows cross builds use the GNU MinGW-w64 target compiler by default:
  `x86_64-w64-mingw32-gcc` and `x86_64-w64-mingw32-g++`.
- Do not point a Windows build at Linux host Qt or Linux host pkg-config files.

## Prefix layout

Use a dedicated Windows target prefix, for example:

```sh
export ATHENA_WIN64_PREFIX="$PWD/build_win64_prefix/x86_64-w64-mingw32"
```

The prefix must contain Windows target headers, libraries, CMake packages, and
pkg-config files.  It must not contain Linux host libraries.

## Required target dependency surface

ATHENA's current CMake configuration requires at least:

- CMake packages: `Qt6`, `mimalloc`, `SQLite3`, `Freetype`, `GMP`, `GnuTLS`,
  `PNG`, `Iconv`, `ZLIB`, `JPEG`
- pkg-config modules: `spdlog`, `llama`, `libzstd`, `MagickWand`, `guile-1.8`
  and `libsodium`
- headers/libraries found directly: `common.h` plus `llama-common`/`common`,
  `ResvgQt.h` plus `resvg`, and `libtcc.h` plus `tcc`/`libtcc`
- Qt6 modules: `Core`, `Gui`, `Widgets`, `PrintSupport`, `Svg`, `Network`,
  `Core5Compat`

The commonly available MXE packages cover most of the generic surface
(`qt6-qtbase`, `qt6-qtsvg`, `qt6-qt5compat`, `imagemagick`, `spdlog`, `gnutls`,
`gmp`, `freetype`, `sqlite`, `zstd`, `libpng`, `libjpeg-turbo`, `libiconv`,
`zlib`).  ATHENA-specific pieces such as `mimalloc`, `libtcc`, `llama-common`,
`ResvgQt` and `llama-common` may need overlay ports or manual installation into
the same prefix.  ATHENA's Windows dependency tree must use Guile 1.8.8 from the
local source tree:

```sh
/home/felix/data/Software/TeXmacs/obs/guile-1.8.8
```

Use `build-guile18-win64.sh` to build that source tree out-of-tree and install
it into the Windows prefix.

## Build and package ATHENA

After preparing the prefix, use the main release script:

```sh
tools/windows-deps/build-athena-win64-release.sh
```

By default this uses:

- Windows target prefix: `build_windows/prefix`
- isolated CMake build tree: `build_windows`
- release output: `release/windows/ATHENA`

The script checks the prefix, configures and builds `ATHENA.exe`, copies the
main `ATHENA/` runtime tree to the release directory, and installs the generated
executable, ADS DLL, runtime DLLs, and Qt plugins into `release/windows/ATHENA`.

The generated CMake configuration must define `OS_MINGW64` for x86_64 MinGW
builds. This selects the `src/Subsystems/Windows64` system layer instead of the
old 32-bit Windows compatibility layer, which is required for Windows' LLP64
ABI and for correct Guile smob pointer/tag handling.

The release script excludes optional formula-cleaner model artifacts such as
`.gguf`, `.safetensors`, and formula-cleaner virtual environments from the main
Windows runtime tree. Package those artifacts separately if they are needed.

For non-default paths:

```sh
tools/windows-deps/build-athena-win64-release.sh \
  --prefix "$ATHENA_WIN64_PREFIX" \
  --build-dir build_windows \
  --release-dir release/windows/ATHENA \
  -j"$(nproc)"
```

## Dependency preparation

If the prefix is not prepared yet, build or install the target dependencies
first. The helper scripts can be run individually:

```sh
tools/windows-deps/check-win64-prefix.sh "$ATHENA_WIN64_PREFIX"
tools/windows-deps/build-mimalloc-win64.sh "$ATHENA_WIN64_PREFIX"
tools/windows-deps/build-sqlite-win64.sh "$ATHENA_WIN64_PREFIX"
tools/windows-deps/build-zlib-win64.sh "$ATHENA_WIN64_PREFIX"
tools/windows-deps/build-libpng-win64.sh "$ATHENA_WIN64_PREFIX"
tools/windows-deps/build-libiconv-win64.sh "$ATHENA_WIN64_PREFIX"
tools/windows-deps/build-libjpeg-turbo-win64.sh "$ATHENA_WIN64_PREFIX"
tools/windows-deps/build-zstd-win64.sh "$ATHENA_WIN64_PREFIX"
tools/windows-deps/build-libsodium-win64.sh "$ATHENA_WIN64_PREFIX"
tools/windows-deps/build-imagemagick-win64.sh "$ATHENA_WIN64_PREFIX"
tools/windows-deps/build-freetype-win64.sh "$ATHENA_WIN64_PREFIX"
tools/windows-deps/build-gmp-win64.sh "$ATHENA_WIN64_PREFIX"
tools/windows-deps/build-nettle-win64.sh "$ATHENA_WIN64_PREFIX"
tools/windows-deps/build-libtasn1-win64.sh "$ATHENA_WIN64_PREFIX"
tools/windows-deps/build-gnutls-win64.sh "$ATHENA_WIN64_PREFIX"
tools/windows-deps/build-spdlog-win64.sh "$ATHENA_WIN64_PREFIX"
tools/windows-deps/build-llama-win64.sh "$ATHENA_WIN64_PREFIX"
tools/windows-deps/build-resvg-win64.sh "$ATHENA_WIN64_PREFIX"
tools/windows-deps/build-tinycc-win64.sh "$ATHENA_WIN64_PREFIX"
tools/windows-deps/build-guile18-win64.sh "$ATHENA_WIN64_PREFIX"
```

The configure and release scripts deliberately set `PKG_CONFIG_LIBDIR` and
`PKG_CONFIG_SYSROOT_DIR` so CMake cannot accidentally consume Linux host
pkg-config metadata.
