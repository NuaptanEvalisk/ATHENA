# ATHENA tests

CTest is the source of truth for maintained automated regressions.

Build the configured Qt6 tree with the normal project command:

```sh
cmake --build build_qt6 -j20
```

Run all registered tests, or a focused subset:

```sh
ctest --test-dir build_qt6 -j20 --output-on-failure
ctest --test-dir build_qt6 -R 'document_history|node_v2' --output-on-failure
```

## C++ tests

`tests/CMakeLists.txt` automatically registers ordinary `*.cpp` tests. These
targets link through the same full ATHENA test-runtime closure so a test does
not silently keep an obsolete hand-written subset of production libraries.

A few executables need special setup or compile definitions and are declared
explicitly in CMake, but they should still reuse the shared test-runtime link
configuration whenever they exercise the main application body.

Running a test binary directly is useful for debugging, but CTest supplies the
project environment and should be used for the regression result.

## Scheme tests

Maintained Scheme regressions use one of two paths:

- source-level/pure Scheme checks run under the configured Guile and are
  registered directly with CTest;
- editor, actor, persistence, export, and other application-level checks run an
  isolated ATHENA profile through a Python runtime wrapper and are also
  registered with CTest.

`scheme/runtime-test.py` is the generic isolated runner for a Scheme script.
Feature-specific wrappers remain appropriate when a test needs extra fixtures
or post-run validation, such as persisted XML inspection or multi-process
protocol checks.

Do not add a permanent regression as an undocumented command that must be run
by hand. If it is stable and exercises a supported contract, register it.

## Manual smoke tests

Tests that genuinely require an interactive desktop, Xvfb-specific behavior,
external services, or other environment-dependent manual inspection may remain
opt-in. Their filenames and comments should say why they are not in CTest.

Historical review harnesses, pre-fix checkpoint comparisons, temporary tracing
programs, and tests for retired features should not be kept as pseudo-regression
tests. Preserve useful design history in notes or commit history instead.
