# ATHENA Private Guile Trimming Audit

Date: 2026-09-08. The implementation and verification record below supersedes
the original source-only inventory. Subsequent sections preserve that inventory
and its reasoning; references there to removed files describe the pre-trim tree.

## Implemented Trimming

- Removed non-Scheme frontends, examples, benchmarks, Emacs/readline and
  interactive/network REPL integration, Guix packaging, and inherited release
  history. Preserved licenses, attribution, and upstream revision provenance.
- Removed Guile's documentation build chain: manuals, Texinfo, C `.doc`
  extraction/tokenizer tools, generated procedure-doc database, and their
  configure/build/install rules. C `.x` registration and in-memory Scheme
  procedure docstrings remain; ATHENA uses the latter.
- Removed Guile Web/SXML and unused interactive utilities with their dedicated
  tests. ATHENA's own HTML/SXML converters remain. Preserved compiler IRs,
  Scheme/RNRS/SRFI, GOOPS, FFI, Unicode, threads/fluids/GC, JIT, and diagnostics.
  `scheme/repl.scm` remains for the R7RS `interaction-environment` API, not an
  interactive REPL. Platform/JIT architecture backends were not blanket-pruned.
- Moved noninteractive backtraces from `(system repl debug)` to
  `(system vm backtrace)` and updated native callers. The private `guile`
  executable remains a batch driver needed by ATHENA's dependency planner;
  `guild compile` remains for bootstrap. No user init or REPL is entered.
- Updated the shared bootstrap/source-install manifest. CMake now regenerates
  Autotools inputs, invalidates on build-description/source-inventory changes,
  and clears the private installed module trees before installation. ATHENA
  bytecode also depends on the runtime library, preventing stale cache reuse.

Logical source size fell from approximately 28.554 MiB / 1,997 files to
16.272 MiB / 1,626 files (about 43%). Compiled runtime modules fell from 339
in the previous installed prefix to 261. This is a source/build reduction,
not a claim about measured application memory or startup latency.

## Verification Record

- Clean three-stage Guile bootstrap and private-prefix installation succeeded,
  without the previous prebuilt prefix. Log:
  `build_qt6/guile-trimming-build.log`.
- Installed source and bytecode inventories exactly match `am/bootstrap.am`:
  271 source files and 261 compiled files, without stale removed modules.
- Focused runtime/fluids/threads/modules/procedure-properties/eval suites:
  208 passes, zero failures, one expected failure, four errors. All four errors
  are existing `eval.test` local-evaluation attempts to mutate an immutable
  pair; the previous runtime reproduces the same four. The non-eval suites
  account for 147 passes, including 17 new runtime-boundary checks.
  Logs: `build_qt6/guile-trimming-tests.log` and
  `build_qt6/guile-trimming-baseline-eval.log`.
- Batch `-c` evaluation succeeds, invocation without arguments exits without
  a REPL, `--listen` is rejected, and an uncaught Scheme error prints a usable
  backtrace before exiting nonzero.
- Normal debug ATHENA built successfully in `build_qt6`, followed by all 349
  ATHENA Scheme modules recompiling successfully with the trimmed runtime.
  Log: `build_qt6/guile-trimming-athena-build.log`.
- No TSan build or GUI stress run was performed. The deployed application
  binary/runtime was not replaced; the old private build/prefix were retained
  under `build_qt6/*-before-trimming` for comparison.

## Identity And Size

`3rdparty/athena-guile/ATHENA-UPSTREAM.md` identifies Guile 3.0.10, upstream
commit `b2cc237a02dcb13625885e76df28bc254a522100`, as ATHENA's private runtime,
not a general-purpose Guile distribution. The source still includes the broad
upstream distribution.

Tracked files total 1,997 files and approximately 28.554 MiB of logical file
contents. Selected groups (not installed sizes):

| Group | Files | MiB |
| --- | ---: | ---: |
| libguile | 528 | 7.901 |
| gc-benchmarks | 29 | 5.134 |
| module | 365 | 5.011 |
| doc | 116 | 3.115 |
| lib | 299 | 2.545 |
| test-suite | 276 | 2.450 |
| m4 | 226 | 1.052 |
| benchmark-suite | 26 | 0.104 |
| emacs | 11 | 0.060 |
| examples | 33 | 0.057 |
| guile-readline | 5 | 0.049 |

Across directories, 30 inherited `ChangeLog*` / `NEWS*` files occupy 2,577,802
bytes. These overlap the directory totals above. The brainfuck, ECMAScript,
Elisp, and Wisp language sources together occupy about 221 KiB.

Disk usage also includes generated, untracked material: for example,
`autom4te.cache` occupies approximately 8.7 MiB. Generated caches, backup files,
and bootstrap outputs are a separate cleanup category, not tracked features.
Do not indiscriminately delete build inputs based on `du` output.

## Remove With Their Build And Test References

