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
