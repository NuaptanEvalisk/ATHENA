# Namespace Sub-product Ownership Audit

Date: 2026-09-05. Source baseline: `55d121e20`.

This is a source audit, not a TSan reproduction. No production Vault was
modified and no namespace implementation was changed during this audit.

## Who Calls TCC

The namespace manager calls the linked `libtcc` API, not an external `tcc`
process and not Guile's Scheme compiler. This build resolves it to
`/usr/local/lib/libtcc.a` with headers in `/usr/local/include`.

For two semi-concrete/concrete parents:

1. `namespace_manager_show` dispatches onto the Qt main thread
   (`src/Subsystems/Qt/QTMNamespaceManager.cpp:2069`).
2. The toolbar action calls `generateSubproducts` on that thread (1032, 1693).
   Template suggestion is C++ template derivation, not C compilation.
3. The modal wizard displays both parent sorters and asks the user to confirm
   compatibility. This is a checkbox, not a proof of comparator compatibility.
4. After acceptance, `athena_namespace_generate_product_sorter` is called
   synchronously (1818). It renames the parent comparator entry points,
   combines their C sources and generates field adapters
   (`src/ATHENA/Data/namespaces_sorter.cpp:388`).
5. It writes `.athena/ns-sorters/product-<parents>-<timestamp>[suffix].c`
   and calls `load_sorter` to check compilation (519, 584).
6. `load_sorter` calls `tcc_new`, `tcc_compile_string`, registers ABI helpers,
   relocates to memory and obtains `athena_ns_compare` (96-186).
7. Namespace definitions and declared parent relations are committed to SQLite
   by `athena_namespace_save`; the ontology service is invalidated, then the
   manager refreshes (manager:1746, 1884; namespaces_db.cpp:619-689).

One abstract parent uses a restricted sorter where needed. Two abstract parents
create abstract definitions without compiling a product sorter. Merely opening
the manager can also compile an existing sorter: `loadNamespace` refreshes its
matched files, reaching `athena_namespace_members -> load_sorter`.

## Findings

### High: Shared Compiler Cache Has No Owner or Execution Lifetime

`namespaces_sorter.cpp:61` is a process-global mutable `std::map`. `load_sorter`
uses `operator[]`, mutates entries and returns a bare function pointer. There
is no owning-thread check, synchronization or retained compiled-generation
handle. On a source timestamp change it deletes the prior TCC state (108)
while existing callers can still retain the old pointer in their `stable_sort`
lambda (`namespaces_members.cpp:135-145`). Concurrent reload and execution can
therefore access freed compiled code; concurrent cache operations are also
unprotected. Cache entries have raw state pointers without final cleanup.

The UI reaches this through manager/explorer/switcher member lists. The same
API is exposed without an owner dispatch through `namespace-info-page`
(`src/Scheme/Scheme/glue.cpp:2412`), including the `tmfs://ns/` handler. An
actor-side include can reach it via `typeset_include -> load_inclusion ->
import_tree -> get_from_server -> tmfs-load`; see `concat_macro.cpp:247`,
`new_buffer.cpp:745`, `web_files.cpp:125`, and `tm-vault-namespaces.scm:30`.
Ordinary `load-buffer` is dispatched to the main thread, so its successful use
alone does not exercise this concurrent path. No particular concurrent crash
was reproduced in this audit. This finding does not assume that libtcc's
internal implementation is itself unsafe for independent compiler states.

### High: Comparator Adaptation Copies and Leaks Strings on the Hot Path

`to_c_field` calls `as_charp` twice per field (`namespaces_sorter.cpp:192,200`).
`as_charp` allocates a fresh NUL-terminated character array, rather than returning
a borrowed view (`src/Kernel/Types/string.cpp:444`). Neither allocation is
released. Destruction of `vector<AthenaNsField>` does not free its raw `text`
pointers. For two records with n captures this leaks 4n character allocations
per comparison. The fallback stem comparator also calls `as_charp` without
freeing the allocations (`namespaces_members.cpp:151`). These are mimalloc/new
allocations requiring `tm_delete_array`, not Guile-GC-owned buffers.

### Medium: Main-Thread Refresh Waits for Background Indexing