| Candidate | Required scope |
| --- | --- |
| `module/language/brainfuck/` | Frontend, manifest entries, frontend tests |
| `module/language/ecmascript/` | JavaScript frontend, manifest entries, frontend tests |
| `module/language/elisp/` | Elisp frontend, `.el` inputs, `ELISP_SOURCES`, compilation rules and tests |
| `module/language/wisp/`, `module/language/wisp.scm` | Alternate syntax frontend and its references |
| `examples/` | Examples and recursive build/distribution entries |
| `benchmark-suite/`, `gc-benchmarks/`, `benchmark/` | Benchmarks and included make fragments |
| `emacs/` | Editor integration, separate from the Elisp language frontend |
| `.guix/`, `guix.scm` | Guix packaging/development integration and distribution hooks |
| Inherited `NEWS*`, `ChangeLog*` | Historical release messaging; retain provenance elsewhere |

Guix is packaging integration, not an operating-system platform. Removing it
does not imply dropping GNU/Linux support.

Update generic README/INSTALL/HACKING guidance and `ATHENA-UPSTREAM.md` to
describe the trimmed private runtime. Preserve applicable COPYING, LGPL,
third-party license notices, source copyright notices, and upstream provenance.
Historical release prose and license/attribution material are different things.

## REPL: Remove Interaction, Preserve Diagnostics And Batch Execution

The interactive frontend, network REPL, readline integration, and REPL-only
commands are unwanted. Candidates include `guile-readline/`,
`module/ice-9/top-repl.scm`, and the interactive portions of
`module/system/repl/{repl,server,coop-server,command,common,describe}.scm`.

Do not simply delete `module/system/repl/`:

- `libguile/backtrace.c:95-105` resolves `print-frame`, `print-frames`, and
  `frame->stack-vector` from `(system repl debug)`.
- `module/system/repl/debug.scm` supplies noninteractive stack diagnostics.
  Move those facilities to a diagnostics/VM module and update the C callers.
- `system/repl/error-handling.scm` mixes exception handling with entry into an
  interactive debugger. Remove interactive branches or their callers while
  preserving noninteractive error reporting.
- `ice-9/command-line.scm` implements both batch execution and REPL options,
  including the default top-level loop and `--listen`.
- `ice-9/boot-9.scm` contains REPL reader/prompt state. Remove it only after
  checking remaining readers and command-line consumers.
- `src/Scheme/Guile/guile_tm.cpp` contains obsolete commented REPL hooks;
  ATHENA's active backtrace/debug support must remain.

Crucially, **`bin/guile` is not only a REPL executable**.
`tools/compile-athena-scheme-bytecode.sh:101` invokes the private executable
with `--no-auto-compile` to run the bytecode dependency planner. Preserve a
batch Scheme driver, or replace that invocation before removing the executable.
`libguile/script.c` likewise mixes shell argument processing with batch use.

Build-time `guild compile` and `module/scripts/compile.scm` are currently
required by bootstrap. Removing the REPL does not justify deleting them.

## Documentation: Separate Extraction From C Registration

Remove manuals/Texinfo generation and their dedicated dependencies, but first
split the build chain:

1. `libguile/Makefile.am` makes `guile-procedures.texi` an `all-local` target.
2. C `.doc` extraction invokes `guild snarf-check-and-output-texi`.
3. The top-level makefile converts that output to `guile-procedures.txt` and
   installs it.
4. `module/ice-9/documentation.scm` reads this external procedure-doc database.

Remove that generated documentation database and extraction tools if no
remaining consumer needs them. `doc/`, `module/texinfo*`, and documentation-only
scripts can then be removed with their build/install rules.

Two distinct facilities must remain:

- `guile-snarf` generates `.x` C registration code. The `.c.x` rule is separate
  from `.c.doc`; `.x` generation is not documentation generation.
- ATHENA calls `procedure-documentation` in
  `ATHENA/progs/kernel/athena/tm-define.scm`. Preserve Scheme procedure docstrings
  and their API, even when the external documentation database is removed.

`module/sxml/` is a further candidate once Texinfo dependencies are removed.
ATHENA's own SXHTML converter is a separate implementation, not a reason to
retain or delete Guile's SXML modules by name alone.

## Additional Library Candidates

No direct shipping ATHENA imports were identified for the following groups.
That is evidence for a closure audit, not proof that every module is removable:

- `module/web/`: Scheme HTTP client/server stack. Preserve unrelated C/Qt
  networking and any remaining core socket users.
- `module/sxml/` and `module/texinfo*`: conversion/documentation libraries.
- Nonessential `module/scripts/` commands, such as API inspection, source
  scanning, RFC822/outline conversion, and documentation utilities. Keep
  compiler tooling; decide explicitly whether disassembly tooling stays.
- `ice-9/expect.scm`, `ice-9/occam-channel.scm`, `ice-9/sandbox.scm`: candidate
  general-purpose facilities without identified direct ATHENA callers.
- `ice-9/session.scm` and GOOPS describe/save utilities after REPL/doc consumers
  are removed. Keep GOOPS core.
- `statprof.scm`: optional developer profiling, not the benchmark suite. Decide
  whether this diagnostic capability is wanted before removing it.

