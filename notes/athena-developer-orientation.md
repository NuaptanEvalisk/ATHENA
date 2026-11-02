# ATHENA: Orientation For A New Development Session

Updated 2026-10-20. This is project background, not a task assignment.
The user will supply the bug or feature to work on.

The production Notes Vault uses native XML v3, and Hodarium XML v3 is deployed
and converged in production (user-confirmed 2026-10-20). The original migration
is complete; do not repeat it or pursue the withdrawn old-DAG bridge. The
current format and implementation map are in `notes/athena-xml-v3.md`.
Older implementation records in Git are not current tasks.

## What ATHENA Is

ATHENA means **Advanced Typesetting and Hypertext Environment for Notes and
Archives**. It is a mathematics-centered knowledge workspace derived from GNU
TeXmacs, with structured WYSIWYG editing and high-quality mathematical typesetting.
It is not a conservative TeXmacs distribution or a drop-in upstream replacement.
Many inherited features have deliberately been removed or replaced.

The user uses it for real mathematical notes and research. Important concepts:

- A **document** is a structured tree, not a string containing LaTeX. Paragraphs,
  equations, tables, labels and semantic environments are nodes in that tree.
- A **buffer** is an open document with live editor state. Buffers can exist
  without a vault, and an unsaved buffer need not have a filesystem path.
- A **vault** is a managed collection of documents, settings and databases.
- **Namespaces** organize knowledge and can expose generated document pages,
  such as `tmfs://ns/Universe`. Such a page is not necessarily a disk file.
- **Enunciations** are unified `enunciation` nodes with a `kind` property for
  definitions, theorems, proofs and similar environments. Node UUIDs, not
  generated anchors, supply their persistent source identity.
- **Wikilinks** navigate to document content; **transclusions** embed referenced
  content. Preview/inserter UI uses the document rendering machinery too.
- **Artifacts** are indexed knowledge objects. Radioactive links use that index
  to connect document terms to artifacts, including disambiguation when needed.
- **Materials** support reference management and citations.
- **AVD** (`.avd`) is an ATHENA Compound Virtual Document: namespace members in
  sorter order, one continuous editable scrolling surface, per-file environments
  and shared counters. It is not a sequence of unrelated editor windows.
- **Hodarium** is ATHENA's multi-device synchronization system. Its authority
  server and transport Relays are separate services. See the current subsystem
  documentation and `notes/athena-hodarium.tex`; do not infer its implementation
  from earlier Groupoid discussions.
- AI completion, search/indexing, artifactization and remote delegation are
  separate application services, not interchangeable names for one subsystem.

Artifact databases contain persistent identities, relationships, model decisions
and embeddings. Do not delete them as a repair strategy. Node location indexes,
by contrast, are rebuildable caches: the source UUIDs live in documents. Use
Vaultfile configuration for actual database locations; do not assume historical
root-level filenames are still canonical.

## Local Environment

Repository: `/home/felix/data/Software/TeXmacs/texmacs`.

| Location | Meaning |
| --- | --- |
| `ATHENA/` | Runtime resource tree: styles, Scheme, fonts, data and tools |
| `ATHENA/bin/ATHENA.bin` | Installed development executable used by the user |
| `build_qt6/` | Existing configured normal Debug build |
| `build_qt6/src/ATHENA.bin` | Build output, not automatically the running binary |
| `/home/felix/.ATHENA` | User profile; `ATHENA_HOME_PATH` overrides it |
| `/home/felix/data/Notes` | Production vault, not a scratch test fixture |

Inspect the current branch, `git status` and recent history when starting.
Other ChatGPT sessions may have changed the repository since this note was
written; this orientation deliberately does not pin development to a recorded
HEAD.

There are intentional untracked assets, including `.codex/`, large models under
`ATHENA/tools/` and `misc/embedding/`. Do not
clean or stage them as part of routine fixes. Preserve unrelated changes.

## Code Map

Paths in this note are relative to the repository unless absolute.

