# ATHENA source ownership and repository layout

Updated: 2026-11-03. This describes the first phase of source reorganization,
not a claim that the inherited editor and Qt directory boundaries are finished.

## Source versus service versus tooling

- `src/` contains the native desktop application, libraries that are linked
  directly into it, and helper executables installed as part of that runtime.
  `src/Materials/Engine/` owns the Rust/Hayagriva citation engine, and
  `src/Integrations/Codex/Bridge/` owns the installed Codex AppServer bridge.
- `services/` contains independently deployed applications. The Hodarium Go
  authority/relay, Web-Accessible ATHENA broker, and delegation transmitter each
  retain their own application entrypoints and deployment resources. The
  native Hodarium client under `src/ATHENA/Hodarium/` is a separate component.
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

This phase is a **source-path reorganization**, not a redesign of source types,
protocols, XML v3, ownership or storage. The historical `src/ATHENA/Data/`,
`src/Subsystems/Qt/`, `src/System/`, and `src/Kernel/` boundaries still require
separate semantic and dependency analysis before relocating their code.

## Verification boundary

The November 3 reorganization was performed without running a compiler, Cargo,
Go, npm, CMake configuration/build, or any runtime executable, in accordance
with the power-conservation requirement. Static source and path checks do not
establish that a new build has succeeded. Existing build directories were not
regenerated or deployed; a future deliberate build must reconfigure its CMake
source graph for the new locations.
