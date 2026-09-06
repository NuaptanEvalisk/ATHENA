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
   - Initial source audit (not yet a reproduced root cause): manual saves call
     vault-anchor-before-manual-save. If changes require confirmation,
     vault-anchor-confirm-native calls athena_native_anchor_enunciations_confirm
     in native_interfaces.cpp:621. That function constructs QDialog and runs
     exec() directly, with no GUI dispatch guard, although the save continuation
     executes in its BufferActor. This is an unsafe boundary to reproduce and
     fix with asynchronous confirmation and actor-bound completion, not a
     blocking GUI hop or a function-name exception in the glue generator.
   - Also inspect buffer_export -> export_buffer -> get-link-locations and
     save completion. buffer_actor::wait blocks on its condition variable;
     prove an actual wait cycle before attributing the two-document freeze.
4. **DONE: Angle brackets nested inside vertical bars**
   - Baseline real typesetting reproduced shorter outer bars with both fixed
     and stretchable angles, most visible in bold Pagella. Source, box metrics
     and 27 baseline images: /tmp/athena-angle-{metrics.log,baseline/}.
   - First incorrect transition: nested delimiter boxes report the enclosed
     body's bracket extents, not their resolved glyph size. The outer sizing
     heuristic then tightens further, selecting a bar shorter than the angles.
   - In concat_post.cpp, retain already resolved opening/middle/closing glyph
     bounds as a minimum after normal body tightening. Use existing line-item
     semantics, with no font names, glyph-name cases or renderer compensation.
     Rejected the broader trial of changing all delimiter box extent reporting.
   - PASS: 18 actual pixel-enclosure cases (Pagella medium/bold and Termes,
     fixed/stretchable angles, character/fraction/nested fraction), plus stable
     heights through 12 repeated delimiter layers for all three body sizes.
     Final images: /tmp/athena-angle-regression/. Temporary metrics removed.
   - PASS: normal icpx -j20 build and complete local runtime installation:
     /tmp/athena-angle-runtime-build.log. Built/installed SHA-256:
     e7fe1f60f814f832d05a597af95a62a29579d5d634e75657251a5d5e3def2db0.
   - PASS: evaluation_bar_test, evaluation_bar_editor_test and
     math_keyboard_scheme_test, 3/3 in 8.88s; /tmp/athena-angle-tests.log.
     Tests used temporary profiles and offscreen Qt, no personal vault or Xvfb.
