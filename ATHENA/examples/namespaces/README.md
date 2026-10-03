# Namespace sorter examples

ATHENA namespace sorter scripts use the canonical `.luau` suffix. A sorter
module returns a table with `version = 1` and exactly one of `key(fields)` or
`compare(a, b)`.

`key(fields)` is preferred: it runs once per member and ATHENA performs the
stable lexicographic ordering in C++. Keys may contain bytewise strings, finite
numbers, booleans, and exact signed 64-bit integer values from typed captures.
Shorter keys sort before longer keys when one is an exact prefix; equal keys
retain input order.

`compare(a, b)` must return exactly `-1`, `0`, or `1` and define a strict weak
ordering. ATHENA validates the relation on the current member set before
publishing a reordered result.

Input field tables are one-based and read-only. Every field exposes `text` and
`type`; integer fields also expose exact `integer`, and Roman fields expose
numeric `roman`. The sandbox provides the pure `athena.byte_compare`,
`athena.int64_compare`, and `athena.roman_value` helpers. Filesystem, network,
process, clock, and random-number access are not available to sorter scripts.
