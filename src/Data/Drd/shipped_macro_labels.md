# Cold Shipped Macro Declarations

`init_standard_tree_labels()` registers core labels first, then a generated
catalog of `assign(name, macro(...))` and `assign(name, xmacro(...))`
declarations from `ATHENA/packages/**/*.ats` and `ATHENA/styles/**/*.ats`.
These shipped declarations are the source of truth; document input is not.

The Python generator uses ElementTree and currently accepts the shipped XML v1
UTF-8 source grammar. An unsupported envelope, encoded literal name, malformed
XML, or DOCTYPE fails generation rather than silently producing a partial
catalog. Generated C++ lives under the build directory. Both desktop runtime
archives and the standalone Hodarium node depend on the same generation target.
CMake tracks edits, additions and removals in these two resource directories.

The catalog registers names only. It does not load packages, execute Scheme,
evaluate macro bodies, infer arities/accessibility, or modify the strict v3
codec. The normal style/DRD engine still owns those semantics. Core label IDs
are installed first; catalog registration reuses existing IDs.

## Boundaries

- Literal declarations are collected throughout shipped sources, including
  conditional/nested declarations. Registration does not imply that the
  corresponding package is active or that its macro has been evaluated.
- Declaration factories are inspected at static calls in shipped sources.
  Their names may use literal text, formal `arg` bindings, `merge` and
  `unquote`; transparent `style-with` wrappers are allowed. The factory body
  itself supplies the declaration template, including names such as `section*`
  and `todo-list-continued`. No feature-name expansion table is maintained.
  Nested macro parameter scopes are not traversed with outer argument bindings;
  unresolved expressions stay undeclared. Expansion has a bounded worklist.
- Literal `value` aliases are included only when their target is a proven
  macro declaration (including alias chains). Variables and unresolved cycles
  do not become macros. Arbitrary `extern`, computed branches and runtime
  evaluation are not executed. Mere tag occurrences do not declare labels.
- Examples, user styles, downloaded plugins and custom extensions are not in
  the shipped standard catalog. Their labels must be explicitly registered by
  their declaration/DRD owner before strict decoding. Unknown input cannot
  register itself, and the reader never loads a style to make it valid.
- Editing installed `.ats` resources without rebuilding does not update the
  compiled catalog. A changed shipped resource set requires regeneration.

The generator reports the catalog size and counts of computed declaration
templates. These are observations, not hardcoded limits or completeness claims
for dynamic styles. Native-owned placeholders (for example namespace-page
fields) belong to the standard DRD declarations, not this style catalog.
