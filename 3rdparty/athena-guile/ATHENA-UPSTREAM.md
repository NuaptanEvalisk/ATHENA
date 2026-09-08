# ATHENA Guile Runtime

This directory vendors the source of GNU Guile 3 for ATHENA's private Scheme
runtime.  It is not a general-purpose Guile distribution.

- Upstream project: GNU Guile
- Upstream release: 3.0.10 (`v3.0.10`)
- Upstream commit: `b2cc237a02dcb13625885e76df28bc254a522100`
- Upstream license: GNU LGPL version 3 or later; see `COPYING.LESSER`
- Imported from: `https://git.savannah.gnu.org/git/guile.git`

ATHENA carries a Scheme-only subset so that the runtime, module registry,
bytecode compiler, and ABI evolve together with ATHENA. Local changes retain
upstream copyright and licensing notices. The upstream revision above records
provenance; upstream NEWS and ChangeLogs do not describe this private runtime.

Removed surfaces: Brainfuck, ECMAScript, Elisp, Wisp, interactive/network REPL,
readline, Emacs integration, examples, benchmarks, Guix packaging, Texinfo
documentation generation, Guile Web/SXML/Texinfo modules, and unused interactive
inspection, session, expect, channel, and sandbox utilities. ATHENA's separate
HTML/SXML conversion modules are unaffected. Scheme/RNRS/SRFI, GOOPS, FFI,
Unicode, GC, threads, fluids, compiler IRs, and JIT support remain.

The private `guile` executable is a batch driver used by ATHENA's bytecode
planner. It does not start a REPL or read user startup files. `guild compile`
remains available for the three bootstrap stages. Noninteractive C backtraces
use `(system vm backtrace)`. Procedure docstrings remain available in memory;
there is no generated `guile-procedures.txt` database. C `.x` snarf registration
is retained independently of the removed `.doc`/Texinfo extraction pipeline.

The module installation and all bootstrap stages share the explicit source
manifest in `am/bootstrap.am`. Do not remove dependencies based only on direct
ATHENA imports: include compiler, native C lookup, test, and build-time users.
Core regression tests remain in `test-suite/`; tests for deleted features are
removed with those features.

Build through ATHENA's `cmake/AthenaGuile.cmake`, which regenerates Autotools
inputs before configuring. For a clean validation, do not set
`ATHENA_GUILE_PREBUILT_PREFIX` and do not reuse old bootstrap or installed
standard-library trees. See `notes/guile-trimming-audit.md` in the ATHENA root
for the audit and validation record.

The runtime is built with a private library name and is installed beside
ATHENA.  ATHENA must never silently fall back to a system Guile installation.

ATHENA-specific native facilities live in `libguile/athena-runtime.c`. They
own the module registry, import/inheritance operations, contextual definition
dispatch, definition properties, and lazy provider registry. The corresponding
Scheme forms (`texmacs-module`, `import-from`, `inherit-modules`, `tm-define`,
`tm-property`, and `lazy-define`) are syntax provided by the runtime, not a
bootstrap library re-evaluated on every process start.

The private compiler also preserves source metadata expected by ATHENA's
Guile 1.8-era procedure introspection and accepts ATHENA's sequential top-level
module construction. Application bytecode is cached separately from the
runtime's compiled standard library so upgrading either source corpus safely
invalidates the relevant cache.
