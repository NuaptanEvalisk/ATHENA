# ATHENA native source layout

This source tree is organized by responsibility rather than by TeXmacs-era
directory origin. The top-level `ATHENA/` directory at the repository root
remains the shipped runtime resource tree, **not** a C++ source directory.

| Directory | Owner |
| --- | --- |
| `App/` | Program entrypoint, application lifecycle, buffer/view management, actor ownership, native command dispatch, windows and watchdog |
| `Core/` | Generic tree, string, metadata and container primitives, language support and utilities |
| `Document/` | Document representation, node contracts, conversion, persistence and revision/edit history |
| `Domains/` | Vault, Artifacts, Namespaces, Knowledge, References and Websites |
| `Editor/` | Editor engine, editing commands, text modifications and selection |
| `Infrastructure/` | Filesystem, links, virtual filesystem and runtime services |
| `Integrations/` | External C/C++ library adapters and the installed Codex bridge |
| `Interop/` | AUDM resolution engine and application resource adapters |
| `Materials/` | Citation/materials domain model and the Rust/Hayagriva engine |
| `Math/` | Mathematical geometry, handwriting and specialized tools |
| `Platform/` | Unix, Windows, macOS and iPadOS adaptations |
| `Rendering/` | Graphical rendering, images, fonts, PDF backends and VTK |
| `Services/` | AUDMAP transport, RAG, MCP, Google integration and plugins |
| `Sync/` | Native Hodarium client, logical synchronization, and headless node |
| `Typesetting/` | Typesetter, page layout, boxes, environments and style evaluation |
| `UI/` | Abstract widgets and the Qt-specific presentation layer |

Qt-specific implementation is grouped under `UI/Qt/` by shell, commands,
document, editor, vault, dialogs, knowledge, materials, integration and toolkit
responsibilities. Keep a new file with its actual owner; do not introduce
another general-purpose `Data`, `System` or `Subsystems` bucket.

The `App/Runtime` actor boundary is significant: an actor-owned mutable buffer,
cursor or editor cannot be accessed from an arbitrary Qt/UI thread. The source
move does not change BufferActor ownership, XML v3 storage, native contracts or
the deployed application. See `notes/athena-developer-orientation.md` and
`notes/source-layout.md` for runtime details.

This reorganization preserves the inherited `athena_body` target and its
transitional per-directory include paths. The source files have been matched
statically to the preceding CMake compilation inventory, but **no configure,
build, tests or deployment were run**. Separate targets with enforced dependency
directions require a later, explicit compilation/verification step.