Do not blanket-delete `srfi`, `rnrs`, `ice-9`, `oop`, `system/vm`, or the FFI.
Examples of actual dependencies:

- ATHENA boot uses `(ice-9 rdelim)` and `(ice-9 pretty-print)`.
- ATHENA's compatibility boot imports SRFI-2, 8, 16, and 26 through `(:use ...)`.
- Anchors/conversion use `(ice-9 threads)`; state/logic/graphics modules use
  `(ice-9 copy-tree)`; the bytecode planner uses SRFI-1 and rdelim.
- Scheme compilation uses `system base compile/language`, tree-il, CPS,
  bytecode, and value representations. They are **not foreign frontends**.
- Compiler passes import RNRS bytevectors, SRFIs, match, vlist, threads,
  GOOPS-related facilities, and other shared modules.
- GOOPS is also initialized/referenced from C, including `init.c` and `smob.c`.
- ATHENA recompiles changed Scheme sources at runtime: the compiler is not
  merely a build dependency.

Imports are not all literal `use-modules` forms. Account for ATHENA `(:use ...)`,
C `scm_c_public_variable`/module lookups, autoloads, compiler language discovery,
boot includes, and dynamically constructed imports. ATHENA evaluation and
typesetting code can construct module imports dynamically. A shipped runtime
allowlist must deliberately define which document-requested modules it supports.

## Platforms, Portability, And Tests

`cmake/AthenaGuile.cmake` rejects WIN32 and describes the supported runtime as
Linux Qt6. Windows-specific sources/configuration are candidates under that
policy. Architecture is a separate decision: the local and server machines
use x86_64, but do not assume that every other CPU architecture is permanently
unsupported without establishing the target matrix.

- `libguile/lightening/` contains the JIT and multiple architecture backends.
  Preserve x86 and shared implementation; trim others only for agreed targets.
- `prebuilt/` contains bootstrap inputs/target variants, not examples.
  `configure.ac` selects them by endianness and pointer width.
- `lib/`, `m4/`, and `gnulib-local/` include portability/build support, not just
  optional application libraries. Trace configured use before pruning.
- NLS is already disabled by ATHENA's configure flags. Catalog infrastructure
  may be removable; Unicode, locale, and character conversion are not NLS-only.
- Disabling deprecated APIs requires checking ATHENA's C API usage first.
- Keep core thread/fluid/GC/module/compiler/JIT regression tests. Remove tests
  dedicated to deleted features. `test-suite/` is not the benchmark suite.
  The 200 lightening test files occupy only about 172 KiB and protect the JIT.

## Build And Packaging Changes Required

The present build is broad: `Makefile.am` recurses into runtime modules,
stage0/1/2, readline, examples, tests, and docs. `am/bootstrap.am` shares
`SOURCES`/`ELISP_SOURCES` across three bootstrap stages; module sources and
stage2 bytecode are installed. Trim the authoritative manifests, not just
installed files. This reduces compilation work and shipped source/bytecode;
removing unloaded modules does not by itself prove lower runtime memory usage.

Important integration points:

- `cmake/AthenaGuile.cmake` configures shared/JIT, no NLS, thin LTO and runs
  full `make install`. Existing prebuilt-prefix configuration bypasses a
  vendored rebuild, so it cannot validate source trimming.
- Its source fingerprint currently covers `.c`, `.h`, and `.scm`, not all
  configure/make manifests. Add build-description changes to invalidation.
- The build invokes `configure`. Editing `.am`/`.ac` alone is insufficient;
  regenerate the distributed build products through the established process.
- `tools/release/copy-private-guile-runtime.py` copies whole source/bytecode
  trees, the private library, and `bin/guile`. Its staging replacement handles
  removals when the runtime signature changes. Update its required-file checks
  if the batch driver contract changes.
- `CMakeLists.txt` also installs runtime directories. Verify incremental
  installation/deployment does not leave deleted modules behind.
- Preserve the private runtime ABI and ATHENA native extensions. Invalidate
  bytecode/build caches as required; stale `.go` files can hide missing sources.

## Recommended Implementation Order

1. Remove leaf frontends, examples, benchmarks, Emacs/Guix integrations, and
   obsolete release prose; update build manifests and private-runtime identity.
2. Extract backtrace diagnostics from REPL modules. Retain batch execution;
   remove interactive/network REPL and readline.
3. Remove the Texinfo/doc extraction chain while preserving `.x` registration
   and Scheme docstrings.
4. Establish an explicit module manifest with dependency closure, then remove
   unused library families. Keep runtime, build, and diagnostic needs distinct.
5. Prune platform/architecture support only against the agreed target matrix.

After implementation, use one clean bootstrap and full ATHENA bytecode compile,
then focused startup, stale-source recompilation, thread/fluid/module ownership,
exception/backtrace, and batch-driver checks. Confirm removed modules are absent
from installed source and bytecode trees. Existing cached/prebuilt runtimes and
successful ordinary GUI startup alone are not sufficient validation. Broad GUI
stress testing is not required for this inventory.
