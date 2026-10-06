# Replace namespace libtcc sorters with Luau

Date: 2026-10-03. This is an implementation assignment for a new session.
Historical assignment: runtime and explicit-mapping migration subsequently landed
in ab87d88d5. IMPORTANT subsequent user clarification: comparator zero means no
precedence constraint and is NOT transitive. The strict-weak-order requirement
for arbitrary comparators below is superseded. Only key-based sorting has that
contract. Preserve raw constraints through nested products and topologically
order them only for presentation; do not turn a linear extension into semantic
ranks. Consult current code rather than implementing this historical plan again.

## User decisions and non-negotiable boundaries

- Replace libtcc completely with the official Luau compiler and bytecode VM.
  Do not enable native CodeGen/JIT or retain TCC as a fallback.
- No automatic C/C++ to Luau translation. Do not parse C with regular
  expressions, translate a convenient subset, or use an LLM inside migration.
- No nIKIS/Notes-specific sorter recognition, built-in replacements, filename
  heuristics, or course/book special cases in ATHENA.
- The offline migration CLI MUST require an explicit JSON ARRAY mapping old
  C sorter paths to user-supplied Luau sorter paths. A user supplies the actual
  replacement implementations. Missing coverage is an actionable error, never
  permission to invent a replacement or silently use trivial sorting.
- Generated sub-product sorters should become structural composition plans,
  not copied/concatenated Luau source files. Preserve field projection.
- Do not write-test or migrate production `/home/felix/data/Notes`. Use isolated
  fixtures under `/home/felix/tmp`. Production migration is a separate user step.
- Do not change unrelated work. Start with git status and recent history.

## ATHENA in brief

Repository: `/home/felix/data/Software/TeXmacs/texmacs`, normally branch master.
ATHENA is a mathematics-oriented knowledge workspace derived from GNU TeXmacs,
not a compatibility distribution of TeXmacs. It has structured document editing,
vaults, namespaces, artifacts, linked/transcluded content and compound virtual
documents (AVD). It is migrating behavior from Scheme to native C++.

Native document text is UTF-8 and `.ath` persistence is XML v2 with optional
node UUIDs and typed properties. Modern enunciations have one `enunciation` tag
plus properties. Source UUIDs, not generated identity anchors, underlie modern
references. Do not restart historical document migrations during this task.

Namespaces select files using filename-stem templates. Literal text is NOT a
capture. `%s`, `%w`, `%N`, `%R` etc. produce typed capture fields. Namespaces
have parent relations, optional sorting and other settings. Member order is
consumed by navigation, generated pages, AVD and exports, so sorting is not just
a cosmetic manager-dialog feature.

Qt Main owns widgets and the GUI registry. Each BufferActor owns its live
document/editor/typesetter. Namespace ontology has immutable published snapshots
and worker-owned database connections. Keep VM state owner-affine; no global
mutable VM shared by actors or worker threads. AUDMAP is external interop, not
the internal sorting mechanism.

Read AGENTS.md first. `notes/athena-developer-orientation.md` is useful background
but contains historical descriptions (notably anchors); current code and this
assignment take precedence. Read `notes/namespace-subproduct-ownership.md` for
the ownership guarantees that must survive replacement of the TCC implementation.

## Build, deployment and scope discipline

The normal build command is EXACTLY:

```sh
cmake --build build_qt6 --target ATHENA.bin -j20
```

Do NOT run an unqualified `cmake --build build_qt6`, default ALL, `-j8`, a full
test suite, or build `utf8_editor_test` as a routine step. Previous careless
builds damaged Guile bytecode deployment. Do not delete or rebuild unrelated
bytecode caches. Finish substantial blocks before limited relevant validation;
do not spend the task building elaborate test harnesses.

After a successful final build, deploy automatically; do not require the user
to ask again. Run these separately and proceed only if the previous succeeds:

