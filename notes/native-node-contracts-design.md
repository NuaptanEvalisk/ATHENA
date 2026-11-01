# Native Node Contracts: Current Design And Cutover

Updated: 2026-11-01. The bounded DRD replacement is complete. This is a current
architecture entry point, not another migration assignment. Detailed protocols
and dated evidence are in [the implementation README](../src/Data/NodeContracts/README.md);
historical remaining-work lists there describe their original checkpoints.

## Completed Boundary

- `src/Data/NodeContracts/` is the native authority for fields, declarations,
  bindings, inference and groups. Static declarations belong in
  `node-fields.json` and `node-groups.json`.
- The old DRD directory, `drd_info`, `std_drd`, `drd_update`,
  `restricted_exec` and `typeset_exec_until` are retired.
- Editor and conversion consumers use source-owned prepared contexts, not a
  query-time evaluator or an unrelated ambient cursor environment.
- Optional analysis consumes detached inputs. Owner capture/publication is
  bounded and rejects stale or partial work; closing owners stops publication.
- The final integration and deployment completed on 2026-10-31. The recorded
  integration probe covered syntax changes, real BufferActor analysis,
  supersession, local rules, isolation and queued close.
- Removing all remaining Scheme from ATHENA is a separate project.
  Neither a new document format nor the proposed `.ac` cache was required.

The persisted `drd-props` spelling remains declaration input decoded by C++.
It is not evidence that the old DRD runtime survives.

## Responsibilities

1. Structure contracts name fields separately from physical storage: child
   slots, repeated children and typed metadata properties. Do not create a
   second validator beside the authoritative native v3 codec.
2. Editing policy governs cursor stops, insertion, deletion and selection.
   Native mathematical/table operations consume contracts; contracts are not
   a new scripting language.
3. Presentation describes inline/block behavior, child environments and
   source-to-render mappings. Styles do not rewrite core structural invariants.
4. Groups and transformations provide categories and ordered alternatives.
   A kind change with a different structure requires an operation, not a blind
   tag rename.

Content, editability, search participation, radioactive matching and source
identity are separate concepts. An editable semantic title need not have its
own UUID or participate in radioactive matching. Peek/transclusion permissions
belong to view context, not shared descriptor mutations.

## Definitions And Declarations

The normal evaluator is the definition authority. A prepared checkpoint pairs
values/presence, callable binding identity, authored declarations and their
resolution receipts. Analysis must not discover definitions by running macros
or scanning an arbitrary GUI environment.

Declaration contributions belong to actual source executions. Replacement or
deletion retires the old producer, including invalid replacements; an earlier
surviving contribution may become effective again. Equal-looking executions
are not interchangeable producers.

A rule declared before its matching callable remains scoped pending input.
Rebinding does not accidentally inherit a previous binding's attached rule.
Package defaults, local overrides, groups and ordered inputs retain their
provenance. Cache reuse must validate the consumed inputs and cannot be based
only on a merged value map or macro name.

Native/authored facts and optional inferred facts are separate. An inference
result cannot modify the author's declaration, become authority for a different
binding, or survive a changed dependency merely because its name is unchanged.

## Prepared Occurrences

Source paths alone do not identify repeated or folded displays. Physical hits,
cursor movement and commands use the selected source occurrence and its admitted
environment/contract checkpoint.

Missing preparation is explicit pending state, not permission to evaluate
source from a getter, approximate a prefix, or borrow the last cursor's state.
The last rendered frame can remain visible while a replacement is prepared,
but it does not authorize edits through an obsolete mapping.

Actual admitted display positions can establish editable locations without
waiting for optional field-wide inference. Native structural restrictions and
authored visibility still apply.

## Asynchronous Analysis

Workers receive detached immutable inputs, never editors, live mutable trees
or owner-affine runtime resources. Owners freeze inputs in bounded slices and
publish only complete results for matching demand, bindings and receipts.
Superseded/closed requests do not publish.

Exact inference admission is constant-time. Older evidence may inspect at most
128 immutable dependency actions; over-budget evidence stays unavailable until
background publication. This delivered bound supersedes the early draft's
absolute ban on any dependency checks in getters. Admission must not execute
source, scan an unlimited closure, schedule inference or expose partial facts.

Native execution-state routing used by groups, program highlighting, syntax
parsing and UI translation is not another descriptor authority. It must not
become a fallback for an unrelated source position.

## Maintenance

Work on concrete current defects rather than restarting completed cutover
stages. Build/deploy and testing policy are in AGENTS.md. Focus evidence on
uncertain ownership, invalidation or representation boundaries; do not revive
the retired acceptance backlog or require another Notes migration.

Real-window IME validation is complete, confirmed by the user on 2026-11-01.
The older keyboard commit's outstanding-validation note is superseded.
