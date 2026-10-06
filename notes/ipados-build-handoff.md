# ATHENA iPadOS Build Handoff

Updated: 2026-10-06. This is an implementation assignment, not just an audit.

## Objective

Complete the missing build and platform integration work so that ATHENA can be
cross-compiled in the existing macOS VM into an actual arm64 iPad application.
The product target remains iPadOS 27 or later on M2-or-newer iPads. Preserve the
working Linux Wayland application.

Do not stop after adding platform switches, producing a dependency inventory,
compiling a Qt sample, or linking an executable without its runtime resources.
Deliver reproducible dependency/runtime/application builds and a complete device
`.app`. Produce a signed installable artifact when the user supplies the required
signing identity/provisioning. If signing or physical-device access is unavailable,
finish the unsigned device build and clearly separate that accomplishment from
device launch validation. Do not label an unsigned ZIP as an installable IPA.

The user asked another session to do this work to conserve the current session's
quota. Work independently through real build failures, not repeated speculative
audits. Ask for genuinely missing permissions, dependencies or product decisions.

## What ATHENA Is

ATHENA is the Advanced Typesetting and Hypertext Environment for Notes and
Archives, a mathematics-oriented knowledge workspace forked from GNU TeXmacs.
It is not a LaTeX frontend, web application, or compatibility distribution of
upstream TeXmacs.

- Documents are native structured trees. Ordinary text is UTF-8; native `.ath`
  persistence is versioned XML, including node UUIDs and typed properties.
- Source identity lives in documents. Wikilinks and transclusions use native node
  UUIDs and tmfs; a location index is a rebuildable cache. Do not revive generated
  anchors or require AUDMAP for internal rendering/navigation.
- Enunciations are unified `enunciation` nodes with properties such as `kind`.
  Definition/theorem/proof are not separate new source node types.
- Vaults contain documents, namespace databases, artifacts, embeddings and user
  settings. Persistent artifact/model databases are not disposable caches.
- Namespaces organize files; their custom sorters now use Luau, not libtcc.
- AVD (`.avd`) is a compound virtual document over namespace members: one logical
  scrolling/editing surface, per-file environments and shared counters. It is not
  a stack of unrelated editor windows.
- The application shell owns the menubar, command palette and application
  shortcuts. It remains open with no buffers. Focus can belong to a namespace
  pane rather than an editor. Editor toolbars remain editor-owned.
- The GUI is Qt Widgets with ADS docking panes. Retain this desktop-style UI on
  iPad; do not replace it with QML, a web UI or a new editor.
- C++ is progressively replacing Scheme. Guile and shipped Scheme/style modules
  still remain essential. Removing Guile is not this assignment.
- ATHENA has its own Unicode font selection/fallback infrastructure, HarfBuzz and
  FreeType. Pango was intentionally removed; do not reintroduce it.
- Native PDF export is retained. Do not revive PS-to-PDF or distill pipelines.

Read repository `AGENTS.md` first. Also read:

1. `cmake/README-iPadOS.md` for the implemented platform foundation.
2. `notes/actor-runtime-architecture.md` and `notes/font-domain-ownership.md`.
3. `notes/athena-developer-orientation.md` for general navigation/build rules.

The older orientation is dated September 25. Its discussion of anchored ranges
predates the UUID model; current code and the statements above take precedence.
Historical handoffs are context, not extra tasks to resume.

## Host, VM And Access

### Linux source of truth

```text
/home/felix/data/Software/TeXmacs/texmacs
```

Production vault: `/home/felix/data/Notes`. Never use it for write tests, migrations
or experimental iPad synchronization. Use an isolated temporary vault/copies.
Do not alter `/home/felix/.ATHENA`, deployed bytecode or installed Linux binaries
as collateral effects of preparing the iPad build.

At handoff, HEAD is `b0f2619ae13dee173741e7a5106ce612e458574c`. **The iPad
foundation and Materials library changes are still dirty/untracked.** A fresh
clone of HEAD does not contain them. Start with `git status --short`, recent full
commit messages and the actual diff. Preserve all existing and concurrent work.

### macOS build VM

From the Linux host, using the existing authorized SSH key:

```sh
ssh -o BatchMode=yes -p 2222 felix@127.0.0.1
ssh -p 2222 felix@127.0.0.1 'sw_vers; xcodebuild -version; xcodebuild -showsdks'
```