```sh
install -m755 build_qt6/src/ATHENA.bin ATHENA/bin/ATHENA.bin.new
cmp build_qt6/src/ATHENA.bin ATHENA/bin/ATHENA.bin.new
mv -f ATHENA/bin/ATHENA.bin.new ATHENA/bin/ATHENA.bin
```

Do not install system packages without explicit user approval. If a needed
system dependency is missing, report the exact package/command and wait; do
not extract packages into private prefixes to bypass this. For Luau, investigate
the official maintained implementation and fit its pinned source/dependency
integration into the repository's established approach. Document its MIT
license and preserve required notices. Do not implement a substitute VM.

Commit coherent stages, following AGENTS.md: inspect full recent commit bodies,
use `type: imperative summary`, then concrete bullet points. New C++ files need
the project's MODULE/DESCRIPTION/COPYRIGHT and GPLv3+ header (2026 Nuaptan Felix
Evalisk). Do not hand-write Scheme glue: bindings belong in the XML generator.

## Current implementation: read these files

- `src/ATHENA/Data/namespaces_sorter.cpp`: C ABI, thread-local compiled cache,
  borrowed field storage, sorting, product/restricted source generation.
- `src/ATHENA/Data/namespaces_private.hpp` and `namespaces.hpp`: internal/public
  interfaces and projection types.
- `src/ATHENA/Data/namespaces_template.cpp`: typed matching, Roman parsing,
  `template_derivation_mapping` and sub-product template inference.
- `src/ATHENA/Data/namespaces_members.cpp`: actual sorter consumers.
- `src/ATHENA/Data/namespace_ontology.cpp`: cached typed captures and snapshots.
- `src/ATHENA/Data/namespaces_db.cpp`: definitions and persistence. Inspect the
  actual schema/parent relation representation before designing a schema change.
- `src/Subsystems/Qt/QTMNamespaceManager.cpp`: sorter selection/source display,
  product wizard and restricted-sorter generation (around lines 1820-1950).
- `src/ATHENA/ATHENA/athena.cpp`: help and early offline CLI dispatch.
- `src/ATHENA/Data/vault_database_layout.cpp`, existing vault upgrade code and
  `vault_directory_lease`: examples of pinned context, exclusive migration,
  journaling and recovery. Reuse appropriate infrastructure rather than creating
  a competing vault lock protocol.
- `CMakeLists.txt`: TCC discovery around line 324, include/link entries later.
  Search the repo for other libtcc dependencies, docs, packaging and tests.

Current sorter input has text, field type, signed integer and Roman value.
`sort_namespace_members` prepares stable fields once and stable-sorts indices,
not mutable records. Compiled handles own their generation and are retained
through an operation; cache replacement must not invalidate active handles.

Current generated product works as follows:

```text
Parent A: FA %s %R
Parent B: %w Lecture Notes %R
Child:    FA Lecture Notes %R

Child capture [IV]
  -> A fields [Lecture Notes, IV]
  -> B fields [FA, IV]
```

It copies each parent's C source, renames its entry point, generates projection
code, writes `.athena/ns-sorters/product-....c` and compiles it. A restricted
sorter projects to one parent and delegates. Parent-source changes do NOT update
already-generated copies. Generated projection currently uses 512-byte buffers;
do not inherit this truncation in the replacement.

The current product merges results with `c1 < 0 || c2 < 0`, then the analogous
positive test. Opposing parent results make both directions compare less-than.
Do NOT port that bug into a std::sort comparator.

## Runtime contract to implement

Use `.luau` as the canonical script suffix. Use only official compiler + VM;
no execution-memory relocation, native code generation or compiler subprocess.
Suggested versioned module interface (final names should be documented):

```lua
return {
    version = 1,
    key = function(fields)
        return { fields[2].roman }
    end,
}
```

- Exactly one of `key(fields)` and `compare(a, b)`; key is preferred.
- `key` runs once per member; C++ performs lexicographic stable sorting of the
  resulting typed keys. Define supported types, heterogeneous-key errors,
  shorter-key ordering and tie behavior, rather than relying on Lua coercion.
