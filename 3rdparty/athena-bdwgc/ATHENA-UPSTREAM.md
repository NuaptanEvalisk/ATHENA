# ATHENA private BDW-GC source

This directory vendors the BDW-GC 8.2.12 source used by ATHENA's private
Guile runtime.

- Upstream: https://github.com/ivmai/bdwgc
- Upstream tag: `v8.2.12`
- Upstream commit: `4fab5386df64466b2b61fc7209bef033cad1e6cc`

ATHENA builds this source as a private static PIC library with parallel
marking, thread-local allocation, large-heap support, and Intel LLVM
ThinLTO optimization.  It is linked into `libathena-guile.so`; runtime GC
policy remains in Guile's `libguile/gc.c`.