Verified configuration:

| Item | Value |
| --- | --- |
| macOS | Tahoe 26.6.2, build 25G83 |
| Host architecture inside VM | x86_64 |
| Xcode | 26.6, build 17F113 |
| Developer directory | `/Applications/Xcode.app/Contents/Developer` |
| Device SDK | iPhoneOS 26.5 |
| SDK path | `/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS26.5.sdk` |
| VM resources | 32 GiB RAM, 16 virtual CPUs |
| VM login | `felix` |

No password belongs in this document, scripts, git, logs or command-line args.
Key authentication already works. Ask the user for an interactive administrative
step if needed; do not disable SSH host-key verification or overwrite keys.

The VM's GUI has no accelerated graphics and previously stayed on a Welcome
spinner. SSH and Xcode work. Use SSH for builds; fixing its desktop is not a
prerequisite for producing a device application. The Intel host can cross-compile
arm64 iOS code; it cannot execute the resulting device binary.

In the inspected noninteractive SSH environment, `cmake`, `ninja` and `cargo`
were not found. Usual Qt installation directories were also absent. Recheck:
the user may install dependencies after this handoff. Distinguish PATH problems
from actual absence, and macOS libraries from iOS libraries.

Suggested new VM-owned layout, not yet created by this handoff:

```text
/Users/felix/Developer/ATHENA/                  source mirror
/Users/felix/Developer/athena-deps/host/        x86_64 macOS build tools
/Users/felix/Developer/athena-deps/ipados/      arm64 iphoneos dependencies
/Users/felix/Developer/athena-build/ipados/     isolated device build
/Users/felix/Developer/athena-artifacts/        bundles and build manifests
```

Synchronize the **current working tree**, including new platform files and needed
vendored sources, not just committed HEAD. Inspect an `rsync --dry-run` before a
large transfer. Exclude Linux build trees, deployed Linux binaries/libraries,
unrelated model files, profiles, vaults and VM images. Do not use `--delete` on a
shared/general directory. Pick one authoritative edit location and return every
source change to the Linux repository; no fixes existing only in a VM copy.

### Console and rollback information

```sh
remote-viewer spice://127.0.0.1:5900
```

VM configuration is under `/home/felix/data/Software/macos/OSX-KVM`.
`OpenCore-Boot.sh` starts its daemonized QEMU; do not launch another instance
against a running disk. QMP is at `runtime/qmp.sock`, with a small helper
`runtime/qmp-control.py`. The active boot image is `OpenCore/OpenCore-Tahoe.qcow2`.

Cold pre-upgrade disk/NVRAM/boot backups are retained at
`/home/felix/data/Software/macos/pre-tahoe-20261006`. Do not delete or restore them
as part of compilation work. One installer reboot hit AppleSMC panic *after*
complete filesystem unmount; a QEMU reset allowed the installed Tahoe to boot.
That was a specific observed shutdown failure, not permission to reset an active
installer or filesystem whenever the screen stops changing.

## Non-Negotiable Product And Work Rules

- Supported product platforms are Linux Wayland and iPadOS 27+, M2+. `arm64`
  alone does not enforce an M2 device baseline.
- No JIT on iPad, including Guile and any other dependency. Ship compatible `.go`
  bytecode; no on-device compilation, source-fingerprint rebuild/re-exec, or
  writable fallback bytecode cache. Luau must not enable its native JIT/codegen.
- First iPad release excludes formula cleaner inference, Watchdog, ATHENA CLI
  modes, AOFM vault importer, website exporter, external plugins, Codex bridge,
  AUDMAP REPL and the local AUDMAP/plugin listener.
- Preserve ordinary editing, local vaults, namespace/AVD functionality, Materials,
  native menus/toolbars and PDF export. Local-first application storage is agreed.
  Do not quietly drop a retained feature to make linkage succeed.
- Keep ADS panes; use iPad system scenes/windows and the system menubar. There
  must not be a second manually maintained menu/command registry for UIKit.
- Linux OpenVINO/NPU subprocesses must not run on iPad. Core ML/ANE acceleration
  is a later optional project, not a prerequisite for this first build.