- `compare` returns -1, 0 or 1 and must obey strict weak ordering. Zero is
  equivalence, NOT a general "incomparable" result. Do not claim to prove an
  arbitrary comparator correct with a few sample comparisons.
- Input tables are read-only, strings UTF-8, fields one-based and explicitly
  typed. Expose only the defined fields and a small documented pure helper API.
- Preserve exact signed 64-bit integer ordering. Luau numbers cannot exactly
  represent every int64. Provide an opaque exact-integer key/helper representation
  or equivalent lossless API; never silently round to double. Roman values can
  use numbers within the existing validated range.
- Specify bytewise string ordering unless an explicit alternative is requested;
  do not introduce locale-dependent collation accidentally.
- Per-thread VM with isolated script environments, read-only libraries, no
  arbitrary filesystem/network/process APIs. Apply memory allocation budgets
  and interruption/time budgets to module initialization and calls. Reject NaN,
  invalid return values and script errors with namespace/path diagnostics through
  ATHENA's normal error channel. Do not publish a half-sorted result.
- Only host-compiled source; do not accept untrusted persisted bytecode as input.
- Cache immutable compiled generations and derived sorting results against
  script revision, namespace/template/member revision and composition dependencies.
  Avoid hashing/compiling/projecting each comparison or introducing an O(n^2)
  path for the ordinary key-based case. Do not promise speedups without evidence.

Official references (verify current integration APIs before implementation):
https://github.com/luau-lang/luau
https://luau.org/sandbox/
https://luau.org/performance/

## New composition representation

Store versioned JSON composition data, not executable script copies. Data
includes parent sorter identities/references, field projections and composition
mode. Bind parent references to stable identity rather than mutable display name
alone; invalidate dependents when a referenced definition changes. Detect
dependency cycles separately from contradictory ordering constraints.

Restricted composition delegates after projecting fields. Product composition
must have explicit semantics:

- Lexicographic: declared parent priority, compare secondary only on a tie.
- Constraint union: preserve strict precedence constraints from both parents,
  use stable topological ordering, and report cycles with useful witnesses.
  Never feed pairwise OR into std::stable_sort. Arbitrary comparator constraints
  can require costly enumeration; do not conceal that cost. Key-based ordered
  groups allow a better representation than all pairwise edges.

The prior design proposing these two modes was accepted in general. No evidence
establishes that every historical product is intended to be lexicographic or
that parent list order expresses priority. Make product creation/migration's
choice explicit; do not silently choose a semantic change. If necessary ask the
user a focused question before setting a historical default.

## Offline migration: explicit mapping, no translation

Proposed CLI (implement and document concrete names):

```sh
ATHENA/bin/ATHENA.bin --upgrade-vault-sorters /path/to/vault \
  --sorter-map '[{"from":"dependencies/old.c","to":"dependencies/new.luau"}]' \
  --dry-run
```

`--sorter-map` is a required JSON ARRAY, not an inferred directory mapping or
object keyed by course names. An optional `--sorter-map-file` may read exactly
the same array schema for large mappings; require one unambiguous input source.
Paths in the array are vault-relative unless explicitly absolute. Normalize
aliases consistently (existing bindings use both); reject conflicting mappings.
Report unused entries rather than silently ignoring possible mistakes.

The replacement Luau files must already exist and be supplied by the user.
Read/validate them, do not overwrite or invent them. The tool never executes
old C code and must not retain libtcc just to compare old/new results.

Generated historical C files require particular care:

- A direct explicit C -> Luau mapping for a generated file is valid and wins.
- Regenerating a structural composition from persisted namespace parent/template
  data is NOT C translation, but requires unambiguous provenance and an explicit
  regeneration choice, since copied parent code may differ from today's parents.
- Do not assume two namespace parents prove a sorter file was generated or
  unmodified. A filename/comment is not sufficient proof of provenance.