After save, `refreshAll -> athena_namespaces_list` requests a current ontology
snapshot. `NamespaceOntologyService::snapshot` waits on a condition variable
without a timeout until the requested generation is published or fails
(`namespace_ontology.cpp:1369`). This blocks the Qt event loop while indexing
runs. SQLite calls can also wait up to five seconds on contention
(`namespaces_db.cpp:151`); TCC compilation itself runs synchronously on the UI.
These are blocking paths, not proof of a permanent deadlock.

### Medium: Immutable Snapshots Are Safe to Read, But Not Zero-copy End to End

The service publishes a retained `shared_ptr<const OntologySnapshot>` under its
mutex, and the worker owns its SQLite connections and native state. However,
readers materialize copied TeXmacs strings/arrays for each namespace and match
(`namespace_ontology.cpp:850-877,1585-1647`). `std_to_tm_string` copies bytes
(120). This avoids sharing mutable TeXmacs containers between owners, but does
not meet the requested shared-storage/no-cross-thread-payload-copy model.

## Boundaries That Are Already Correct

- The manager, wizard, controls and signals stay on Qt's main thread. The show
  dispatch sends a simple action/resource ID (`qt_utilities.cpp:157`).
- The template suggestion and C source construction use local values in the
  calling thread. The wizard's references remain within its synchronous scope.
- Namespace persistence uses per-call SQLite connections, transactions for each
  saved definition, and invalidation after commit. No SQLite statement or Qt
  widget is passed into the ontology worker.
- The ontology worker does not invoke TCC; sorting occurs after snapshot lookup
  on the calling thread, not in that worker.

## Original Recommended Direction

Give compiled sorters explicit ownership and retain an immutable compiled
generation for the entire sort; never retire executable storage while it is
in use. Keep compilation away from the GUI event loop. Prepare stable field
views once per record, not allocations per comparison. Publish immutable
namespace/member storage with ID-based asynchronous completion and let UI and
actor consumers retain/read it without rebuilding the full payload. Audit the
sorter C ABI's mutable globals/reentrancy before sharing execution between
owners. Merely adding a lock around cache lookup would not protect the returned
function's lifetime or fix the UI wait and copy/leak problems.

## Targeted Fixes

The requested fixes cover findings 1, 2 and 4. Synchronous compilation and
the existing ontology refresh wait are intentionally unchanged.

- Compiler caches are thread-local. Each entry retains an RAII-owned compiled
  generation, and the active sort retains that generation even if its cache
  entry is replaced. Sort execution checks the owning thread. Compiler error
  callbacks are detached before the compiled state escapes compilation.
- C ABI fields borrow stationary capture buffers and are prepared once per
  record. Comparisons only inspect those fields and reorder record indices.
  The fallback stem comparison also borrows strings; the leaking as_charp
  conversions in these paths have been removed.
- The ontology producer prepares immutable public records before publication.
  Readers retain record storage and select/reorder integer indices, without
  materializing namespace/member/string payloads again. Published strings use
  transferable storage, capture/parent lists are immutable std::vector values,
  and file URLs are constructed on the consuming thread from shared paths.
  Member lists and sorter definitions come from the same snapshot generation.
- Reader APIs and their UI, Scheme and data consumers use retained immutable
  views. Writable namespace forms remain local drafts. No new global lock or
  synchronization inside individual comparator calls was introduced.

Focused coverage in namespace_ontology_test checks cross-thread record and
capture-buffer identity, old snapshot retention across refresh/restart,
compiled-generation retention across replacement/compilation failure,
thread-local compiler state, stable payload addresses while sorting, and
compiler-state cleanup at thread exit.

Verification:
- Normal namespace_ontology_test: four QtTest checks passed, including both
  test cases and setup/cleanup. namespaces_template_test also passed.
- TSan: both cases passed with no data-race report. The default exit check
  separately reported a finished QtTest Watchdog thread as a thread leak;
  the full log is /tmp/athena-namespace-ownership-tsan-test.log.
- Re-running with TSAN_OPTIONS=halt_on_error=1:report_thread_leaks=0 passed.
  No data-race suppression was added. Log:
  /tmp/athena-namespace-ownership-tsan-races.log.
- libtcc and its generated machine code are not TSan-instrumented. The test
  exercises instrumented host ownership/cache/snapshot code and functionally
  checks compiler-generation isolation; it does not certify arbitrary C sorter
  source as race-free.
- All fixture data was under QTemporaryDir; the user's Vault was not touched.