- No new synchronization implementation in this assignment. Nextcloud is a
  one-way backup sink, not live two-way vault sync. The future online-only E2EE
  relay is separate. Never sync changing SQLite/WAL files as ordinary files.
- No system package installation without explicit user approval, on Linux or in
  the VM. This includes Homebrew/system package managers and sudo installs.
  Identify exact missing packages and commands, then ask. Do not evade the rule
  by unpacking packages into private prefixes. Explicit source cross-builds of
  target dependencies are the work of this assignment, not a way to hide a
  missing host prerequisite.
- Never run a full test suite unless explicitly requested. Do not build test
  executables routinely. Batch changes and use a few relevant checks; avoid
  spending the task on test harnesses.
- Preserve unrelated modifications. Do not reset/clean the worktree. Never
  install to or overwrite the user's Linux deployment from an iOS build.
- New C++/Objective-C++ files need the project's module/description/copyright/GPL
  header. Declarative registrations belong in existing JSON/data sources where
  appropriate; algorithms and ownership logic belong in C++.
- Native Scheme interfaces are declared in XML under `src/Scheme/Glue/`, not
  handwritten wrappers. Read its README and use the current generator.

### Build commands

Normal Linux validation is **only**:

```sh
cmake --build build_qt6 --target ATHENA.bin -j20
```

Do not run bare `cmake --build build_qt6`, ALL, unrestricted ninja/make, or use
`-j8`. Default builds have previously damaged deployed Scheme bytecode.

The new **Apple** application target is currently named **`ATHENA`**, not
`ATHENA.bin` (`src/CMakeLists.txt`). Once the separate device configuration is
valid, its explicit application build has this shape:

```sh
cmake --build /Users/felix/Developer/athena-build/ipados \
  --config Debug --target ATHENA -j20
```

This is not a ready-to-run configure recipe. Supply real target prefixes and
the Qt iOS toolchain first. Dependency/bootstrap jobs also need explicit targets
and separate build directories. Do not run a target-built program on the host to
generate files. If resources genuinely prevent 20 jobs, ask before changing it.

## What Exists, And What Does Not

Read these files rather than inferring completeness from names:

| Area | Current files and state |
| --- | --- |
| Target capabilities | `cmake/AthenaPlatform.cmake`, `src/System/athena_platform.hpp.cmake`; iOS vs macOS separated, arm64/no host AVX policy |
| Build wiring | Root and `src/CMakeLists.txt`; platform branches exist, complete dependency/source isolation does not |
| Bundle/resources | `cmake/AthenaIOS.cmake`, `src/Subsystems/iOS/Info.plist.in`; initial resource collection and iPad bundle properties |
| Entry/storage | `ios_entrypoint.cpp`, `ios_paths.mm`, `athena_ios.hpp` under `src/Subsystems/iOS/` |
| UIKit scenes/menus | `src/Subsystems/iOS/ios_application.mm`, Qt shell/menu/persistence modifications; never Apple-SDK compiled yet |
| Guile target input | `cmake/AthenaGuile.cmake`; requires a separately built private target runtime; no complete target bootstrap supplied |
| Shipped bytecode | `cmake/AthenaPackagedScheme.cmake`; checks module presence/runtime stamp, not actual ABI or freshness |
| Materials | `tools/materials-engine/src/lib.rs`, `Cargo.toml`, its CMake, `materials_engine.cpp/.hpp`; Hayagriva C ABI library with CLI adapter |
| Startup exclusions | `athena.cpp`, `init_texmacs.cpp`, `guile_tm.cpp`, several Qt integration files |

Most of that foundation remains in the dirty worktree. It was developed on Linux;
Linux compilation does not validate Objective-C++ APIs or iOS lifecycle behavior.

## Implementation Sequence

### 1. Fix The SDK/Target Contract And Inventory Dependencies

Xcode 26.6 is installed intentionally and carries SDK 26.5, **not 27**. Current
CMake defaults to and rejects anything below deployment target 27.0. Separate
base SDK, compiler capabilities, minimum OS and product device policy. Determine
what this toolchain actually accepts before writing a build recipe. New iPadOS
27-only APIs cannot be obtained by changing a CMake number.

Do not silently lower the agreed minimum runtime OS. If this Xcode cannot express
the required contract or necessary APIs, present the exact limitation and options
to the user. Do not assume that upgrading Xcode on an Intel VM is always supported.
Using an older SDK on newer runtime systems is not inherently prohibited, but
API availability and SDK-linked behavior need explicit treatment.