| Area | Entry Points |
| --- | --- |
| Startup and CLI modes | `src/ATHENA/ATHENA/athena.cpp` |
| Buffer lifecycle, vaults, namespaces, artifacts | `src/ATHENA/Data/` |
| Application coordination and actors | `src/ATHENA/Server/`, `src/ATHENA/buffer_actor.hpp` |
| Editing, selection, navigation, undo | `src/Edit/`, especially `src/Edit/Editor/` |
| Style/macro evaluation | `src/Typeset/Env/` |
| Inline content and mathematical construction | `src/Typeset/Concat/` |
| Incremental typesetting and layout | `src/Typeset/Bridge/`, `src/Typeset/Boxes/` |
| Font selection, shaping and ownership | `src/Graphics/Fonts/`, `src/Subsystems/Freetype/` |
| Rendering | `src/Graphics/Renderer/`, `src/Subsystems/Qt/qt_renderer.cpp` |
| Native UI and render service | `src/Subsystems/Qt/` |
| Native document codec and legacy import | `src/Data/Convert/Xml/` |
| Scheme runtime and generated bindings | `src/Scheme/` |
| Styles and declarative resources | `ATHENA/packages/`, `ATHENA/misc/` |
| AUDM resolution and resource adapters | `src/ATHENA/Interop/`, `src/ATHENA/Data/interop_*.cpp` |
| AUDMAP transport and new plugins | `src/Subsystems/AUDMAP/`, `src/Subsystems/Plugins/`, `clients/` |
| Tests | `tests/`, organized by subsystem |

Read the relevant implementation before attributing behavior to it. Historical
TeXmacs names remain in paths, types and symbols; a name alone does not establish
current behavior or compatibility requirements.

## Thread Ownership Is Part Of Correctness

The main/Qt thread owns widgets, windows and the GUI buffer registry. Each live
buffer has a serial **BufferActor** owning its mutable document, editor, cursor,
selection, undo, typesetting state and boxes. Different buffers may compute
concurrently. Buffer-bound Scheme executes in the corresponding actor context.

Consequences for ordinary bug fixes:

- An actor must not construct/manipulate Qt widgets or traverse the GUI registry.
- Moving a whole command to Main is not a solution if it then reads or mutates
  actor-owned editor state.
- Preserve the originating actor/view identity across asynchronous work; the
  currently focused document can change before completion.
- Use existing command/effect, payload and lifetime mechanisms. Do not capture
  live editor/tree/Scheme/font pointers in arbitrary cross-thread callbacks.
- Background work needs explicit ownership of detached data, not merely a
  convenient pointer into a live document or a global lock around it.
- Ownership assertions are evidence of a wrong boundary. Do not disable them
  just to make an operation continue.

Fonts also have owner-scoped domains. FreeType faces, metrics, glyph caches and
native font handles are not freely shareable between threads. Handles can be
non-owning, so lifetime matters independently of thread affinity.

Actors produce recorded rendering commands and image resources. RenderService
replays them; Qt presents completed frames. Live document boxes and font graphs
are not handed to the GUI for traversal.

References: `notes/font-domain-ownership.md`,
`notes/actor-runtime-architecture.md`, `src/ATHENA/actor_transport.hpp`,
`src/ATHENA/actor_ui_bridge.hpp`, `src/ATHENA/buffer_state.hpp`, and
`src/Subsystems/Qt/QTMRenderService.cpp`. The architecture notes describe the
current ownership model; verify the implementation when exact details matter.

## Text, Documents And Rendering

Ordinary runtime text is **UTF-8**, retained in the existing native string/tree
containers. Internal text positions are **byte offsets**. Editing boundaries use
ICU grapheme segmentation; parsing and font operations can use scalar boundaries.
Qt UTF-16 positions and Guile character indices require explicit conversion.
Do not use byte count, character count and visual cursor position interchangeably.

Native `.ath` persistence is versioned XML. The codec preserves structured tags,
argument order, empty values and explicit binary payloads. Legacy imports are
separate boundaries; runtime text must not guess Cork encoding or interpret
literal `<...>` strings as old font tokens. Non-Unicode symbol identities use
`named-symbol`, not the node for unfinished symbol input.