- When provenance is insufficient, require an explicit mapping for that C file,
  or an explicit user-authorized regeneration selection identifying namespace
  and composition mode. Do not infer/parse its C algorithm.
- A CLI may offer a separate explicit regeneration option/manifest for these
  namespaces. This does not replace the required C -> Luau mapping array for
  the executable rules. Explain both sets of unresolved items in dry-run output.

This intentionally favors a clear manual mapping over clever migration. The
user explicitly rejected hardcoded known-sorter conversions.

Transaction requirements:

1. Run headless before normal GUI/vault workers start; acquire exclusive vault
   ownership. Resolve the namespace database through Vaultfile.json, NOT a
   hardcoded root ns.sqlite. It may be `.athena/namespaces.sqlite`.
2. Inventory bindings, aliases, required replacements and dependencies. Dry-run
   makes no production changes. Validate scripts, field contracts, graph cycles
   and representative fixture/member results without running legacy C.
3. Stage any newly authored composition descriptors, pin source/config revisions
   and back up the namespace database using a consistent SQLite method.
4. Publish bindings/format metadata transactionally with a recovery journal for
   filesystem + database changes; include rollback/recovery behavior and external
   modification checks. Do not leave half-migrated namespace definitions.
5. Preserve namespace UUIDs, parents, templates, styles, homepage and all unrelated
   fields. Do not rebuild artifacts, embeddings or document content. Leave old
   C files in place; migration is not a cleanup/deletion operation.
6. Report counts, per-binding decisions and unresolved mappings with nonzero exit
   on failure. Repeated execution with already-applied mappings should be a
   documented no-op, not an error or an attempt to run C.

Unmigrated vaults in the new runtime need a clear unsupported-C/migration-required
diagnostic. Do not silently interpret C as Luau, launch a compiler, or use a
trivial sorter while pretending the requested order was applied.

## Production observations: context, NOT special-case requirements

Notes is the user's real nIKIS vault. Its namespace DB currently uses canonical
location `.athena/namespaces.sqlite`. Always read its configured location.
Earlier inventory found 67 distinct physical sorter files: 5 handwritten and
62 generated. Recheck if needed; this is not a schema constant.

At user request, ONLY these production source files were just manually fixed:

- `dependencies/roman-sorter.c`: accepts one Roman field or string + Roman.
- `dependencies/reading-sorter.c`: accepts positive-int + string, or word +
  positive-int + string (the latter retains old same-group comparison behavior).

No generated products were regenerated. They still contain historical copies.
This is expected until explicit migration, not permission to mutate them now.
The Reading cross-group equality behavior is not generally a strict weak order;
do not advertise a direct semantic preservation claim for it in the new API.
Replacement scripts and desired grouping/order are supplied explicitly by users.

## Delivery stages and bounded verification

1. Inspect current consumers and establish/document the typed Luau ABI and
   dependency integration. Remove TCC after replacements cover its callers.
2. Implement runtime, caching/ownership, diagnostics and manager script UI.
3. Replace product/restricted generation with declarative composition and wire
   all member consumers, including AVD, to the same service.
4. Implement explicit-mapping offline migration, help and user-facing examples
   that use generic fictional filenames rather than a Notes hardcoded registry.
5. Validate small isolated cases: typed/Roman/int64 keys, direct compare, script
   error/budget, retained generation, restricted projection, composition conflict,
   missing mapping, dry-run and interrupted/repeated migration. Reuse existing
   focused tests where practical; do not launch full tests or build new elaborate
   infrastructure merely to test obvious control flow.
6. Build ATHENA.bin with -j20, deploy, commit coherent changes and report what is
   implemented plus the migration command the user must still run themselves.

Completion means CMake no longer discovers/links TCC, no runtime C sorter
execution remains, normal and composed sorting work through one service, and
the migration tool requires explicit replacements and cannot quietly change
production data. Merely adding Luau beside TCC is not completion.