Review the complete CMake graph. At handoff it still requires, among others,
Qt >=6.11, KF6 SyntaxHighlighting/Completion, VTK RenderingOpenGL2, mimalloc,
Boost, SQLite, FreeType, HarfBuzz, ICU, GMP, GnuTLS, Hunspell, LMDB, image/compression
libraries, libsodium, MagickWand and interop dependencies. Bundled third-party
targets and plugin/runtime targets also need auditing. Do not treat this list as
complete or assume every item should be removed.

For each dependency establish: actual consumers, retained iPad feature, iOS
support, source/version/license, host versus device artifacts, and packaging.
Remove requirements and source targets only for intentionally excluded features.
For retained functionality, use a mature supported implementation or ask about a
real incompatibility; do not insert stubs that pretend to succeed.

Examples requiring attention:

- VTK/OpenGL platform support is not solved by finding desktop VTK in Homebrew.
- Disabled plugin UI/listeners still have unconditional targets/shared consumers.
- Some Unix sources, subprocesses, filesystem confinement, device discovery and
  power handling still need mobile implementations or source exclusion.
- Audit `APPLE` checks so Carbon/AppKit/Cocoa/IOKit desktop assumptions do not
  leak into the UIKit target.
- Inspect Luau/native execution configuration and current font discovery paths.

### 2. Make Host Tools And Device Dependencies Reproducible

Prepare approved host tools, Qt host tools and the **iOS arm64** Qt libraries,
including required modules. Qt >=6.11 is an existing requirement because the
foundation relies on its scene-aware iOS QPA; verify against the actual pinned
Qt source rather than weakening that version check.

Build the retained dependency graph for iphoneos, with isolated CMake/pkg-config
search roots. The x86_64 VM architecture must not select x86 target libraries.
Likewise, a macOS arm64 binary is not an iOS arm64 binary. Validate Mach-O platform
load commands as well as architecture.

Separate executable host tools (Qt moc/rcc, code/data generators, Guile compiler,
ICU tools, etc.) from libraries linked into the app. Account for cross-compiling
`try_run` and build-generated data without disabling meaningful checks globally.
Pin versions and record patches/options so this does not become an undocumented
collection of commands under `/tmp`.

### 3. Finish Private Guile, GC And Bytecode Production

Keep the repository's patched private Guile and GC; an arbitrary system Guile is
not interchangeable. Existing checks require the private
`scm_athena_set_auto_compile_callback` symbol.

Current target inputs:

```text
ATHENA_GUILE_PREBUILT_PREFIX
ATHENA_GUILE_PREBUILT_CONFIG_HEADER
ATHENA_PACKAGED_SCHEME_DIR
```

Current runtime identity:

```text
athena-guile-3.0.10-ipados-arm64-nojit
```

Build the target runtime with JIT disabled, and validate how `ENABLE_JIT` is
actually represented in its generated config. Audit GC/thread/Mach integration,
unsupported process APIs, foreign modules and runtime library dependencies.
Current prebuilt loading expects shared libraries; choose and implement a valid
iOS static/framework packaging strategy rather than leaving loose unembedded
dylibs or assuming Qt will package arbitrary libraries automatically.

Create a reproducible host-side compiler/bootstrap path for both Guile standard
modules and ATHENA modules. Target architecture, byte order, word size, runtime
ABI, generated bindings and source freshness all matter. Investigate the real
Guile cross-compilation mechanism. Never copy Linux `.go` files and rewrite the
`.complete` stamp to make CMake accept them. The stamp alone proves nothing about
compatibility. Do not execute an iPad program during the host build.

Verify that runtime module search paths point into the signed bundle and that no
source recompilation/JIT fallback occurs. Keep Linux runtime/cache production
separate and unchanged.

### 4. Compile ATHENA And Correct Real Platform Failures

Configure the isolated device target with the real dependency prefixes and
`BUILD_TESTS=OFF`; build the explicit application target with `-j20`. Work through
real configuration, compilation and linkage failures by fixing their owning
layer. Do not weaken ownership assertions, discard functionality, or add a web
replacement merely to obtain a binary.