The current native source format is **XML v3** with dedicated source UUIDs and
typed named properties on both atomic and compound nodes; it uses a native
mathematical AST rather than legacy bracket/script wrappers. Paragraphs can
be atomic and still have identity. Source identities must survive
normalization, editing and undo; copying as new objects is distinct from
copying a snapshot. Full storage equality and content/model-input equality
are different operations. Do not round-trip source edits through
metadata-losing Scheme strees. See `notes/athena-xml-v3.md` and
`src/Data/Tree/math_tree_v3.hpp` for the current math structure.

Wikilinks and transclusions resolve source UUIDs through native location services
and tmfs. Do not restore file/anchor hints or generated identity-anchor production.
Artifact identities are bound to source nodes, not guessed anew on each edit.
Continuous Maintenance normalizes legacy enunciations; new insertion commands
must produce the modern model directly. Read `notes/node-model-handoff.md`
for source identity and edit contracts.

Use the shared document IO/transaction interfaces rather than writing a new
parser or directly calling a legacy writer. Do not run format migration on the
production vault while investigating an unrelated bug. The current format
reference is `notes/athena-xml-v3.md`; older storage notes in
`src/Data/Convert/Xml/README.md` describe previous XML versions.

Document structure, macro expansion, layout and final painting are different
representations. For a visual failure, follow the relevant transitions:

```text
source tree -> evaluated/expanded content -> bridge/lazy structures
            -> line items and boxes -> renderer commands -> screen/PDF
```

A correct source tree does not establish correct boxes; correct glyph metrics
do not establish correct raster scaling. Layout geometry, ink bounds, baselines,
selection/caret geometry, zoom and device pixel ratio are distinct quantities.
Font selection can involve composite logical fonts as well as physical faces.
Do not assume every logical font implements a single physical shaping operation.

Style macros, JSON maps and theme resources are sources of truth. Fix their
definitions when those are wrong, rather than adding special-case C++ overrides.
Resource changes may also require invalidating the relevant generated cache;
that does not justify deleting unrelated user data or artifact indexes.

## Native Interfaces And Plugins

The project is moving behavior from Scheme to native C++ plus declarative data.
Application Scheme sources have been removed. Vendored Guile, embedded Scheme
expressions and style definitions remain; new business logic belongs in C++.

Native Scheme interfaces are declared in XML under `src/Scheme/Glue/`. CMake's
`athena_glue` preprocessor generates C++ and Scheme in the build directory.
Read that directory's README before changing bindings. No manual per-procedure
glue, generated-file edits, generator function-name exceptions, or revived
`build-glue.scm`. Implement functionality and ownership handling in normal C++.

Old TeXmacs plugins were removed. Current ATHENA plugins are subprocess clients
of **AUDMAP**, the protocol exposing **AUDM** resource resolution and operations.
Resolvers/accessors separate resource semantics from generic orchestration and
authorization UI. There are fixed worker pools, connection authorization and
ticket-local handles. Plugin access must preserve actor and filesystem ownership
boundaries; it is not arbitrary Scheme execution inside the editor.

References: `src/ATHENA/Interop/README.md`,
`src/Subsystems/Plugins/README.md`, and `clients/README.md`.

## Build, Test And Deploy

Follow `AGENTS.md`. For the existing Linux Debug configuration:

```sh
cmake --build build_qt6 --target ATHENA.bin -j16
python3 tools/install-local-executable.py build_qt6/src/ATHENA.bin ATHENA/bin/ATHENA.bin
```

Use an explicit application target, not the default ALL target. Do not deploy
after a failed or unfinished build. The installer retains retired executables
by ELF build ID; do not substitute a manual install/copy/rename sequence.

Generated interfaces are built by `athena_glue` as an application dependency.
Use `tools/deploy-local-runtime.sh build_qt6` when the private Guile runtime also
changes. There is no longer an application Scheme bytecode target or payload.

