# ATHENA source ownership and repository layout

Updated: 2026-11-03. This describes the physical source and service layout.
It does not claim that build targets and compile-time dependency boundaries are
fully modularized.

## Source versus service versus tooling

- `src/` contains the native desktop application, libraries that are linked
  directly into it, and helper executables installed as part of that runtime.
  `src/Materials/Engine/` owns the Rust/Hayagriva citation engine, and
  `src/Integrations/Codex/Bridge/` owns the installed Codex AppServer bridge.
- `services/` contains independently deployed applications. The Hodarium Go
  authority/relay, Web-Accessible ATHENA broker, and delegation transmitter each
  retain their own application entrypoints and deployment resources. The
  native Hodarium client under `src/Sync/Hodarium/` is a separate component.
- `clients/` contains external AUDMAP client software. The C++ SDK under
  `clients/cpp/` owns the reusable connection implementation, and the native
  command-line/REPL frontends are under `clients/audmap-cli/`.
- `tools/` contains development, generation, packaging, performance and
  operational maintenance utilities. Historical AUDMAP document migration
  scripts remain here under `tools/migrations/interop/`. The echo-plugin
  example is a product example, now under `ATHENA/examples/echo-plugin/`.

## Build boundaries

The main CMake project adds the linked Materials engine, Codex bridge, and
standalone service targets using their new source paths. Existing executable,
library, and install target names stay unchanged. CMake build-tree locations for
the transmitter and web server follow the new `services/` subdirectories;
packaging and documentation point to those new locations. The Rust Cargo
target output remains `build*/materials-engine-cargo/`.

The Go Hodarium server/relay remain their own `services/hodarium/go.mod` module;
the web broker and delegation transmitter remain separately deployable services.
They must not be absorbed into the desktop `athena_body` compilation unit.

The former TeXmacs directory hierarchy has been relocated according to
responsibility. It is no longer the top-level source organization:

| Current source directory | Responsibility |
| --- | --- |
| `src/App/` | Program entry, startup, actor runtime, buffer/view coordination, native commands, windows and watchdog |
| `src/Core/` | Tree/metadata primitives, collections, Unicode text, language support, URL and timers |
| `src/Document/` | Document tree model, node contracts, editing history, source serialization, format conversion and persistence |
| `src/Domains/` | Vault, Artifacts, Namespaces, Knowledge, References and Websites |
| `src/Editor/` | Editing engine, input/interaction commands, selection, modification and text processing |
| `src/Infrastructure/` | Files, process/network links, virtual filesystem and common runtime utilities |
| `src/Integrations/` | External adapters (including the Codex bridge), system APIs and optional libraries |
| `src/Interop/` | AUDM selectors, resolution and document/resource adapters |
| `src/Materials/` | Materials domain model and its Rust/Hayagriva engine |
| `src/Math/` | Mathematics-related geometry, handwriting and graphical tools |
| `src/Platform/` | Unix, Windows, macOS and iPadOS entrypoints and platform adapters |
| `src/Rendering/` | Fonts, PDF, image and graphical rendering support |
| `src/Services/` | AUDMAP transport, RAG, MCP, Google and plugins |
| `src/Sync/` | Hodarium client, synchronization state and headless node entrypoint |
| `src/Typesetting/` | Typesetter, boxes, environment, macros, styles and page layout |
| `src/UI/` | Abstract widgets and Qt UI broken into focused, named directories |

`src/UI/Qt/` now groups UI sources under `Commands`, `Dialogs`, `Document`,
`Editor`, `Integrations`, `Knowledge`, `Materials`, `Platform`, `Rendering`,
`Shell`, `Toolkit`, `Vault` and `Widgets`. The old completely flat directory
is gone. The former `src/ATHENA/Data/` is split by actual consumer/domain;
for example `vault.cpp` now belongs to `src/Domains/Vault/`, `materials.cpp`
to `src/Materials/Model/`, and the Hodarium logical database to
`src/Sync/State/`.

This change is a **physical source-path reorganization**, not a redesign of
source types, protocols, XML v3, BufferActor ownership, or persisted data.
The desktop `athena_body` library and its transitional global include paths
are intentionally retained. Splitting this library into separately enforceable
module targets remains a distinct architectural change.

## Verification boundary

The initial November 3 source move was committed without building, as requested
while the laptop was on battery. After power was restored, the configured Qt6
desktop build was regenerated and `ATHENA.bin` built with IntelLLVM and `-j16`.
The Codex bridge, Web broker, delegation transmitter, AUDMAP CLI/REPL, native
Hodarium headless node, and the Go Hodarium Server/Relay all built successfully.

The separate headless-node build exposed two omitted CMake requirements:
`athena_hodarium` requires the source-root include path for its relocated
`Sync/Hodarium/` headers, and the minimal node links the core field-contract
implementation, which depends on the authoritative `ATHENA/misc/node-fields.json`
and the same generated-header template as desktop ATHENA. Both are now explicit
in the corresponding CMake target configurations.

The new desktop executable successfully loaded and exported a synthetic native
XML v3 document with an isolated temporary profile. `codex_bridge_protocol`,
`athena-web-server-tests`, and `athena-web-session-helper-tests` passed (3/3).
Build logs are in `build_qt6/source-layout-*.log`; node logs are in
`build_hodarium_node/source-layout-*.log`.

The completed build-tree `ATHENA.bin` has ELF build ID
`7bda6827f34c5c5407d58eb8af0d400211f7c899`. The installed
`ATHENA/bin/ATHENA.bin` remains the previous build ID
`89aaf0afb7173ea7534dc13f01b432264444dc48`; it was **not** replaced.
Active user processes, personal configuration and production vault files were
not touched.
These targeted checks do not constitute a complete editor regression suite or
cross-platform acceptance test.
