# iPadOS target foundation

ATHENA's intended mobile target is iPadOS 27 or later on M2-or-newer iPads,
with the Qt Widgets application and ADS panes retained. This is a target
policy and startup-boundary implementation, **not a buildable or validated
iPad application yet**. Linux Wayland remains the working target.

## Target policy

`AthenaPlatform.cmake` distinguishes `CMAKE_SYSTEM_NAME=iOS` from macOS
(`Darwin`). It generates `src/System/athena_platform.hpp` in the build tree.
Capability values are fixed by the platform profile, not independent user
preferences. Source code should consume this policy instead of using `APPLE`
to mean desktop macOS.

The iPad profile defaults to arm64 and deployment target 27.0. It does not
inherit host-native or AVX compiler flags. arm64 alone does not enforce the
M2 device baseline; distribution/device eligibility remains packaging work.

Implemented boundaries:

- Do not construct the AUDMAP terminal, command-line client/REPL, or Codex
  helper targets; do not require qtermwidget or Readline for this profile.
- Retain AUDMAP resource/protocol libraries, but do not start its listener or
  plugin service. Hide the REPL and plugin manager commands.
- Exclude the Watchdog client implementation and provide inert call sites.
- Exclude ATHENA's CLI dispatch, including AOFM vault import, maintenance,
  conversion, server and website generation modes.
- Hide the website manager command. Its backend has not yet been removed
  from the shared source list.
- Exclude formula cleaner inference and Codex subprocess completion;
  ordinary LaTeX import remains available without formula cleaning.
- Exclude startup source-fingerprint refresh/re-execution and ATHENA's
  Scheme bytecode compilation entry points.
- Force Guile auto-compilation off, require packaged application bytecode,
  and do not create a writable fallback bytecode cache.

Linux behavior is preserved, including its private Guile runtime identity.

## Guile and bytecode inputs

Cross-compilation must not run the desktop Guile bootstrap or execute an
iPad binary to generate bytecode. The future packaging build supplies:

- `ATHENA_GUILE_PREBUILT_PREFIX`: the target-built private Guile/GC runtime.
- `ATHENA_GUILE_PREBUILT_CONFIG_HEADER`: that same runtime build's `config.h`,
  produced with `--disable-jit`.  Autoconf may represent that as either
  `#define ENABLE_JIT 0` or `/* #undef ENABLE_JIT */`; both mean JIT is disabled
  because the runtime guards JIT code with `#if ENABLE_JIT`.
- `ATHENA_PACKAGED_SCHEME_DIR`: the precompiled application `.go` tree,
  preserving paths relative to `ATHENA/progs`.

The application bytecode directory must contain `.complete`, whose first
line is `athena-guile-3.0.10-ipados-arm64-nojit`. Configuration checks this
identity and the presence of each application's compiled module and adds
those files to the bundle resources. This check is not a bytecode ABI or
source-freshness validator: the packaging job must produce matching target
bytecode. Never relabel Linux bytecode by editing its stamp.

Application resources, precompiled application bytecode and Guile's standard
library are collected into the bundle by `AthenaIOS.cmake` and
`AthenaPackagedScheme.cmake`. Guile runtime library/framework packaging and
signing are still separate work; the prebuilt runtime loader expects shared
libraries.

## Application storage

`Subsystems/iOS/ios_entrypoint.cpp` runs through Qt's iOS entry point, not the
Unix executable-discovery path. `ios_paths.mm` sets the following paths using
Foundation's application-container APIs before ATHENA initializes:

| Purpose | Location |
| --- | --- |
| Immutable resources (`ATHENA_PATH`) | Bundle resources / ATHENA |
| Preferences, recovery and user data (`ATHENA_HOME_PATH`) | Application Support / ATHENA |
| Local vault chooser root (`ATHENA_VAULTS_PATH`) | Documents / Vaults |
| Reconstructible cache and temporary files | Application Support / ATHENA / system / cache and tmp |

Only the last row is excluded from device backups. Vaults and recovery data
are not put in purgeable cache directories. Existing desktop paths are unchanged.

## Scenes and system menus

