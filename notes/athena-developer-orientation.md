# ATHENA: Orientation For A New Development Session

Updated 2026-10-10. This is project background, not a task assignment.
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

The current node model uses XML v2 and optional metadata on both atomic and
compound nodes: dedicated UUIDs and typed named properties, not wrapper nodes.
Paragraphs can be atomic and still have identity. Source identities must survive
normalization, editing and undo; copying as new objects is distinct from copying
a snapshot. Full storage equality and content/model-input equality are different
operations. Do not round-trip source edits through metadata-losing Scheme strees.

Wikilinks and transclusions resolve source UUIDs through native location services
and tmfs. Do not restore file/anchor hints or generated identity-anchor production.
Artifact identities are bound to source nodes, not guessed anew on each edit.
Continuous Maintenance normalizes legacy enunciations; new insertion commands
must produce the modern model directly. Read current node-model documentation
for the detailed edit, migration and identity contracts, not as a pending todo.

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

After a successful normal Debug build, deploy automatically unless the user
explicitly requests build-only or no deployment. Install to a temporary sibling,
verify, then rename atomically. Do not overwrite an active executable in place:

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

These commands are for Linux. The macOS VM has 16 vCPUs: use **`-j15` there**,
never `-j20`. iPadOS already has a functioning native app build; do not restart
that port from scratch. Read `notes/ipados-build-handoff.md` and verify current
VM/build scripts before iPad work. iPad ships precompiled Guile bytecode, without
JIT or runtime source recompilation. A Linux build does not update an IPA.

## Current Handoff: Editing Performance And Live Spelling

The most recent task is complete, built and deployed on Linux. No new task is
being assigned by this document. The user will report acceptance or the next
problem. Do not automatically rerun tests, benchmark production Notes, or resume
Hodarium's deferred joint validation.

Recent commits, in chronological order:

- `23619630b`: restore image background removal in native menus.
- `786cf3d87`: fix Unicode completion and avoid per-keystroke full-tree scans.
- `e408c9a31`: make live spelling incremental and retain underline presentation.

The reported slow document was
`/home/felix/data/Notes/Notes Root/Splash Think/Splash Think 2026.ath`.
Investigation used an isolated copy, not edits to that production file. It has
about 798 top-level body children, many inline/display formulas and enunciations.
An isolated runtime trace caught completion splitting a multibyte UTF-8 sequence;
the old completion path also traversed the full tree repeatedly. Native ink
region discovery was another unnecessary whole-tree traversal on text edits.

The completion fix uses ICU word boundaries, per-source-block word-frequency
caches and prefix lookup over the aggregate index. Source edit notifications
invalidate affected blocks. Ordinary text edits no longer invalidate ink region
discovery unless they affect graphics structure. A first cold completion index
build can still cost more than subsequent keystrokes; do not confuse cold and
steady-state measurements.

### Live Spelling: Exact Change And Ownership

Previously `post_notify()` cleared alternative selections, `THE_TREE` reset the
spell scanner, and the next scan cleared `spell-live` before rebuilding it.
Even viewport movement could restart an unfinished scan. The old implementation
was time-sliced, not edit-incremental. This explained disappearing/reappearing
underlines after typing.

The new implementation is actor-owned and lives in:

- `src/Data/Tree/tree_spell.{hpp,cpp}`: `incremental_spell_cache` wraps the
  existing bounded subtree scanner. It stores pending jobs by top-level body
  child and committed error ranges relative to the body.
- `src/Edit/Modify/edit_modify.cpp`: `edit_announce` passes the actual edit to
  `live_spelling_announce` before mutation; `post_notify` republishes rebased
  ranges immediately after the generic alternative-selection cancellation.
- `src/Edit/Interface/edit_live_spell.cpp`: schedules affected-block work and
  publishes completed results. Dictionary/environment invalidation retains old
  marks until replacement results are ready.
- `src/Edit/Interface/edit_interface.{hpp,cpp}` and `src/Edit/editor.hpp`:
  editor integration and removal of unconditional tree-change resets.

Existing marks track insert/remove/split/join/wrap/unwrap edits. Atomic ASSIGN
uses a UTF-8-safe common prefix/suffix to relocate endpoints. A composite subtree
replacement removes marks belonging to the replaced content. Completed block
results replace the previous block results in one publication; partial scans do
not publish an empty intermediate selection. Scrolling reprioritizes pending
work without resetting it. The normal 450 ms typing debounce remains.

Granularity is a **top-level source block**, not a word: editing inside a
top-level enunciation rechecks that enunciation, not the whole document.
Initial scans and dictionary/global-environment changes still require broader
work. Publishing selections still visits the committed mark list; do not claim
every operation is constant-time or that all repainting has been eliminated.

Important implementation trap: TeXmacs `path_less` is a cursor-order relation,
not a general lexicographic prefix comparator. It has special `(0)`/`(1)`
semantics. The cache deliberately uses its own lexicographic comparator for
`lower_bound(path(i))` block ranges. Replacing it with `path_less` caused invalid
map range erasure during development. Also, `MOD_INSERT_NODE` obtains its index
from `last_item(mod->p)`; generic `index(mod)` does not support that operation.

### Verification Already Done

- Normal Linux application build succeeded; `git diff --check` passed.
- Built executable was compared and atomically deployed to
  `ATHENA/bin/ATHENA.bin`. The user's running instance was not restarted and
  therefore may still be using an older executable.
- A three-paragraph isolated fixture verified `spell-live` ranges immediately
  after typing, after the debounce, after splitting, and after undo. Marks never
  became an empty intermediate set; following paragraphs shifted correctly.
- These are range/state checks, not a claim that a human has visually accepted
  every rendering case. No full test suite was run.

Temporary evidence, if still present, is under `/tmp/athena-splash-profile/`:
`spell-init.scm`, `document/spell.ath`, `marks-*`, `spell-check.log`, and
`spell-final-build.log`. Older `spell-gdb.log` captures the comparator failure
that was subsequently fixed; do not mistake it for a current failure.

For a necessary isolated runtime investigation, set `ATHENA_HOME_PATH` to a
temporary profile and `ATHENA_PATH` to the repository's `ATHENA/` resource tree.
Directly launching `build_qt6/src/ATHENA.bin` may additionally require
`LD_LIBRARY_PATH` pointing at `ATHENA/lib` for bundled dependencies. Target the
fixture's actor explicitly (`exec-buffer` in an `exec-global` dispatch) rather
than relying on whichever editor happens to be focused. Temporary profiles may
need copies of user styles such as `unnumbered-sections-generic`; a missing style
is not evidence of the original bug. Do not copy credentials unnecessarily.

Hodarium's final combined multi-device validation was explicitly deferred by the
user. Recent implementation and documentation commits exist, but build success
does not establish production synchronization acceptance. Do not start servers,
pair devices or synchronize Notes merely because a handoff mentions Hodarium.

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
