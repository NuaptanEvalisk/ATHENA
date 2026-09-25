# ATHENA: Orientation For A New Development Session

Updated 2026-09-25. This is project background, not a task assignment.
The user will supply the bug or feature to work on.

The UTF-8/XML migration is complete. Do not treat old reports, migration
checklists or earlier handoff notes as pending assignments. Read current code
and reproduce the current report rather than continuing a historical
investigation.

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
- **Enunciations** include structured definitions, theorems, proofs and similar
  environments. Anchored ranges can be selected for linking and transclusion.
- **Wikilinks** navigate to document content; **transclusions** embed referenced
  content. Preview/inserter UI uses the document rendering machinery too.
- **Artifacts** are indexed knowledge objects. Radioactive links use that index
  to connect document terms to artifacts, including disambiguation when needed.
- **Materials** support reference management and citations.
- AI completion, search/indexing, artifactization and remote delegation are
  separate application services, not interchangeable names for one subsystem.

Vault databases contain persistent identities, relationships, model decisions
and embeddings. They are not disposable caches. Rebuilding them can be expensive
and can involve remote hardware.

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
`ATHENA/tools/`, `misc/embedding/`, and `misc/formula-cleaner-convert/`. Do not
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
| Remaining Scheme, styles and declarations | `ATHENA/progs/`, `ATHENA/packages/`, `ATHENA/misc/` |
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
`src/Subsystems/Qt/QTMRenderService.cpp`. Architecture notes include historical
design material; verify the current implementation when details matter.

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

Use the shared document IO/transaction interfaces rather than writing a new
parser or directly calling a legacy writer. Do not run format migration on the
production vault while investigating an unrelated bug. The detailed reference
is `src/Data/Convert/Xml/README.md`, which documents the current native XML
format, legacy read/import boundary and revision-checked storage transactions.

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
It still uses vendored Guile and many Scheme/style definitions. Do not assume
either that Scheme has disappeared or that new functionality belongs there.

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

These are mandatory local build rules, not suggestions. Unless the user
explicitly requests a different build, use the existing normal Debug
configuration and **exactly this command**, from the repository root:

```sh
cmake --build build_qt6 --target ATHENA.bin -j20
```

- **Always specify `--target ATHENA.bin`. Never run bare
  `cmake --build build_qt6`, the default/ALL target, or unrestricted `make` or
  `ninja`.** Those are not equivalent to building the application: they can
  also rebuild and publish Scheme bytecode and runtime resources. An earlier
  default-target build exposed broken bytecode generation and left ATHENA
  unable to start.
- **Use `-j20`. Do not substitute `-j8`, omit parallelism, or choose another
  value yourself.** The user has explicitly required 20 build jobs. If a
  concrete resource failure prevents that, report it and ask before changing
  the setting.
- **Do not build test targets as part of routine application work.** This
  document does not authorize a test-binary build. If a separate test target
  is necessary, explain why and obtain the user's explicit instruction first.
- **Never run the full test suite without the user's explicit request.**
  This includes unfiltered `ctest`, `make test`, `ninja test`, and running a
  test executable without a specific test filter. A request to fix a bug,
  build, deploy, or verify a change is not permission to run all tests.
- Do not independently reconfigure, clean the build tree, build TSan, rebuild
  vendored Guile, rebuild/publish Scheme bytecode, or run runtime-install/ALL
  targets. Ask first when one of these is actually required. Never delete or
  replace user caches as speculative troubleshooting.
- `ATHENA.bin` is the executable target; a target named `ATHENA` is not a
  substitute. Wait for compilation and linking to finish and check the exit
  status. Never deploy after a failed or unfinished build.

Keep verification proportional to the fix. The user normally performs GUI
acceptance; do not launch broad test suites or prolonged Xvfb sessions on your
own. Any authorized automated run must use an isolated profile and temporary
data, not production Notes.

When deployment is requested, install to a temporary sibling, verify, then
rename atomically. Do not overwrite an active executable in place:

```sh
install -m 755 build_qt6/src/ATHENA.bin ATHENA/bin/ATHENA.bin.new
cmp build_qt6/src/ATHENA.bin ATHENA/bin/ATHENA.bin.new
# Only after successful build and comparison:
mv -f ATHENA/bin/ATHENA.bin.new ATHENA/bin/ATHENA.bin
```

An already running process still uses its old executable. Report deployment
accurately and let the user restart for acceptance; do not imply hot replacement.
Normal Debug is the usual first step. Do not automatically build TSan or run a
large GUI suite for every local fix.

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
- Prefer a small relevant regression test set and user acceptance to excessive
  Xvfb automation or repeated full audits. Poll long builds sensibly rather than
  busy-waiting. Do not let a test-harness issue consume the entire task.
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
