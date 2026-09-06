# Overnight Workbench

## Constraints and Baseline

- Branch: `codex-workbench`, fast-forwarded from `2ac8e4bcd6` to master
  `aa58aa244c` before starting. Do not merge back into master before user testing.
- Each requested item gets a separate functional commit, with its own build and
  focused tests before moving to the next item. No stubs or demo implementations.
- Never write `/home/felix/data/Notes`. Never read or write
  `/home/felix/Documents`, `/home/felix/Desktop`, `/home/felix/Downloads`.
- GUI/runtime tests must use isolated homes and vaults under `/tmp`, or the
  explicitly permitted `/home/felix/data/Notes-sandbox`. No host profile startup.
- Check battery periodically; stop work at <=20%. Initial reading: 100%.
- Normal build: `build_qt6`, icpx, `-j20`. Sanitizer build: `build_san` when needed.
- Do not install system dependencies without explicit user approval. Do not ask
  interactive questions overnight; record a dependency blocker and continue
  other independent work where possible.

## Task Ledger

1. **DONE: Minus + Tab -> setminus**
   - Source: `ATHENA/progs/math/math-kbd.scm`, math-mode variant map.
   - Verify Tab is the variant key, text mode is unaffected, existing minus/arrow
     sequences remain unchanged. Build bytecode and run a focused mapping test.
   - Added the math-mode `- var` binding without changing generic/text bindings.
   - PASS: `ctest --test-dir build_qt6 -R '^math_keyboard_scheme_test$'
     (0.04 seconds), including existing arrow and long-minus sequences.
   - PASS: `tools/compile-athena-scheme-bytecode.sh` with the existing normal
     build runtime, 20 workers. Log: `/tmp/athena-workbench-minus-bytecode.log`.
   - Full runtime target was deliberately stopped during redundant Guile
     bootstrap caused by branch checkout timestamps; Guile/GC content matches
     master exactly. All its processes were terminated. No C++ changed here.
2. **DONE: Automatically sized evaluation bar**
   - Support a trailing vertical bar sized to preceding content, including
     `frac(d, d x)` with a `t=0` subscript; make it accessible through editing.
   - Inspect existing bracket sizing and semantic markup first. Verify tall and
     short expressions, scripts, nesting, and export rather than fixed glyphs.
   - Added math-evaluation-bar using the existing around* representation with
     an invisible left delimiter and dynamically sized right bar. No renderer
     or new mathematical parser: wrap selected content or the current row
     prefix, bounded by fraction arguments, bracket bodies and table cells.
   - Menu: Insert -> Evaluation bar; keymap: math:right | var. Existing
     absolute-value, closing-bar and minus variants remain unchanged.
   - PASS: isolated headless actor editor tests for fraction, limit outside the
     pair, scripts, a derivative followed by f(x), explicit selection, content
     after the cursor, fraction argument boundaries, empty and nested insertion.
   - PASS: PDF export, rendered with pdftoppm and inspected visually. Artifacts:
     /tmp/athena-evaluation-bar-artifacts/evaluation.pdf and evaluation.png.
   - Harness finding: buffer-get-body is a snapshot, not the live editor tree.
     Use buffer-tree for cursor paths. After replacing the fixture, explicitly
     refresh its style/DRD and typesetter before selecting. Exit through global
     context and write the test result before exit so queued logs are not lost.
   - PASS: normal icpx -j20 native build, Scheme bytecode compilation and local
     deployment: /tmp/athena-evaluation-bar-build.log. Installed binary SHA-256:
     b5b21e7def6b1fae7e0d431f64f3e8dab07f92f5cf0ac4d9e42c91d47136eada.
   - PASS: evaluation_bar_editor_test, math_keyboard_scheme_test and
     evaluation_bar_test: 3/3 in 6.18 seconds. Pixel test measures increasing
     actual right-bar ink height for a character, fraction and nested fraction
     with Pagella, bold Pagella and Termes. Log:
     /tmp/athena-evaluation-bar-tests.log. No Xvfb or real vault used.
3. **TODO: Freeze saving one of two open documents**
   - Reproduce with isolated buffers and inspect UI/actor stacks and command
     ownership. Include both active and inactive buffer saves and save-as.
4. **TODO: Angle brackets nested inside vertical bars**
   - Trace source delimiters -> sizing -> font selection -> boxes -> pixels.
     Test Pagella bold and regular, nested `|<...>|`, and tall contents.
5. **TODO: Commutative diagram labels and transverse arrow displacement**
   - Consult Quiver design/implementation (q.uiver.app) for terminology and
     geometry. Preserve label placement, allow transverse arrow displacement,
     and test direction, curve, rendering, editing, and serialization.
6. **DONE: XML-driven glue preprocessor**
   - PRIORITY NEXT after item 1: a real missing-binding failure was found.
     `exec-buffer` exists in `build-glue-basic.scm` but not `glue_basic.cpp`;
     the current CMake graph does not run the glue generator.
   - Audit existing generation, migrate authoritative declarations into XML,
     generate C++ and Scheme before binary compilation via CMake dependencies.
   - No hand-maintained generated glue. Add glue-directory README and AGENTS.md
     rule. Verify deterministic output, incremental rebuild, clean build, and
     existing binding signatures/ownership semantics.
   - Evidence: regenerated all three groups with the existing Guile generator
     into `/tmp/athena-workbench-glue-{basic,editor,server}-generated.cpp`.
     Editor/server match exactly. Basic differs in missing exec-buffer, four
     manually implemented GUI color setters, and a std::move call. Preserve or
     relocate those semantics to native implementation/policy, not handwritten
     generated wrappers.
   - Implementation choice: Python ElementTree with direct C++/Scheme emission.
     Also considered SWIG (Guile supported, different interface model) and
     Shiboken (XML but Python-targeted); no package installation is needed.
   - References: https://www.swig.org/compat.html,
     https://doc.qt.io/qtforpython-6/shiboken6/typesystem.html,
     https://docs.python.org/3/library/xml.etree.elementtree.html.
   - Initial migration used the old Scheme emitter for comparison only. The
     user's clarification superseded that intermediate design: build-glue.scm,
     its Scheme inputs and legacy API generators are now deleted. The Python
     preprocessor directly emits C++, Scheme symbol inventory and API docs;
     no function-name branches or hardcoded receiver accessor names.
   - Migrated 1,211 bindings: 748 basic, 320 editor, 56 server, 87 native.
     The initial 1,117 signatures used a one-time Guile reader/SXML conversion;
     the additional 94 registrations were audited against the old native
     wrappers. Clang AST ranges located their implementations for relocation.
   - All native helpers formerly embedded in glue.cpp, including ADS state,
     dialogs, Materials/Vault record adapters, editor tree operations, GUI
     preference setters and UI dispatch, now live in native_interfaces.cpp
     with typed declarations. No handwritten wrappers or raw Scheme values
     remain in that implementation file. Predicates retain legacy semantics.
   - Generic XML policies cover argument moves, procedure validation and free
     nullary UI calls. Rooted object results preserve heterogeneous #f/string,
     #f/tree and #f/(url path) native contracts. No feature code in XML.
   - CMake generates before athena_body and deploys the generated symbol list
     to the ignored local runtime resource before Scheme compilation. Initial
     configuration seeds the resource before the source glob, avoiding a second
     configure/full rebuild when it first appears. Missing outputs are restored.
   - PASS: 11 generator tests including arbitrary-name policies, compiling and
     invoking generated wrappers, CMake regeneration and restored runtime
     resources, determinism, invalid interfaces and migration-boundary checks.
   - PASS: full native migration normal icpx -j20 build, then isolated headless
     runtime test covering 1,211 arities, native return contracts, procedure
     validation and exec-buffer. Log: /tmp/athena-native-glue-runtime.log.
   - PASS: final normal build and complete local runtime deployment, including
     compilation of the generated Scheme symbol inventory. Log:
     /tmp/athena-glue-deploy.log. Built and installed binary SHA-256:
     3e3561de414125139c7fa933b2fee5e252b5f7bac914fcbedb8119f43e1f829d.
   - PASS: final CTest glue_generator_test, glue_runtime_test,
     math_keyboard_scheme_test and unsaved_buffers_scheme_test: 4/4 in 3.68s.
     Log: /tmp/athena-glue-final-tests.log. The generator suite has 11 checks.
   - PASS: no-op normal build performed no C++ compilation, glue generation or
     linking: /tmp/athena-glue-noop-build.log. API documentation generation and
     git diff --check passed. Battery 100%. No GUI/vault tests were launched.
   - Runtime harness correction: CLI -x executes in an actor; exec-buffer is
     global-only. Test uses -H -X and exec-global, with a temporary HOME/profile
     and explicit TeX setup. Exit must be outside Scheme catch, since Guile
     implements it as a quit exception. No source ownership guards were bypassed.
   - The deployed symbol resource is ignored and removed from Git's index;
     its generated local copy remains available to the installed runtime.
7. **TODO: Eqnarray copy/paste semantics**
   - Whole selections carry the enclosing eqnarray, not its table/document.
     Partial rows/cells retain equation-array semantics when pasted.
   - Verify whole, partial, mouse, keyboard, clipboard serialization, and ordinary
     tables, without damaging editable equation contents.
8. **TODO: Definition names and comma-separated aliases**
   - Extract bold first-line names, not arbitrary body text; split all declared
     aliases and make every alias eligible for radioactive linking.
   - Test artifactization and actual matching, including spaces and punctuation.
9. **TODO: Suppress radioactive matches throughout definitions**
   - Exclude both title and all body content of definitions, including nested
     markup, while retaining ordinary downstream matches.
10. **TODO: Structural global search**
    - Inspect index/query representation and design non-plaintext matching.
      Ensure mathematical structure such as `x^2+1` is searchable, with tests for
      token/structure boundaries, scripts, fractions, mixed text/math, and UI.
11. **TODO: Structured definition titles and radioactive linking**
    - Artifactize and match mixed titles such as math sigma + `-algebra` without
      flattening away mathematical identity; integrate with alias extraction.
12. **TODO: Multithreading-aware crash reporting**
    - Audit inherited signal/exception handling; produce useful thread/actor
      information without unsafe editor access in signal handlers.
    - Remove the old root/current/shifted path and physical-selection dump.
      Test crash reporting in isolated subprocesses, including worker faults.
13. **TODO: NESTED UPDATING when saving an unsaved buffer**
    - Locate the first reentrant update boundary in save-as, correct ownership
      and scheduling without hiding the diagnostic. Test cancellation, success,
      UI responsiveness, and related save flows.

## Verification and Handoff

- Record commands/results and commit identifiers here as each item completes.
- Current GUI testing is limited to targeted checks, not repeated broad Xvfb
  sweeps. The user will test the resulting branch before merging.
- The full goal remains incomplete until every item is implemented and verified.

## Next Build Note

`cmake/AthenaGuile.cmake` now has an explicit `ATHENA_GUILE_PREBUILT_PREFIX`
cache option. `build_qt6` uses its existing `athena-guile-runtime`. Configuration
checks the library, headers, standard and compiled modules, and ATHENA-specific
exported callback symbol; normal builds reject TSan runtime libraries. No fake
ExternalProject stamps. Vendored runtime edits deliberately do not rebuild an
explicit prebuilt prefix, and CMake prints that limitation. Default builds still
build Guile/GC from source. The full normal binary build above succeeded.
