# ATHENA Guile Runtime

This directory vendors the source of GNU Guile 3 for ATHENA's private Scheme
runtime.  It is not a general-purpose Guile distribution.

- Upstream project: GNU Guile
- Upstream release: 3.0.10 (`v3.0.10`)
- Upstream commit: `b2cc237a02dcb13625885e76df28bc254a522100`
- Upstream license: GNU LGPL version 3 or later; see `COPYING.LESSER`
- Imported from: `https://git.savannah.gnu.org/git/guile.git`

ATHENA carries the complete upstream source so that the runtime, module
registry, bytecode compiler, and ABI evolve together with ATHENA.  Local
changes must retain upstream copyright and licensing notices.  Features may
only be removed after the ATHENA Scheme corpus has been checked against the
resulting runtime.

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
