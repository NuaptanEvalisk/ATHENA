# AGENTS.md

## Debugging Rules From the PDF Label Failure

This repository contains layered systems. When a bug crosses representation
boundaries, do not patch from intuition. Debug from evidence.

### What Went Wrong

- I treated symptoms as causes and patched the PDF/backend path before proving
  where labels disappeared.
- I blurred separate representations: source markup, expanded loci, lazy/bridge
  typesetting structures, page boxes, renderer callbacks, and final PDF objects.
- I accepted schema-specific fixes such as `export-label-*` checks too easily
  instead of preserving semantic boundaries.
- I added diagnostics at the wrong layer and initially interpreted missing
  diagnostics as a downstream failure instead of evidence that the code path was
  never reached.
- I declared partial progress too early when hyperlinks stopped becoming anchors,
  even though they still failed to become PDF links.

### Rules

- Define the relevant representations before explaining the bug: source tree,
  expanded tree, lazy/bridge structures, line items, boxes, renderer calls, and
  output-format objects.
- Find the first incorrect transition. Instrument boundaries in order and stop
  guessing once a boundary fails.
- Treat missing diagnostics as evidence that the instrumented path was not
  reached; move earlier in the pipeline.
- Keep semantic concepts separate: a hyperlink creates a clickable annotation; a
  label creates a destination; generated ids are not logical labels.
- Do not hardcode feature-specific naming schemes in lower layers. Export naming
  belongs in export code; rendering should consume semantic refs and anchors.
- Use minimal reproductions before patching broad flows. Test label-only,
  hyperlink-only, and combined cases separately.
- Read the exact function before stating intent. If asked what a function does,
  describe behavior from code, not from assumed design.
- Add diagnostics as temporary, scoped checks against concrete expectations.
  Remove them before calling the fix done.
- Preserve user changes and unrelated artifacts. Commit only the files needed for
  the fix.
- When the user challenges an explanation, re-check the code and the logs instead
  of defending a hypothesis.
- Treat user fury plus concrete evidence as signal. If the user supplies a
  minimal case, exact output, or a layer-specific question, follow that evidence
  immediately.
- If three consecutive attempts at the same bug fail to produce observable
  improvement, stop patching from local hypotheses. Proactively tell the user
  that the issue should be escalated to the Pro model, and prepare a prompt plus
  a source/log archive with all relevant context before making further fixes.

## Strict Commit Message Protocol

- Before creating or amending any commit, read the full recent commit messages
  with bodies, not only one-line subjects, and follow the established project
  style.
- Commit messages must use the current project format:
  - a concise subject of the form `type: imperative summary`, where `type` is
    consistent with recent history, such as `fix`, `new`, `remove`, or
    `improve`;
  - a blank line after the subject;
  - concrete bullet points describing the actual changes and affected behavior.
- Do not create bare one-line commits for nontrivial code changes.
- Do not use a misleading type. For example, use `improve:` for improvements,
  `new:` for new features, `fix:` only for bug fixes, and `remove:` for removals.
- The commit body must mention the important functional surface of the change,
  not just a vague cleanup summary.
- If a commit message is wrong, amend it immediately before reporting success.

## Layer Ownership And Source-Of-Truth Fixes

- When evidence identifies the broken layer, fix that layer's source of truth
  first. Do not cover a bad theme, configuration, resource map, generated file,
  or preference value by adding compensating code in a lower or unrelated layer.
- UI styling bugs whose cause is CSS or theme data must be fixed in the theme
  CSS. C++ widget code may set structural invariants, but must not become a pile
  of local style overrides for values that are authored in shipped theme files.
- Resource selection bugs must be fixed in the resource map or packaged
  resources before adding fallback logic. Loader changes are acceptable only
  when the loader policy itself is wrong.
- Before patching around a symptom, search for existing declarative sources
  such as CSS, JSON maps, Scheme preferences, generated dependency patches, and
  startup scripts. If one of them owns the value, edit it directly.
- Avoid duplicate truth. If a value is changed in a source file, remove stale
  compensating overrides that were added while debugging, unless they protect a
  distinct invariant and are documented by code structure.
- Treat "I found the source but patched elsewhere" as a failed debugging
  transition. Revert that direction, move the fix to the source layer, and then
  retest.

## Filesystem Search Safety

- Never run `find` from `/` or `/home/felix`. Large mounted drives are reachable
  below those roots and an unrestricted traversal can exhaust the Codex session.
- Restrict filesystem searches to the known repository or an explicitly named
  data directory. When the user supplies an exact path, operate on that path
  directly instead of searching a broader parent.

## Mature Implementations Before Approximate Replacements

- For a self-contained capability request, first investigate whether a mature,
  maintained implementation or library already exists. Prefer the established
  implementation even when it introduces a substantial required dependency;
  ATHENA does not prioritize being lightweight over correctness and quality.
- Do not hand-write an approximate substitute merely because it is quick or
  avoids adding a dependency. A partial C syntax highlighter, improvised parser,
  simplified layout engine, ad hoc protocol implementation, or similar
  "good enough" replacement is unacceptable when a professional implementation
  is available.
- Before implementing such a capability, document the existing implementations
  considered, verify their licensing and integration surface, and use the best
  suitable one as the source of truth. If none is suitable, explain the gap to
  the user before writing a new implementation.
- Treat "write something that looks approximately right" as a failed engineering
  decision, not as incremental progress. Do not leave the approximation in the
  worktree while switching to the proper implementation.

## System Package Installation Boundary

- Never install system packages without the user's explicit action or approval.
  If a required dependency needs `zypper`, `sudo`, root access, or another system
  package manager, stop and tell the user the exact package and command needed.
- Do not bypass that boundary by downloading RPMs, extracting development files
  into a temporary directory, creating a private package prefix, or otherwise
  emulating a system installation. This prohibition applies even when the files
  would remain under an ignored build directory.
- After identifying a missing system dependency, make no further dependency
  integration or feature implementation changes until the user confirms that
  the package is installed.

## Generated Scheme Interfaces

- Do not write or edit per-procedure C++ glue wrappers, registration calls, or
  Scheme glue declarations manually. Define native interfaces in XML under
  `src/Scheme/Glue/` and let the CMake `athena_glue` preprocessor generate them.
- The XML preprocessor emits C++ and Scheme directly. Do not revive the removed
  Scheme-to-C++ generator or add function-name exceptions to the preprocessor.
  Feature implementations belong in separately compiled native C++ files, not
  in `glue.cpp` or XML code fragments.
- Read `src/Scheme/Glue/README.md` before changing bindings. Generated files
  belong in the build directory, not in source control.
- Keep native implementation and thread/actor ownership outside the XML.
  Generated marshalling does not authorize GUI access or editor-state access.
- Verify both generation and the resulting binary's runtime binding. A changed
  declaration alone does not prove that a procedure is available to Scheme.