The iPad target requires Qt 6.11 or later. Its scene integration uses Qt's
UIWindowScene delegate implementation and retains Qt's ownership of
UIApplicationMain, input, QPA and the event loop. The application delegate and
scene delegate proxies forward Qt callbacks; they do not copy or swizzle Qt
private classes. Recheck the delegate integration when upgrading Qt.

- Each connected application scene hosts a QTMMainTabWindow and ADS manager.
  UIKit controls window size and lifecycle, rather than desktop screen centering.
- Workspace commands create a system window, move the active pane to a new
  window, or move it back. They transfer the existing dock/editor/actor, not a
  document copy. ADS desktop floating is disabled on iPad.
- Scene disconnection retains editors and unsaved data in process. When a
  session is discarded, its panes move to a surviving shell; closing the last
  window retains a hidden shell for reopening rather than quitting ATHENA.
- Background entry requests realtime saves through the owning BufferActors.
  A worker waits for completion without blocking UIKit; the iOS background
  task ends after completion or OS expiration. Manual-save documents are not
  silently saved. This is not a guarantee against OS process termination.
- Layout cache names are scoped to scene session IDs. Full cross-process
  restoration of scene document membership is not implemented by this cache.
- The system menubar is built from the existing JSON-backed command registry
  and QActions, including dynamic items, availability, checks and keyboard
  commands. There is no second C++ command list. Focus remains pane-specific;
  the in-window Qt menubar is hidden on iPad, not duplicated below the system menu.
- Dialogs and popups are routed to the owning window scene, accounting for Qt
  6.11 QPA's screen-based initial attachment of top-level views.

This Objective-C++ integration has not been compiled with an Apple SDK or
validated on a device. Linux compilation does not verify UIKit lifecycle,
window reparenting, system menus or hardware-keyboard behavior.

## Materials library

The pinned Hayagriva implementation now builds as `libathena_materials`, with
an owned UTF-8 C ABI. Import and rendering consume in-memory BibLaTeX/JSON;
ATHENA no longer starts a helper process or creates request temporary files.
Rust-owned result/error strings are released by its matching free function;
Rust panics are caught before crossing the ABI. Engine state is local to each
request. The existing desktop CLI is a thin file/stdio adapter over this library.

The iPad profile builds the library only. Its default Rust target is
`aarch64-apple-ios` (or `aarch64-apple-ios-sim` for a simulator SDK); the supplied
toolchain must include that target and the matching Apple SDK. Linux uses the
same library and retains the CLI for compatibility.

## Remaining platform work

- Audit the complete dependency and source graph. Shared sources still
  contain desktop subprocess, plugin, exporter and converter implementations;
  disabling their normal entry points is not full dependency removal.
- Compile and validate the UIKit integration with the target SDK: multiple
  scenes, reconnect/discard, pane round trips, background saves, focus/menus,
  dialogs and touch/Pencil/pointer/hardware keyboard input.
- Add process-relaunch restoration of document membership for each scene.
- Port Linux-specific filesystem confinement, discovery and power handling.
- Audit external tools, PDF/printing, fonts, permissions and app packaging.
- Leave Core ML/ANE Continuous RAG optional and for a later pass. Do not
  enable the Linux OpenVINO subprocess worker on iPad.

Do not attempt an iPad configure/build until its toolchain and target
dependencies are supplied. Normal Linux development uses only:

```
cmake --build build_qt6 --target ATHENA.bin -j20
```

Do not build the default ALL target, rebuild deployed bytecode, or run the
full test suite as part of this platform work.

## Agreed synchronization boundary (not implemented)

Nextcloud is a one-way backup sink with independent device destinations,
not the mechanism for two-way synchronization. The future
`athena-sync-server` relays data only while both peers are online. It may
persist limited control metadata, but must not see document plaintext or
retain vault files as a store-and-forward service.

Use end-to-end encryption and authenticated device pairing. There is no
single-writer restriction; a lockfile is not a distributed synchronization
protocol. Realtime saves must not silently overwrite concurrent changes.
Selection of a mature FOSS collaboration algorithm, revision/conflict
semantics, deletion/rename handling, and database synchronization boundaries
remain separate design work. Do not synchronize live SQLite/WAL files as
ordinary changing documents, and do not implement custom cryptography.