5. **DONE: Commutative diagram labels and transverse arrow displacement**
   - Consult Quiver design/implementation (q.uiver.app) for terminology and
     geometry. Preserve label placement, allow transverse arrow displacement,
     and test direction, curve, rendering, editing, and serialization.
   - Reproduced shaft-through-fraction labels in actual native PDF output:
     /tmp/athena-cd-baseline/evaluation.pdf. The first incorrect transition is
     fixed 0.16cm centre displacement, independent of typeset label dimensions.
   - Consulted MIT Quiver src/arrow.mjs and src/ui.mjs at
     /tmp/athena-quiver-reference-20260906. Edge offset already moves all native
     control points; retain its AST and make its transverse meaning explicit.
     Use existing Qt QPainterPath/Stroker for curve intersection, rather than
     importing Quiver's DOM editor or hand-porting its intersection engine.
   - Side labels now clear the complete own-edge footprint, including multiple
     shafts and markers. Actor-local Qt value geometry only; no QObject, GUI
     access or shared caches. Do not construct collision paths for unlabelled,
     centre or over edges. Preserve native mathematical boxes and vector output.
   - Centre clips only its own arrow below the horizontal label; over rotates
     the label with its tangent. Reverse preserves the displaced curved route,
     side and longitudinal label position (the last was previously omitted).
   - Initial geometry and real-editor/PDF checks passed. Pixel verification
     found zero black intrusion in six side/centre labels, while over retained
     intentional overlap. Artifacts: /tmp/athena-diagram-final/.
   - Visual inspection exposed a PDF clipping-state bug: restored Q returned
     line width to 5, while the backend cached 10 and omitted the next w command.
     Invalidate current_width on clipping restoration. MuPDF XML trace test
     correctly rejects the pre-fix PDF with widths {5,10}. The fixed export
     retains the same width across every clipped segment and arrowhead.
   - PASS: normal icpx -j20 build, bytecode and local runtime deployment:
     /tmp/athena-diagram-final-build.log. Built/installed SHA-256:
     0950ac7dd13af8ecbdfa8378fe0aa72a8ab5d9f0d5431329a096b0a75b45ca54.
   - PASS: commutative_diagram_geometry_test, commutative_diagram_editor_test,
     evaluation_bar_editor_test and math_keyboard_scheme_test, 4/4 in 12.25s;
     /tmp/athena-diagram-ctest.log. Geometry covers both sides, wide/tall labels,
     curves, loops, multiple shafts, reversal and already-clear positions.
     All editor tests use isolated /tmp profiles and offscreen Qt, no Xvfb,
     personal vault access or TSan-clean claim.
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
7. **DONE: Eqnarray copy/paste semantics**
   - Whole selections carry the enclosing eqnarray, not its table/document.
     Partial rows/cells retain equation-array semantics when pasted.
   - Verify whole, partial, mouse, keyboard, clipboard serialization, and ordinary
     tables, without damaging editable equation contents.
   - Isolated baseline /tmp/athena-eqnarray-baseline.log proves selecting the
     complete layout TFORMAT returns bare TFORMAT instead of the source
     eqnarray*. Selection promotion now targets its enclosing equation array;
     partial clipboard copies rebuild only the intervening semantic wrappers.
     Nested matrices stop at their own cell/wrapper and ordinary tables retain
     their previous behavior. Plain text within a cell remains inline math.
   - Equation fragments paste as display arrays into text, or fill cells when
     the destination is another equation array. Full cuts remove the equation
     shell; partial cuts preserve it. Source editing bypasses promotion.
   - Full-copy validation initially reached an unrelated headless clipboard
     crash: named clipboard retrieval unconditionally dereferenced QClipboard.
     Internal/headless get/set/clear now avoid system clipboard operations.
     Protect internal stores with a mutex and give stored keys independent
     string storage; do not retain actor-owned key reference counts.
   - Expanded runtime asserts numbered/unnumbered whole document/format/table
     selections, full cuts, reverse keyboard selection, row/cell fragments,
     paste into an existing array, ordinary tables and nested matrices.
   - The test harness previously called Guile exit, bypassing application
     shutdown and crashing after writing #t. Use quit-TeXmacs on the GUI
     execution context instead; still require a zero process exit and #t
     report. No production shutdown behavior or crash handling changed.
   - PASS: /tmp/athena-eqnarray-shutdown-ctest.log, all four focused editor and
     keyboard tests, including actual clipboard operations and PDF export.
     Normal build and installation: /tmp/athena-eqnarray-final-build.log;
     installed SHA256 a34e389d3b1f6ffe2ac24b921480316ff92c6aa991b10e1db7ba7f1ae411dc0e.
     Profiles isolated under /tmp, Qt offscreen, no personal vault or Xvfb.
     System clipboard GUI integration and TSan were not exercised here.
   - Two additional eqnarray runs passed: /tmp/athena-eqnarray-repeat.log.
     An intentionally failing script is still rejected despite clean app exit:
     /tmp/athena-editor-expected-failure.log. Assertion checks are not weakened.
8. **DONE: Definition names and comma-separated aliases**
   - Extract bold first-line names, not arbitrary body text; split all declared
     aliases and make every alias eligible for radioactive linking.
   - Test artifactization and actual matching, including spaces and punctuation.
   - Definition semantic_names now come from bold first-line declarations,
     including declarations after introductory text and formatting wrappers.
     Split every comma-separated alias, trim and deduplicate, and stop at the
     first paragraph or explicit newline. No fallback to definition prose.
     Other enunciation naming remains unchanged; matcher already indexes all
     semantic_names. No matching hot-path deep copy or new shared state.
   - User explicitly requires no old artifacts database compatibility: no
     migration or extraction-contract change. Rebuild artifacts from source.
   - New extraction, alias matching and fresh temporary-vault storage tests
     passed. Full artifacts suite: 39 passed, 1 failed, 1 skipped. Remaining
     failure is navigatesArtifactAndLoadsDisambiguationPage's immediate-buffer
     assertion, recorded separately rather than hidden or marked passed.
     Log: /tmp/athena-definition-alias-verified-ctest.log.
   - Normal icpx -j20 build and runtime installation passed:
     /tmp/athena-definition-alias-verified-build.log and
     /tmp/athena-definition-alias-deploy.log. No personal vault touched.
9. **DONE: Suppress radioactive matches throughout definitions**
   - Exclude both title and all body content of definitions, including nested
     markup, while retaining ordinary downstream matches.
   - Public macro suppression scope now includes definition for concat, lazy
     and bridge typesetting. Source-path checking excludes any descendant,
     rather than only the leading bold title. Transclusion marks the whole
     definition before rewriting, including untitled definitions.
   - Removed obsolete title-only APIs. Tests cover title/body/nested content,
     invalid paths, ordinary downstream content and other enunciations.
     Artifact fixture initializes standard DRD labels explicitly, eliminating
     a test-order dependency when exercising formatted definition aliases.
   - PASS: five focused Qt test cases including fixture setup/teardown in
     /tmp/athena-definition-scope-verified-tests.log; three CTest regressions
     in /tmp/athena-definition-scope-ctest.log. All test-owned processes ended.
     Normal build/deployment: /tmp/athena-definition-scope-build.log and
     /tmp/athena-definition-scope-deploy.log. No TSan-clean claim.
