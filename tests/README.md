# ATHENA tests

CTest is the source of truth for maintained automated regressions.

Build only the application or test targets relevant to the change:

```sh
cmake --build build_qt6 --target ATHENA.bin -j16
cmake --build build_qt6 --target buffer_actor_test native_conversion_test -j16
ctest --test-dir build_qt6 -R '^(buffer_actor_test|native_conversion_test)$' --output-on-failure
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

## Native runtime integrations

Command adapters are generated from XML. The generator contract test compiles
and executes a temporary C++ fixture; real value/dispatch and ownership behavior
is covered by the native C++ tests. No tests bootstrap a Scheme VM.

The Python integrations in `runtime/` use normal native CLI operations and
isolated profiles. The AUDMAP integration starts an ordinary document argument
and tests the live protocol plus Vault proposal admission and Qt review. Website/export, offline migration, crash reporting,
watchdog and symbolization checks remain registered in CTest.

Interpreter-only runners and their Scheme fixtures have been retired.
Their supported editor/actor behavior belongs in the C++ tests or AUDMAP
integrations, with explicit owner and exception boundaries.

Do not add a permanent regression as an undocumented command that must be run
by hand. If it is stable and exercises a supported contract, register it.

## Manual smoke tests

Tests that genuinely require an interactive desktop, Xvfb-specific behavior,
external services, or other environment-dependent manual inspection may remain
opt-in. Their filenames and comments should say why they are not in CTest.

Native UI coverage includes `audmap_repl_pane_test`,
`audmap_authorization_ui_test`, and `structured_radioactive_test`. Retired Xvfb
smokes that injected Scheme are no longer callable test entry points.

Historical review harnesses, pre-fix checkpoint comparisons, temporary tracing
programs, and tests for retired features should not be kept as pseudo-regression
tests. Preserve useful design history in notes or commit history instead.