Tests are not a default deliverable. Clear, straightforward changes do not need
tests; use focused evidence only for concrete uncertainty. Do not run broad
suites, incidental test targets or prolonged GUI automation.

An already running process retains its old executable. Deployment is not hot
replacement. The macOS VM uses `-j15`; a Linux build does not update an IPA.
See `notes/ipados-build-handoff.md` for the existing platform workflow.

## Current development context

Updated: 2026-11-02.

- Real-window IME validation is **complete, confirmed by the user**. The
  outstanding-validation sentence in historical commit `abdd53b8e` describes
  that earlier checkpoint, not current work. Do not reopen it as a pending task.
- Native conversion and the bounded DRD replacement are complete. `progs/convert`,
  `progs/kernel` and `progs/prog` have been removed. Program rendering stays
  in ATHENA; generic editing is C++, with KSyntaxHighlighting lexical context
  and no language-specific smart indentation.
- `progs` has been removed entirely. Application and new-buffer initialization
  are native; command UI metadata lives in `misc/command-properties.json`.
  Application Scheme bytecode compilation and packaging are removed. Structural
  pattern selection, editor utilities, preferences, interactive dialogs and
  output ports are native. Unconsumed Scheme list/hash/logic/patch infrastructure
  was removed rather than reproduced in C++. Guile and its native `(athena
  runtime)` module still exist; this is not yet removal of the Scheme runtime.

The production Vault is already XML v3 and the Hodarium NAS replica has
converged and is in use. Do not treat the previous cutover as pending.
Recent 2026-10-19/20 commits repaired native mathematical editing, including
scripts, delimiter deletion, display formula focus, equation grids and row
editing. For a new issue, inspect the current tree and relevant tests instead
of continuing an older handoff. Current format and runtime entry points are in
`notes/athena-xml-v3.md`.

No project note grants permission to migrate production data, reset
Hodarium credentials, redeploy a server, commit, or push code. Perform
isolated, task-specific verification only.

## Debugging And Working With The User

Read `AGENTS.md`; it is authoritative and more detailed than this summary.

- Work from the user's current symptom and evidence, not old conversation tasks.
- Find the first incorrect transition before patching. Missing diagnostic output
  can mean the instrumented path was never reached.
- Separate an established cause from a hypothesis, and focused tests from actual
  end-to-end acceptance. Remove temporary instrumentation when finished.
- For slowness, measure the active hot path rather than assuming the bottleneck.
- The user often offers a running process for GDB inspection. Confirm its PID
  and `/proc/PID/exe`: backup installations, watchdogs and old headless tests can
  coexist. Never attach to a historical PID without checking.
- For GDB, disable debuginfod prompts; Guile/GC SIGPWR and SIGXCPU should normally
  pass without stopping. Optimized Debug frames can lose locals. Detach promptly
  after collecting evidence, without killing the user's inferior.
- Watchdog stall reports are under `/home/felix/.ATHENA/system/hangs/`.
- Use temporary profiles/vaults or sample copies for destructive testing. Do not
  edit production Notes, rebuild its databases, reset credentials or change
  remote backend deployments without authorization.
- Do not add or run tests for straightforward changes. Use focused evidence
  for concrete uncertainty and accept the user's GUI validation. Poll long
  builds sensibly rather than busy-waiting. Do not let a test-harness issue
  consume the entire task.
- If three successive fixes for the same bug yield no observable improvement,
  follow the escalation rule in `AGENTS.md` rather than continuing to guess.

No system package installation or private RPM extraction without user approval.
Use established libraries when appropriate rather than approximate replacements.
Restrict filesystem searches to the repo or the specific supplied directory;
never recursively search `/` or `/home/felix`.

Before a requested commit, read recent full commit messages. Use
`type: imperative summary`, followed by concrete functional bullets. Stage only
the intended files; do not include models, generated build output or someone
else's changes. This note does not request a commit, audit, migration, deployment
or particular bug fix. Those decisions belong to the user's current request.
