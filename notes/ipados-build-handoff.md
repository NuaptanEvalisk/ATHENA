# ATHENA iPadOS Build Reference

Updated: 2026-11-03. The original porting assignment is complete enough to have
a working native application build; this is not a request to start the port
again. Later commits include native startup/window fixes, palette and gesture
fixes, Hodarium integration and additional image formats. This note does not
assert that a particular current commit has been signed or tested on a device.

## Existing Workflow

The Linux repository is the source of truth. Platform scripts are under
`tools/ipados/`:

- `sync-source-to-vm.sh`: source synchronization.
- `bootstrap-vm.sh`, `install-host-tools.sh`: host prerequisites.
- `build-qt.sh`, `build-athena-deps.sh`, `build-materials.sh`: dependencies.
- `configure-athena.sh`, `build-athena.sh`: device application configuration
  and build. Read the scripts before choosing which stages need rerunning.

The earlier BDW-GC/Guile host and target stages and Scheme-bytecode build were
removed by the native runtime migration. They are not part of this workflow.

`build-athena.sh` uses the explicit Apple target `ATHENA`, Debug configuration,
and `ATHENA_IPADOS_JOBS` (default 15). Linux uses `ATHENA.bin -j16`.
The default target deployment setting in the current script is iPadOS 27.0.

## VM Entry Points

The existing SSH defaults are:

```sh
ssh -o BatchMode=yes -p 2222 felix@127.0.0.1
```

Script overrides are `ATHENA_IPADOS_SSH_TARGET`, `ATHENA_IPADOS_SSH_PORT`,
and `ATHENA_IPADOS_REMOTE_DEVELOPER_ROOT` (default
`/Users/felix/Developer`). The device build directory is
`/Users/felix/Developer/athena-build/ipados`.

The October 6 inspection recorded an Intel macOS VM with 16 vCPUs, 32 GiB RAM,
Xcode under `/Applications/Xcode.app/Contents/Developer`, and Tahoe. Those are
dated observations, not a claim that its SDK/tools are unchanged. Inspect
`sw_vers`, `xcodebuild -version` and `xcodebuild -showsdks` when platform work
is actually requested; no remote inspection was performed for this note refresh.

## Platform Boundaries

- Host build tools and arm64 device libraries are different artifacts. Do not
  run target executables as host generators or mix Linux runtime resources into
  the iPad bundle.
- The native iPad build does not package a Guile interpreter, BDW-GC or Scheme
  bytecode. Native XML command interface changes must reach the compiled binary.
- Relevant integration lives in `cmake/AthenaPlatform.cmake`,
  `cmake/AthenaIOS.cmake`, `cmake/AthenaCommands.cmake` and
  `src/Platform/iOS/`.
- A Linux build does not update an IPA. Compiling a device bundle, signing it,
  installing it and verifying actual device behavior are distinct milestones.
  Do not label an unsigned ZIP as an installable signed IPA.
- Use the existing scripts and explicit user scope. This reference does not
  authorize package installation, VM resets, deployment or data migration.
  Do not restart a VM against an already-running disk or disable SSH host-key
  verification.