10. **DONE: Structural global search**
    - Inspect index/query representation and design non-plaintext matching.
      Ensure mathematical structure such as `x^2+1` is searchable, with tests for
      token/structure boundaries, scripts, fractions, mixed text/math, and UI.
    - Verified UI path: QTMGlobalSearch retains query trees, imports document
      trees and calls append_content_matches. Existing tree search already
      handles structure; no plaintext index or replacement parser introduced.
    - Reproduced zero results for math x^2+1 inside f=x^2+1+y in
      /tmp/athena-structural-search-baseline.log. Reuse select-region to search
      within the math context and return exact source ranges.
    - Reject substring hits inside mathematical words/numbers using the same
      lexer as math_language::next_word, extracted into math_token.hpp.
      Thread-local search configuration prevents concurrent GUI/actor queries
      from overwriting each other's flags and limits.
    - PASS: all 18 vault-search cases, including scripts, fractions, mixed
      text/math, numeric/operator boundaries, existing fuzzy behavior and
      4000 concurrent case-sensitive/insensitive queries. Log:
      /tmp/athena-structural-search-final-tests.log.
    - Normal build/deploy passed: /tmp/athena-structural-search-final-build.log.
      Installed SHA256 d3d1f7e090e0a73f3a43610dccef94d1d169a332d861c8314c3a656a460a8c53.
      No GUI interaction or TSan run claimed; helper is used by the real pane.
11. **TODO: Structured definition titles and radioactive linking**
    - Artifactize and match mixed titles such as math sigma + `-algebra` without
      flattening away mathematical identity; integrate with alias extraction.
12. **TODO: Multithreading-aware crash reporting**
    - Audit inherited signal/exception handling; produce useful thread/actor
      information without unsafe editor access in signal handlers.
    - Remove the old root/current/shifted path and physical-selection dump.
      Test crash reporting in isolated subprocesses, including worker faults.
13. **DONE: NESTED UPDATING when saving an unsaved buffer**
    - Locate the first reentrant update boundary in save-as, correct ownership
      and scheduling without hiding the diagnostic. Test cancellation, success,
      UI responsiveness, and related save flows.
    - Confirmed source shape: qt_chooser_widget.cpp still uses stack KFileDialog
      with dialog.exec() in perform_dialog_with_kfiledialog, and exec() in the
      QFileDialog fallback. Launch is reached from the GUI update's external
      effect drain. Trace the reentrant timer and convert the modal lifetime to
      open()/finished with an explicit retained widget/callback lifetime.
    - Replaced both chooser exec() paths with heap dialogs, application-modal
      show() and finished callbacks. The completion connection retains the
      TeXmacs widget until native dialog destruction, reads values before child
      destruction and delivers callbacks once even with repeated finish signals.
      Visibility cancellation is implemented; duplicate focus/open requests do
      not create additional dialogs. No suppression of NESTED UPDATING.
    - The old unused static QFileDialog helper was replaced by the complete Qt
      fallback, preserving suffixes, directories, image parameters and portable
      LaTeX options. KDE remains the normal backend. GUI creation asserts thread
      affinity. Corrected the missing separator before Qt image parameters.
    - Baseline stack in /tmp/athena-file-chooser-baseline.log confirms
      perform_dialog_with_kfiledialog -> QDialog::exec. The initial test's
      single-shot watchdog expired before the KDE dialog appeared; corrected
      the harness to wait for a visible chooser instead of treating that test
      timeout as a separate product defect.
    - PASS: 14 offscreen Qt/KDE cases for nonblocking completion, application
      modality, caller-reference release, cancellation, duplicate completion,
      file opening, save suffix, directory and image selection. Tests use only
      temporary homes and input files. No Xvfb or real vault used.
    - PASS: file_chooser_test, unsaved_buffers_scheme_test and
      glue_generator_test: 3/3 in 3.69s, /tmp/athena-file-chooser-ctest.log.
    - PASS: normal icpx -j20 build and local runtime deployment,
      /tmp/athena-file-chooser-deploy.log. Built/installed binary SHA-256:
      68585ee627378f999becf18099765b234bbf88bfa8e6b916aa7deb85c1114fb3.
      This removes the chooser's nested update boundary; it does not establish
      the separate two-document save freeze as resolved.

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
