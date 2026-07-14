# ATHENA Semantic Math Correction: Architecture and Lean Preparation

## Scope

This report describes the implementation inherited from TeXmacs that ATHENA
currently calls semantic mathematical editing and correction. It distinguishes
the document tree, grammar input, parser state, and correction output because
these are different representations. It also records what is and is not
available for future Lean integration.

## Representations

1. **ATHENA document tree**

   The authoritative formula is a native `tree`. Atomic nodes contain encoded
   text or symbols. Compound nodes carry tags such as `concat`, `frac`, `sqrt`,
   `rsub`, `around`, `with`, and document macros. This is the structure stored
   in an `.ath` file and shown by the Formula AST viewer.

2. **DRD and environment view**

   The document DRD supplies tag arities, child accessibility, syntax
   expansions, and inherited environments such as `mode=math`. Correction
   traversals consult this layer to avoid rewriting non-mathematical or
   non-correctable children and to remove redundant wrappers only when the
   inherited environment proves them redundant.

3. **Packrat serialized input**

   `packrat_parser_rep::serialize` flattens the relevant tree into a token
   string. It records bidirectional maps between source-tree paths and offsets
   in that string. Structural tags without a syntax expansion are serialized
   using tagged delimiters; layout-only or hidden structures may be ignored,
   unwrapped, or reordered according to explicit cases.

4. **Packrat grammar and memo table**

   `language/std-math.scm` defines grammar symbols such as `Main`, `Strict`,
   `Expression`, operator precedence levels, scripts, brackets, and tables.
   The C++ parser compiles these productions into token instructions and
   memoizes `(grammar symbol, input position) -> end position` results.

5. **Correction output tree**

   Correction functions return another native ATHENA tree. They do not return
   a packrat parse tree. Automatic correction replaces the original tree
   directly; manual correction can instead use the version-comparison UI to
   review the old and corrected source trees.

## User-visible entry points

- `math-correct-all` corrects the selected complete subtree or the entire
  buffer.
- `math-correct-manually` computes the same candidate, wraps source-tree
  differences with the version comparison machinery, and navigates to the
  first difference.
- Semantic editing wrappers intercept insertion and deletion. They attempt a
  modification, call `math-correct?`, and roll back when the result is not
  accepted. If needed, they insert `suppressed` placeholders so an incomplete
  formula remains editable without pretending to be valid.
- Semantic focus and selection use packrat source-range mappings. Correctness
  is indicated by whether the selected grammar symbol consumes the complete
  serialized input.

## Manual correction pipeline

`manual_correct` in `src/Data/Tree/tree_correct.cpp` applies these passes in
order:

1. `with_correct` normalizes adjacent and nested `with`-like structures and
   merges compatible runs.
2. `superfluous_with_correct` removes empty, environment-equivalent, and
   otherwise redundant `math`, `text`, and `with` wrappers using DRD-derived
   child environments.
3. `upgrade_brackets` converts legacy bracket structures into the current
   representation.
4. `misc_math_correct` repairs malformed script nesting, normalizes text inside
   scripts, moves trailing punctuation out of a `math` wrapper, and recomposes
   concatenations.
5. Optional `superfluous_invisible_correct` removes contextually invalid
   explicit spaces or multiplication markers and repairs a few malformed
   script/root forms.
6. Optional `homoglyph_correct` changes visual spellings to semantic symbols,
   for example `:` followed by `=` to assignment, a binary backslash to set
   difference, or a negated relation wrapper to the corresponding negated
   symbol.
7. The superfluous-invisible pass is repeated because homoglyph replacement can
   change token roles.
8. Optional `missing_invisible_correct_twice` inserts explicit multiplication
   or application spacing between adjacent basic tokens. Its decision combines
   symbol groups, operand shape, scripts, punctuation, and usage statistics
   gathered from the same tree.
9. Optional zealous correction repeats missing-invisible inference with a more
   permissive policy.
10. `downgrade_big` normalizes big-operator representation for the current
    document format.

The automatic load-time pipeline is similar but preference-gated and
version-sensitive. LaTeX import deliberately uses a more aggressive variant
and also repairs missing block/document wrappers.

## Correctness and semantic selection

`packrat-correct?` is a recognition test, not a theorem prover and not a type
checker. It is true precisely when the selected `std-math` grammar entry
consumes all serialized tokens. `Main`, `Strict`, and `Cell` are chosen from the
formula context. The parser's source maps allow semantic selection and focus to
translate recognized grammar spans back into native tree paths.

The grammar encodes syntactic categories and precedence. It does not resolve
identifiers, infer mathematical types, bind names to declarations, elaborate
implicit arguments, or produce proof obligations.

## Formula AST viewer

The **Inspect AST** command in the formula context menu resolves the semantic
root at the cursor and opens a floating ADS pane. The pane displays every
native tree node, child edge, child order, tag, arity, atomic value, and source
path. It supports panning, wheel zoom, explicit zoom controls, and fit-to-view.
Reopening the command updates the existing pane to the newly focused formula.

The viewer is implemented on a generic `ast_viewer_show_tree(tree, title)` C++
entry point, so later integrations can display other native or elaborated tree
representations without duplicating the graph UI.

## Consequences for Lean integration

ATHENA currently has a strong source AST and a useful syntax recognizer, but no
exported semantic parse AST. The first honest Lean integration should therefore
introduce an explicit intermediate representation rather than treating
packrat-success paths as one:

1. Preserve the native source tree and source paths as the editing authority.
2. Add a parser/elaborator result whose nodes represent application, binder,
   operator, literal, annotation, and error constructs and retain source spans.
3. Resolve ATHENA symbols and user declarations into names in a controlled Lean
   environment.
4. Translate that IR to Lean syntax/terms and retain a reverse map for errors,
   goals, and hover information.
5. Keep correction separate from elaboration: correction may propose source
   rewrites, while Lean feedback must never silently rewrite the document.

The existing packrat serializer and path maps are reusable for source-location
tracking, and the Formula AST viewer is reusable for inspecting the future IR.
The missing component is the semantic AST itself, not another graphical view or
another correction heuristic.