Materials must build its `aarch64-apple-ios` Rust library, not launch its desktop
helper. Ensure the Rust target/SDK and static C ABI link dependencies are correct.
An Intel simulator would require different simulator artifacts; it is not the
primary deliverable and must not be confused with arm64 device validation.

Compile and correct the UIKit implementation. Review Qt's actual ownership of
UIApplication, scenes, native views, input and event loop. Avoid duplicated
delegates, unsupported private-class assumptions and undocumented lifecycle hacks.
Retain pane-specific focus, application shortcuts, zero-buffer shell, menus,
window/pane transfer, dialogs and asynchronous background saves.

Thread ownership remains mandatory:

- Main thread owns Qt/UIKit widgets and the GUI registry.
- Each BufferActor owns its live tree, editor, cursor, undo and typesetting state.
- Font domains/FreeType objects and rendering resources have owner/lifetime rules.
- Background work receives detached owned data or actor messages, not arbitrary
  pointers into live documents. Moving everything onto Main is not a fix.

### 5. Complete Bundle, Signing And Delivery

Package the actual executable, permitted frameworks/libraries, Qt static plugins
or frameworks as appropriate, styles, fonts, JSON/declarative data, Scheme modules
and matching bytecode. Audit every dependency's install name/rpath and platform.
The app must not depend on `/usr/local`, Homebrew, the source/build tree or writable
bundle contents. Keep resource paths and loadable Guile modules consistent.

Use a correct iPad Info.plist, scene declarations, bundle version/identifier and
required assets. No JIT, executable-memory, subprocess, or private entitlements as
workarounds. Do not invent an entitlement that enforces M2; report the actual
available mechanism for the agreed hardware baseline.

Keep signing configurable. Ask for the development team/profile/device access
when necessary, never embed account credentials or commit provisioning secrets.
Allow an unsigned build for compile/link/package verification while clearly
labeling it as such. A physical launch may require user-side installation rather
than USB passthrough to this VM; that is not a reason to skip the build.

## Focused Acceptance And Definition Of Done

Do not run exhaustive tests. Validate completed blocks with high-signal checks:

1. A recorded configure command and explicit target build produce the real
   arm64 iphoneos application from synchronized current source.
2. Mach-O inspection establishes iOS device platform, expected deployment target,
   correct dependency architectures/install names, and no accidental macOS/Linux
   libraries. Resource/bytecode inventories are complete.
3. Re-running the documented build is reproducible and does not depend on hidden
   VM edits, Linux installed resources, target execution, or on-device compilation.
4. With device access: launch, Guile/module initialization, create/open/edit/save
   an isolated `.ath`, mathematics/fonts and a minimal native PDF export. Check
   system menu/focus and basic scene/background-save behavior. Record precisely
   what was exercised; do not claim lifecycle correctness from compilation alone.
5. The excluded services do not launch, no process/JIT fallback is attempted, and
   vault/recovery paths are writable app-container paths rather than bundle paths.
6. Changes affecting Linux receive the normal explicit Linux application build,
   not ALL or the full test suite. Do not auto-deploy an unvalidated port over the
   working installation.

Deliver source changes, dependency/runtime build recipes, the artifact path,
exact commands and versions, and a short honest list of remaining device-only
validation. Stage logical batches only when instructed to commit. For any commit,
read recent full messages and use `type: imperative summary`, blank line, concrete
bullets; never sweep other sessions' work into a commit.

Do not announce completion merely because the SDK can compile a small program.
If a genuine blocker needs the user's action, identify the exact prerequisite and
what has already been completed. Avoid making the user repeatedly rediscover
setup steps.

## Reference Documentation

Use upstream documentation/source for the actual pinned versions:

- Xcode SDK/system requirements: https://developer.apple.com/xcode/system-requirements
- Qt for iOS and toolchain usage: https://doc.qt.io/qt-6/ios.html
- Qt iOS source builds: https://doc.qt.io/qt-6/ios-building-from-source.html
- CMake Apple cross-compilation: https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html#cross-compiling-for-ios-tvos-or-watchos
- Guile internals and compilation: https://www.gnu.org/software/guile/manual/

The user's existing Xcode 26.6 is the starting point. Do not spend the task
reinstalling macOS, re-downloading its installer, or repeating the completed VM
upgrade investigation.
