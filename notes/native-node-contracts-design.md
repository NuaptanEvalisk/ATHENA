# Native Node Contracts: Design Decisions And Cutover

This is the implementation entry point for replacing DRD. Detailed semantics,
analysis transfer rules and implementation evidence live in
[`src/Data/NodeContracts/README.md`](../src/Data/NodeContracts/README.md).
This document does not declare the replacement complete. It makes the next
ownership changes explicit rather than adding another independent subsystem.

## Current Checkpoint: 2026-10-30

### Bounded Delivery

The current delivery stops at the source-owned editor/conversion consumers,
wrapper correction and source-value preparation documented below. Commit these
working changes now; do not hold them for the entire DRD replacement. The
owner-side analysis capture slice is committed separately as `d8bbbfec2`.

The first bracket/symbol experiment was withdrawn from that delivery. Its
targeted check exposed a custom macro used as a delimiter being normalized in
isolation before the surrounding token sequence was processed. The old matcher
also cannot recover a delimiter glyph from an arbitrary macro invocation.
The later source-owned bracket implementation described below separates symbol
classification from lossless delimiter rewriting. The failed experimental
probe is not acceptance evidence for either delivery.

The follow-up work is divided into independently deliverable changes:

1. Completed: explicit contexts for the remaining math correctors and syntax consumers.
   Completion: those named consumers no longer borrow ambient state; detached
   imports and editor commands retain their own source environment, and the
   concrete token-provenance case passes. Do not redesign mathematical parsing.
2. Scheduled/incremental editor math statistics. Completion: input getters do
   not synchronously traverse the whole buffer; edits invalidate the matching
   result and obsolete work cannot publish.
3. Analysis capture/publication responsiveness. Completion: the known bulk
   owner-side snapshot/diff/freeze/retirement steps no longer run as unbounded
   work in an interactive request. Preserve cancellation and exact-demand
   publication; this is not a hard-real-time guarantee for every worker or
   standard-library operation. Explicit offline preparation may be synchronous.

For each change, stop after its concrete exit conditions, build the production
target with `-j16`, use a focused runtime check only where the ownership or
representation boundary is uncertain, and commit. Final deployment is a
separate integration step, not justification for accumulating more changes.

ATHENA's end state is native C++ without Scheme. Prefer implementing new
behavior and new callers directly in C++, rather than growing the Scheme API
or adding Scheme logic that will need another rewrite. Bindings needed by
existing callers may remain as a transitional integration surface, not the
default architecture for new work.

The closed delivery builds successfully with `-j16`
(`/tmp/athena-native-context-delivery-build.log`). The source-value/runtime
evidence below applies to the retained implementation; the withdrawn bracket
experiment is not included in the delivery or counted as passing validation.

### Implemented State

Homoglyph and invisible-operator correction now use
source-owned C++ stages. Each stage prepares its own rewritten input, keeps
token occurrence paths across recomposition, and distinguishes source tokens
from syntax substituted from a definition. Existing invisible-correction
Scheme names now dispatch to native editor commands; there is no new Scheme
implementation. Its production build and runtime evidence are recorded below;
the subsequent bracket delivery is described separately.
Production built with `-j16` (`/tmp/athena-native-invisible-build.log`); the
targeted source/actor check passed at 23:16
(`/tmp/athena-source-invisible-probe.log`). This batch is ready to commit
independently of bracket correction and is committed as `f81552dee`.

Bracket and big-operator correction now share the source-math preparation
helper. Detached documents retain their initial/preamble environments; native
editor commands use the attached source entry or proposal insertion context.
Legacy bracket movement reconstructs the parent source before preparing its
changed math child, retaining surrounding assignments and WITH bindings.
Custom delimiter macros remain intact when the delimiter writer cannot encode
them losslessly; their local syntax still participates in symbol classification.
The ambient symbol/eligibility facades and free document-context fallback are
removed. Existing Scheme names use generated native editor bindings, with no
new Scheme implementation. Scheduled statistics and analysis responsiveness
remain separate deliveries, not additions to this bracket batch.

The bracket batch built with `-j16`
(`/tmp/athena-native-brackets-build.log`). Its targeted runtime check passed at
23:34 (`/tmp/athena-source-brackets-probe.log`), including the earlier failed
custom-delimiter representation case, local big-operator classification,
parent context after bracket movement, and both existing generated bindings.
No additional permanent tests were added. Final deployment still follows the
separate statistics and analysis deliveries.

The committed baseline through `5ddc3edeb` has removed `Data/Drd`, the eager
editor-wide `prepare_source_contracts` API, and the obsolete offline document
contract cache. Source identity now selects exact BODY-relative entries lazily,
including authored preamble state, for live transactions and detached/Vault
sources. Do not restart those removals from the historical transition notes.

The current batch moves document/paragraph/enclosing-node cursor boundaries to
admitted per-node navigation and uses retained entry contexts for dynamic
argument deletion, wrapper removal and hybrid draft editing. Prospective
selection wrappers and native math blocks prepare their own destination before
cutting the selection. Compound insertion now resolves the actual selected-content
destination, including table cell context; explicit insertion endpoints receive
structural correction instead of being reinterpreted against ambient contracts.
Whole-node selection/cut and structural-search boundaries also use explicit
source preparation, while selection-environment reads stay admission-only.
Generic mutation-oriented `go_to_start/end` now explicitly prepare the selected
source subtree; default tree insertion prepares its prospective tree in the
insertion context. Atomic endpoints use literal offsets. This command API does
not change the context policy of passive queries. Direct mathematical boundary
calls now also use explicit source or prospective-wrapper preparation, with
literal offsets for empty/atomic slots. Scheme start/end boundaries now have
separate passive and command APIs, as detailed below. Field-environment and
generic search Scheme facades have now been removed. TeX mode statistics now
prepare their own source entries; the last implicit native field-environment
utility and `cursor_execution.cpp` are removed.
Clipboard extraction uses a source-addressed provider, including metadata
completeness and source-root stripping. Complete-object cuts follow extraction's
ancestor choice instead of retaining the copied identity on an empty field.
Spelling reads consume atomic source ranges, not structural clipboard extraction.
ProofTrack geometry and whole-document selection visibility use admitted-only
boundary queries rooted at the queried subtree, not an unrelated document
root. Heading section ranges now describe whole source nodes; their geometry
is obtained from current boxes without navigating an offscreen section or tail.
Image resize retains its selection context before changing the source.
Editor construction and observer-position recovery use native structural
correction; Vault search uses the immutable native source policy.
The command-local entry cache also now distinguishes root and nested subtree
addresses: cursor-position ordering is not a valid map comparator for those keys.

Folded screen containers and ToC replacements now retain explicit source/display
pairs at the projection owner. Context admission resolves those pairs without
relaxing occurrence identity checks or executing source. This fixes the absent
document-root context that otherwise made valid formula selections pending.
Generic focus/parameter fields, hidden-field editing and structured argument
navigation now select the target's prepared entry or the detached proposal's
caret context. Missing layout remains unavailable; these getters do not prepare
source. The duplicate ambient field-name implementation has been removed.

Scheme label/arity/accessibility/name/type queries now use the same prepared
source-or-caret selection. Unavailable entries return false/empty UI data or
`-1` for an unknown arity/insertion point. The graphics completeness predicate
does not treat an unknown maximum as a completed object. The old global
property-query implementations are removed. Post-deletion editable-content
checks now use native field rules directly and prepare explicit source entries
only for non-native nodes. The runtime property check also exposed an eager
`in_source()` read on ordinary consecutive deletion; that mode query now occurs
only in the relevant symbol/MathGrid branch and is explicitly command-prepared.
This batch built with `-j16` and passed its focused real-actor check at 12:57
(`/tmp/athena-source-properties-probe-final.log`), including runtime property
bindings, opposing attached/prospective scopes, pending queries after mutation,
and consecutive deletion without intervening layout.

Scheme `path-start/end` now read the requested source entry and return `#f`
when unavailable; they do not prepare detached templates. The explicit
`command-path-start/end` interfaces prepare attached source or a complete
prospective template under its insertion context. `path-in` retains the whole
template while descending, so a nested macro sees outer local field rules.
Selection wrappers compute their destination before cutting. Label jumps use
command boundaries; reference previews, hover outlines and AUDMAP paragraph
positions remain admission-only. AVD range defaults use each source editor;
history hints use native structural rules and structural differences mark
whole-node borders. Shifted-caret queries use admitted source navigation.
Production built with `-j16`
(`/tmp/athena-native-boundaries-final-build.log`). The focused binding/template
check passed at 13:58 (`/tmp/athena-source-boundaries-probe.log`), including
opposing local/caret policies, relative coordinates and stale-source pending
versus explicit command preparation. The subsequent peripheral consumer edits
were built, not subjected to an unrelated regression suite.

Owner-side analysis input materialization now yields after 128 names or roughly
4ms. Further changes are merged before the next slice, unresolved dependencies
retire old inferred facts, and only a complete current demand is submitted or
published. This does not bound initial name-diff enumeration, an individual
tree freeze, or stale-inference retirement. Those and remaining compound command
preparation are still open; removing the directory is not completion of the
whole design. Build/runtime evidence belongs in the implementation README.
Initial capture now seeds from inherited analysis inputs and compares current
binding/declaration receipts, including removed labels. It no longer discards
already analyzed style facts merely because a new owner starts capturing inputs.
This fixed a concrete cold-layout regression where `math` had zero accessible
fields and selection correction promoted a substring to the whole formula.

Scheme cursor movement now belongs to the active editor: valid-position, word,
node, tag and argument traversal select each visited source entry. Root-tree
queries are restricted to the editor's buffer while retaining absolute result
paths; attached subtree queries retain relative paths. Missing or stale layout
returns the original position without evaluating source. The tag helpers also
use per-node queries instead of an ambient contract snapshot. The unused global
word/node/tag/argument wrappers have been removed, and the multiparagraph-delete
exit branch uses the same source-aware movement. Bindings are generated from
`editor.xml`, not handwritten glue. Cursor path accessibility now stops before
the terminal text offset/border; treating that component as a field previously
made ordinary movement enter source mode and expose hidden arguments.
Production built with `-j16`, and the focused runtime binding check passed at
12:33: opposing local field rules, all migrated Scheme movement entrypoints,
cross-field traversal, relative subtree coordinates and unavailable edited
source. See `/tmp/athena-source-navigation-probe-final.log`.

The preceding focused math/heading real-actor check passed on 2026-10-30
at 12:03. Global start/end, cursor correction/accessibility, valid/accessibility
movement and generic search wrappers are now removed. Retained tests explicitly
select native rules or their editor's command context. Six retired Scheme
bindings are absent from the running binary. `tree-search-source` remains as
an explicitly native, detached-source operation; the existing structured-math
search checks use that operation rather than preserving an ambient facade.
Graphical-text contiguity now belongs to source-addressed navigation and keeps
subtree-local endpoints. Scheme geometry, Qt AVD selection and UUID jumps also
use passive or command-owned boundaries as appropriate. The final production
build is `/tmp/athena-native-utility-callers-build.log`; the focused binding and
source-policy runtime check passed at 14:34
(`/tmp/athena-native-utility-probe.log`).

TeX mode statistics now traverse the original document body, with its own
style, initial values, preamble and source URL. They request exact source entries
only for custom nodes; native nodes use immutable contracts. Definition bodies
are not counted as uses, and editor accessibility does not filter export uses.
The detached document preparation helper now applies initial values before the
preamble, matching the editor's initialization order. The unused standalone
`latex-export-init-mode-stats` binding and the last `get_env_child` /
`current_cursor_navigation` chain are removed.

The focused runtime check passed at 15:07 on 2026-10-30
(`/tmp/athena-conversion-context-probe.log`). With opposing active/source macro
rules, a source math use emitted a direct parameter and a source text use
emitted `ensuremath`; changing the ambient context did not change the source
output or mutate the caller's contracts. The retired binding is absent.
The subsequent default-input adjustment passes the export driver's normalized
style list and initial collection to statistics, rather than raw preservation
metadata. Its production build log is
`/tmp/athena-native-conversion-context-final-build.log`.

Verbatim conversion now distinguishes expanded/native formatting, explicit
source-position formatting and detached source excerpts. Search workers use
the latter without loading styles or running evaluation. Metadata getters
retain source paths and return unavailable rather than partial text when a
custom-node entry is missing. Direct text exports prepare their own document
or use their supplied editor for snippets. The focused path/pending check
passed at 20:42 on 2026-10-30
(`/tmp/athena-verbatim-context-probe.log`), following the successful `-j16`
production build `/tmp/athena-native-verbatim-build.log`. The subsequent
argument-formatting adjustment retains unwrapped argument text until the
outer paragraph's wrapping pass; it is included in the compression build below.

AI compression now belongs to the calling editor. Attached input uses its
source positions; detached input is prepared under the explicit insertion
context, retaining local rules inside the complete proposed tree. Native nodes
use immutable contracts. Its four existing Scheme names now bind editor methods
through XML. Each editor owns its compression dictionary; retained templates
and exposed content are detached copies, metadata-bearing formatting nodes keep
their identity through HTML, and foreign placeholders fail explicitly instead
of resolving to another editor's data. The final `-j16` production build is
`/tmp/athena-native-compression-snapshot-build.log`. The focused runtime check
passed at 20:59 (`/tmp/athena-compression-owner-probe.log`), including runtime
bindings, attached/prospective local rules, tree/HTML round trips, dictionary
isolation and subsequent source mutation.

There are no direct `current_evaluation_state` reads in `Data/Convert`. This
does not mean every conversion path is independent: shared tree utilities still
contain implicit reads. Detached correction's `prepare_document_context(tree)`
can borrow an editor state merely because it defines `theorem`; its math
transforms are concrete remaining consumers, not a
reason to add fallback contexts. Large-input preparation/admission also remains
open. The strict cutover is not deployed or declared complete.

Live node/downward/upward correction now belongs to the source editor. Its
three existing Scheme names are generated from `editor.xml`. Native nodes use
native contracts; custom nodes prepare their exact current source positions.
Upward correction stops at the buffer root. Detached/foreign mutable inputs
fail explicitly instead of borrowing another document or partially mutating a
temporary handle. Only native/authored arity may erase invalid unannotated
markup; optional inference cannot authorize deletion, and annotated content
remains for owner validation. The lower tree helper accepts an explicit query
and no longer reads ambient state or the global document root.

The with-like/type/similarity getters now read each operand's admitted context,
while format insertion/toggling uses explicit command preparation. Comparisons
retain each operand's source rules and compare definition content for same-name
custom macros. They do not compare execution receipt pointers: independent
cold preparations can allocate different bindings for the same source. The
offline wrapper normalizer was a separate remaining consumer, migrated below.

Production built with `-j16`
(`/tmp/athena-native-correction-definition-build.log`). The focused real-actor
check passed at 21:25 (`/tmp/athena-source-correction-probe.log`), including
six runtime bindings, opposing local/outer arities, all correction directions,
annotated-source preservation, source/prospective wrapper queries, different
definitions and cold preparation after editing. No deployment, commit or Notes
writes.

Mathematical input statistics now have editor-local buffer/cursor caches.
`math-stats-update` and both count getters bind editor methods; the global
`math-stats-compile` entry and Scheme URL/paragraph-count/source copies are
removed. Traversal retains source paths and asks only for admitted custom-node
contexts, while native nodes use immutable rules. Missing entries publish no
partial count map. Macro definitions are not counted as uses, and retained
count keys are copied rather than aliasing mutable document leaves.

Cache identity includes the successful execution base and contract trace.
Source/environment invalidation clears counts before layout; a new layout or
optional-analysis publication invalidates them, and moving to another root
paragraph refreshes the cursor cache independently. This fixes same-paragraph
edits that the old paragraph-count heuristic missed. It does not yet bound the
first synchronous statistics traversal: large-input incremental/scheduled
statistics work remains required before final cutover deployment.

Production built with `-j16`
(`/tmp/athena-native-math-statistics-build.log`). The focused temporary check
passed at 21:44 (`/tmp/athena-math-statistics-probe.log`): exact source paths,
opposing local mode declarations, pending-result atomicity, owned count keys,
runtime bindings, two-actor isolation, cursor-paragraph changes and edits
without changing paragraph count. No permanent test or broad suite was added;
no deployment, commit or Notes writes.

Wrapper normalization now has explicit source providers too. The two wrapper
passes no longer use ambient queries; the no-context `is_with_like` and old
`with_decompose` helper are removed. Recomposition retains each custom wrapper's
original context with its output identity, so flattened/new paths are never
used to query the old source and equal-looking wrappers with different
definitions do not merge. Definitions and quotation templates remain code,
not executed wrapper occurrences. Missing requested source contexts fail
explicitly before any source mutation.

Detached document correction prepares the document's style/initial/preamble
and original BODY entries, preserving the envelope. Standalone snippets use an
explicit generic document, not the caller's environment; document snippets keep
their paragraph structure for forward entry preparation. The generated
`with-correct`, `with-correct-superfluous` and `manual-correct` bindings now
dispatch to editor commands with Scheme exception conversion. They prepare the
actual attached source entry or detached insertion context. The manual command
captures source math mode natively and supplies a fresh provider after each
wrapper rewrite. Its later math passes have not yet been migrated.

Production built with `-j16`
(`/tmp/athena-native-wrapper-correction-final-build.log`). The focused temporary
runtime check passed (`/tmp/athena-wrapper-normalization-probe.log`): local
definitions, rewritten-wrapper provenance, metadata, quotation preservation,
missing-context errors, detached document rules, all three generated bindings,
source/proposal distinction, source math mode and cold preparation after edits.
No deployment, commit, Notes writes or permanent tests.

Redundant-wrapper correction now consumes the actual source-entry values paired
with its contracts, including initial mode, preceding assignments and local
macro scope. Contract-only preparation still captures only contracts; value
consumers explicitly request full checkpoints. Root and backward requests do
not borrow the forward paragraph state. Assignment expressions and WITH binding
parameters remain code rather than display children. Manual correction applies
its temporary mode adapter only after the wrapper passes.
Production built with `-j16`
(`/tmp/athena-native-source-values-final-build.log`), and the targeted actor check
passed (`/tmp/athena-source-values-probe.log`), including math-stage execution
after wrapper removal and cold preparation after changing an earlier assignment.
No deployment, commit, Notes writes or permanent tests. Remaining mathematical
correction/symbol expansion and bounded analysis/statistics work are still open.

Compound clipboard text is now generated on each source actor before merging
the XML pieces. Qt receives owned XML/text strings; it no longer interprets the
merged raw source with a UI-thread contract context. Production built with
`-j16` (`/tmp/athena-native-compound-text-build.log`). The two-actor check passed
at 15:31 (`/tmp/athena-compound-text-probe.log`): identical source macro names
with different per-document initial definitions retained both textual meanings,
while their native XML still retained the unexpanded macros.

## Decision Summary

The replacement is **Native Node Contracts**, not a C++ transcription of the
Scheme DRD tables. Its authority is a prepared, immutable view selected from
normal evaluation. The following decisions govern the implementation sections
below; historical implementation records are evidence, not additional policy.

| Decision | Consequence |
| --- | --- |
| V3 structure is authoritative independently of styles | A style or an inferred macro contract cannot legalize an invalid mathematical node or expose its structural arguments for deletion |
| Static descriptions are data; behavior is native code | JSON holds field/category descriptions; compiled editor operations own mutation; no JSON procedure dispatch |
| An evaluated binding is not its name or its value | Equal-valued reassignment can change local-rule applicability without changing expression-analysis facts |
| Contributions retain their owner and attachment | Source replacement withdraws old fields, groups and diagnostics together; merged descriptors cannot be the source of truth |
| Normal evaluation prepares contexts | A getter never runs a prefix of the document, loads a package or schedules inference |
| Native editing does not depend on optional inference | Typing, deletion and navigation use native policies and valid source/display mappings |
| Background results are proposals | The owner validates observed dependencies before publication; workers never mutate editor state |
| Compatibility is at the source boundary, not the old API | Existing v3/style syntax remains readable; the positional DRD facade is removed, not preserved as a parallel authority |

The first deliverable is correct ownership and preparation, not another worker
or descriptor abstraction. Keep implementation in the existing evaluator,
bridge, editor and native field owners. Do not introduce a general event-sourced
application framework: retained records cover only contract-relevant effects
and their admission inputs.

### Implementation Status At This Revision

| Area | Observed state | Still required |
| --- | --- | --- |
| Static fields/groups | Native storage and JSON descriptions exist; bootstrap constructs a process-wide immutable native baseline directly, with thread-private parser code maps; `std_drd`, `tag_info`, its tuple codecs and obsolete DRD descriptor setters are removed | Complete shared schema use and remaining positional consumers |
| Definition/declaration state | Paired checkpoints, histories, package origin, binding applicability, scoped pending attachment and source execution outcomes exist; normal bridge replacement handles invalid declarations | Complete ownership/receipts across the remaining preparation modes |
| Replay | Variable/descriptor/binding/group/syntax, reference/attachment and package-source reads are tracked; actual WITH_PACKAGE execution has scoped restoration | Complete derived-state and external-input receipts where required |
| Preparation | Atomic/compound entry and exit contexts are indexed during real execution and admitted replay; prepared queries no longer reconstruct event prefixes or copy complete variable maps. Physical hits carry occurrence tokens through Unicode shaping and admitted reuse, including changed roots and merged spans; completed progressive prefixes and root outer boundaries are queryable, incomplete nodes have no exit context, and edits invalidate the execution base. Metadata inputs receive detached matching values/contracts before initialization, with live slot refresh; explicit conversion and interactive graphics evaluation restore matching state in temporary scopes | Cold-position preparation, newly edited fields and pending behavior for non-admitted occurrences now exposed by strict reads |
| Inference | Detached workers consume immutable binding/value/declaration resolutions; recursive groups and their callers require matching receipts; preparation retires stale published facts while new results are pending; analysis status is separate from schema types; exact admitted display hits can enter macro fields without waiting for inferred access, while authored/native restrictions still apply | Budgeted large-input capture/admission and remaining pending-state consumer policy |
| Consumers | Native cursor navigation, search and selection receive explicit access/writability policy; all DRD read getters, global access modes and `drd_update` are removed; identity planning takes an explicit native scope; the refcounted `drd_info` owner is removed, and editor/evaluator share one native variable/contract state; menus no longer request analysis; cold identity preparation has its own style/init/preamble context, while metadata buffers do not create a document identity graph. `restricted_exec`, `typeset_exec_until` and the boundaryless path cache are removed; source authoring has a separate signature index | Complete missing-occurrence preparation and remaining ambient query selection before deployment |

The worktree now rejects unprepared environment reads instead of reconstructing
an approximation. The removed path scanned only ten preceding siblings, ignored
some table scopes and supplied an unscoped source-definition catalogue. Its
removal is not evidence that all callers are ready: live spelling now consumes
admitted layout contexts with explicit pending branches, while several compound
command lifecycles still need explicit preparation. Do not deploy this intermediate
cutover merely because the production target builds.

Source-authoring discovery now has a separate owner-prepared signature index.
It records literal top-level assignments without executing their expressions,
updates affected paragraph entries, and restores earlier definitions when a
shadowing assignment is removed. Completion and hybrid insertion use signatures
only in source mode; runtime bindings and source-identity roles remain separate.
The input's outer invocation context selects this mode, not the name field's
display-only text mode. Verification status is recorded in the implementation
README.

Native LaTeX commands capture auxiliary prefixes and compound insertion context
before consuming their draft. Native keyboard shortcut chains retain their
original insertion checkpoint before cancelling the previous visible result;
only their compound insertion actions consume it. Prospective argument fields
still require their own evaluation. The retained checkpoint is not a blanket
replacement for queries in newly created fields or Scheme command bodies.

IME draft replacement and commit explicitly prepare the restored source through
the normal owner layout path after cancelling the pre-edit marker, before mode
routing or Scheme insertion. Text and math replacement/commit transitions have
passed a temporary real-actor check; this does not establish GUI composition
timing or remove the other missing-context lifecycles.

Interactive environment refresh now requests detached analysis instead of
running `synchronize` inside `typeset_preamble`. Cold style/source identity and
offline preparation still use explicit synchronous analysis. Command/focus
snapshots remain unavailable without an admitted cursor context; publication
does not infer a mode by scanning source. Native action dispatch prepares dirty
layout before checking those snapshots, matching keyboard dispatch. The right
footer retains its last prepared field description while document statistics
remain independent. Page-dimension getters read committed layout geometry, not
the currently selected field's evaluator.

Hidden-source search and manual spelling now explicitly prepare candidate
positions before comparing their mode and language. They reuse an admitted
source ancestor when available, otherwise prepare from the document's initial
environment with the native evaluator. The isolated preparation does not publish
its reference, auxiliary or attachment outputs, or register a display occurrence.
It is command preparation, not a fallback in environment getters. Candidate
context reuse is limited to one unchanged atomic leaf in one synchronous scan;
an unreachable source field reports failure rather than inventing a context.
The legacy search/spelling stepper and previous/next-compound lookup now choose
accessibility from each visited source node's entry context, with command-local
reuse for sibling queries. The native search bar now explicitly supplies this
preparation to the structured matcher too. Cascaded matching and result-border
correction carry the physical source path through every descent; they do not
identify contexts by a shared tree object or by the query pattern. The ordinary
single-context cursor/search APIs do not gain an implicit preparation fallback.
Pending inferred fields and a bound on prefix execution remain separate
obligations.

Live spelling reads admitted source-entry contexts without executing hidden
prefixes. Mode/language come from actual layout, and parent field permissions
remain native/authored-authoritative. Missing or ambiguous contexts park only
the affected branch while retaining its committed marks. New layout receipts
are reviewed incrementally, allowing an earlier definition to invalidate later
untouched blocks without rescanning unchanged admitted blocks. A temporary
real-actor probe verified native math/text scopes, local macro language/access,
forward invalidation and pending/reuse behavior; production built with `-j16`.
The old unused recursive spelling entrypoints and Scheme exports are removed.
This does not complete the other compound preparation or owner-side capture
obligations, and the strict-cutover binary is still not deployed.

Dynamic argument edits now prepare the target invocation's entry context, not
the caret field's context. An argument can locally rebind the same macro, so a
valid caret checkpoint is not authority for its enclosing invocation's arity
or field permissions. Insertion/removal retain the invocation checkpoint across
mutation for argument navigation. Source-mode policy is retained over that
operation as well. Macro-input argument shortcuts retain the draft and report
pending layout instead of consulting ambient contracts when their invocation
context is missing.

The existing explicit source-preparation algorithm is shared by search and
these editing commands through `prepare_source_position`. It first selects an
admitted context, otherwise evaluates from a prepared ancestor or document
initial context in the existing isolated source evaluator. Its optional root
memo belongs to one unchanged synchronous search; editing commands do not
retain it across mutations. Environment getters still do not execute source.
Verification for this slice is recorded in the implementation README.

Upward structural deletion now prepares the entry of each node being removed,
and repeats preparation after an inner removal invalidates layout. It no longer
passes one caret-field contract through ancestor recursion. Native structural
cases remain independent of inferred border policy. Opposing local border
declarations and consecutive ancestor removal passed a temporary real-actor
check. Selection enlargement likewise reads each selected ancestor's entry;
text enlargement reads the selected text's mode, and formula-root discovery
queries each inspected wrapper's own entry. Those command changes are separate
from recursive selection correction and the other ambient consumers. Selection
geometry is also requested during repaint and cannot use cold prefix execution
as a substitute for admitted contexts.

The selection correction algorithm now advances an explicit navigation provider
when entering a subtree, including border checks and script-adjacent endpoints.
Source-only range highlighting skips custom border promotion and uses the
immutable native baseline, not the current caret's contract scope. The unused
two-path geometry overload carrying another ambient fallback has been removed.
The normal selection getter now supplies a per-node admitted-context provider.
Missing context returns pending without publishing partial endpoints or
executing a source prefix. Menu snapshots, focus rectangle selection correction
and scroll checks consume that result; commands requiring definite boundaries
stop with an explicit awaiting-layout error. A retained table-command checkpoint
selects source-editing policy only, not contracts for other nodes. No automatic
command replay is claimed. The unused ambient tree-selection entry is removed; its one existing
test call now passes explicit navigation.

Focus lookup now exposes pending state to command and toolbar snapshots. Hidden
field visibility uses the focused invocation's entry, not a local declaration
inside its caret field. Focus outlines share the read-only admitted navigation
provider with selection correction, traverse relative to the document root and
publish geometry only after the whole calculation succeeds. This removes the
focus path's ambient fallback without evaluating source during repaint. A
temporary real-actor check covered opposing invocation/caret visibility and
unprepared selections; production built with `-j16`. The source-only selection
geometry overload remains independent of these pending contract queries.

Physical hits now select contexts through their box path and source-occurrence
ancestry, independently of `tp`. The per-node navigation provider accepts this
explicit display anchor; physical correction no longer uses a single hit-leaf
scope or an ambient fallback for every ancestor. Unadmitted hits return pending,
not a cursor position. Physical movement preserves its pre-step coordinates on
pending, and mouse selection/paste consume an explicit movement result rather
than acting at the old cursor. Source-only region dispatch stays independent of
field admission so it does not suppress scroll or existing resize completion.
Implementation evidence is recorded in the README.

Keyboard logical compensation and initial cursor correction on resume now use
the same admitted per-node navigation. A pending logical correction rolls back
the physical candidate, including its occurrence and ghost cursor, before
publishing any selection change. Explicit hidden-source jumps use native group
policies to reveal hidden, folded and switch content, preparing the actual
source entries rather than interpreting a missing display context as hidden.
Mutation-observer correction uses the immutable native field baseline before
new layout exists; it does not execute a source prefix on every edit.

This does not finish cursor command boundaries: `go_to_start/end` and their
paragraph/document variants still call implicit tree-navigation helpers, often
from compound mutations. They need command-owned contexts, not a cold source
scan per keystroke. Text completion now scans admitted per-node entries in
resumable actor-owned slices. Its word index retains per-paragraph contributions
and layout receipts: unreviewed or invalidated contributions are excluded until
readmitted, including later paragraphs affected by preceding declarations.
Displayed fields can supply occurrence evidence while optional inference is
pending, without overriding authored visibility. Candidate lookup does not
evaluate source or tokenize the document. Deferred popup requests retain their
exact source position/text and are cancelled on input or cursor changes.
The subsequent source-identity cutover and cooperative capture work are recorded
in the current checkpoint above. Remaining mutation endpoints and fully bounded
analysis capture/admission are unfinished. Implementation and deployment evidence
are recorded in the README.

Native delimiter, accent and variant content now uses the same environment
scopes in layout, complete evaluation and both partial-evaluation routes. This
does not authorize partial evaluation from a getter. MathBlock display scopes
and MathGrid row numbering/label scopes are now shared by layout, AVD
semantic projection and explicit source-position preparation, including macro
argument traversal. Semantic grid execution also tracks the physical cell
column. Direct table, row and cell layout also records its source
boundaries, including wrappers that bypass the generic concater. Cold/newly
edited table-field preparation still remains separate work.

Macro completion selection now captures the invocation context before replacing
the name. Argument preparation and recursive activation retain that bounded
context, and command lookup happens before the replacement. The name field's
display-only text style is not the invocation environment. General compound
commands and command actions that query a newly created field still need their
own prepared inputs; this is not a blanket pre-edit environment fallback.

Existing table edits retain the actual table-entry checkpoint for their bounded
mutations. Format queries no longer insert a source wrapper; nested TFORMAT and
subtable layout record their inherited format contexts. Table macro construction
now prepares the prospective argument through the existing native evaluator
before its own source mutation, using separate reference/auxiliary outputs. It
does not equate the call-site environment with the macro argument environment.
Subtable insertion prepares empty cell bodies from the parent cell's effective
block/hyphen rules. Table queries now require an admitted entry or the explicit
command-local table context; their legacy variable-query fallback is removed.
Multi-property commands retain that context across dimensions, padding, borders,
spans and selected-cell formatting. They also retain the invocation cursor's
selection navigation policy for restoring selections after mutation, separately
from the table entry and without substituting it for new-field evaluation.
Missing-context continuations remain open.

Native clipboard paste now has owner-local prepare/apply data: the validated
payload, target mode/language, move provenance and available table-entry context
are captured before a compound range cut. Table destinations are associated with
their retained source objects, including enclosing tables, rather than stale
numeric paths. AVD replacement applies that data
directly, without republishing a temporary clipboard or rereading the mutated
target's mode. Ordinary cross-mode content is inserted with its wrapper in one
operation. Local balloon, marginal-note and geometry wrapping uses the shared
`var_insert_tree` path, which now prepares the prospective field
through the native evaluator before cutting its selection, validates the local
transfer and retains the table entry where the template introduces a table.
Partial evaluation reports whether it actually reached the field. Table cell
mode/language bindings share their construction and scope with layout rather
than discarding CWITH during preparation. Non-graphics paste no longer queries
legacy mode/language on an unprepared target. Pending targets and range cuts that
change the insertion owner's semantics still require completion.

Transparent source ancestors retain a lightweight boundary lineage from actual
evaluation, so the selected display leaf can disambiguate ancestors even when
table layout records both rigid and lazy occurrences. Each ancestor boundary is
checked at the current source address before use; the lineage does not retain
parent source trees or argument environments. Compound insertion now declines
before mutation when its invocation context is unavailable. Hybrid activation
consumes its input only after compound/table preparation succeeds and does not
finish the input session or insert its selection after a declined operation.
Hybrid activation now retains a pre-mutation request when its invocation context
is missing. After layout admits the context, it queues a separate BufferActor
editing transaction rather than mutating during layout. The request is cancelled
by later source changes, keyboard input, caret movement or selection changes;
callbacks carry only a numeric request token. This is not generic command replay
and does not yet handle unresolved inferred field policies or prospective table
preparation failure.

Ready hybrid requests are now retained by the BufferActor until the ordinary
FIFO mailbox accepts their token. The actor retries after returning command
slots, without blocking on the producer gate or rerunning layout merely to
retry submission. Queued source edits and caret/selection changes still precede
the activation and cancel stale requests. Closing a view withdraws its retained
request. A real-actor saturation probe verified delivery after 127 queued
commands and cancellation by an earlier queued caret movement.

Analysis capture can reuse frozen variable overrides from the checkpoint just
restored into its evaluator. The lookup is synchronous and owner-local; the
worker receives only the retained immutable value. Cold baseline values and
definition capture still need their own bounded-input treatment.

Restoring an existing checkpoint now rebases the execution-context recorder
directly on that immutable checkpoint, retaining its accumulated boundaries.
It does not recapture a second variable table from the restored evaluator.
Detached field environments likewise merge the checkpoint's baseline and frozen
overrides directly, without materializing and refreezing the overrides. The
evaluator still receives its own mutable table and detached tree values remain
isolated between owners. These remove duplicate work, not the remaining full
baseline capture or large-input budgeting obligations. A legacy hashmap copy
copies its table entries, not the trees referenced by those entries.

Detached analysis now checks owner cancellation inside macro argument and
dependency traversal, variable-arity signature discovery, dependency fan-out,
result projection and dependency-index construction. Boost still owns SCC
computation; a cancellable component property map checks its vertex/edge work
without replacing Tarjan's algorithm. This improves cancellation within one
large analysis, not just between macros.

Detached batches now retain their input, dependency-closure, signature/reset,
fixed-point and output progress. The existing worker pool returns unfinished
batches to its FIFO after a four-millisecond soft slice; fact projection and
provenance construction also retain their progress. One owner has at most one
running batch plus its coalesced latest demand, and only completed exact-demand
results can be published. Superseded batches still finish into the existing
cache without notifying the editor. Offline preparation uses the same stages
without yielding to the scheduler. This is inter-batch fairness, not a hard
latency guarantee: input diff, an individual recursive definition/large fan-out,
the Boost dependency-index pass and large allocations/destruction may overrun a
slice. Owner capture and stale-fact retirement remain separate budget obligations.

Closing an analysis owner now marks its service cancelled immediately but gives
its detached capture/result state to the existing worker executor for release.
Superseded queued requests and unconsumed completions are released there too.
One coalesced reclamation task drains retained objects outside the scheduler
mutex, yielding between objects after a soft time slice; a full worker queue
retains the releases until a finishing task can admit them. This addresses
owner-thread destruction, not input capture or stale-inference invalidation.
An individual destructor is not preempted. During process shutdown, after the
executor starts draining, late destruction is synchronous rather than posting
to a dying executor.

Completed analysis now signals its BufferActor through retained per-view
readiness, not a best-effort native continuation in the bounded command ring.
Repeated notifications coalesce; either a normal command or an independent
wakeup drains them on the owner. Closed owners discard readiness and closed
views are looked up again before dispatch. This guarantees notification delivery
under mailbox pressure without giving workers editor access or changing FIFO
ordering of source-edit commands. It does not resolve the separate retry policy
for arbitrary commands; the specific hybrid retry policy is described above.

The graphics grid command chain now has a native pre-mutation update plan.
It captures the admitted canvas-entry context, authored properties and initial
aspect defaults before computing visual/logical grid changes. Scheme keeps
command names, interactive prompts and checkmarks, but no longer owns mutable
current-grid globals or reads an environment after property writes. Link state
comes from the current canvas, not a cross-document Scheme variable. Notebook
grid setup also uses one plan instead of delayed color-setting callbacks.
Missing canvas preparation is rejected before any source mutation; this does
not install a getter fallback or authorize queries in newly created fields.
Build/runtime verification is recorded separately in the implementation README.

Graphics style sampling now collects all object properties and canvas defaults
before one source assignment. It no longer reads defaults after earlier writes
or reuses the original canvas path after wrapping it in WITH. Style application
and ordinary canvas-property queries select the prepared canvas-entry context,
not a locally styled text object's cursor environment. The nullable prepared
value helper and its generic-getter fallback are removed. Missing preparation
is detected before the style transaction; automatic retry for such commands
remains separate from the existing hybrid activation continuation.

Opening the hybrid inserter captures source mode before cutting its selected
content, so insertion does not query an invalidated cursor after the cut.
The BufferActor's post-command wheel-capture notification publishes only with
an admitted cursor context; source replacement without layout retains the
previous UI state until a later layout/apply command can publish the new state.
It no longer turns a completed replacement into a failed command solely by
querying PREAMBLE before preparation. Editing commands still require their own
prepared inputs; the UI notification gate does not supply a default mode.

Inferred projections carry detached input/dependency evidence through scope
inheritance and local restoration. A new analysis owner validates those receipts
instead of clearing all inherited facts while its worker runs. Effective values
and evidence use separate persistent maps, so renewing a receipt does not make
every unchanged contract a value delta. This resolves that particular cold-fact
loss; source identity handling of genuinely pending/ambiguous inference remains
unfinished, as do query fallback removal and bounded large-input admission.

The source-identity gap has a semantic decision as well as a lifecycle bug.
`finish_node_identities` supplies the cursor's ambient contract scope to a
planner traversing source ancestry and the affected subtree. For non-native
macros, `roles_for` uses inferred child type/accessibility/environment and
parameter names to classify persistent content. Replacing that scope with the
global preamble scope would still misclassify local bindings. Two policies are
not interchangeable: native/explicit source-role authority, or retained
per-binding inference with explicit pending preparation. The user has been
asked which contract custom macro bodies should have; no removal of this
support or blanket content/data classification has been made while that
decision is open.

Live identity finalization now requests an explicit contract at each custom
macro's BODY-relative source entry. Its preparation executes the relevant
prefix in an isolated evaluator, resolves local definitions and declarations,
then supplies the result to the identity planner without borrowing the caret's
display context. The same source-entry policy is used when adopting a source
buffer. Detached load/replacement and template creation prepare an independent
document preamble and source prefix. Native fields and explicitly declared
source roles do not require these extra evaluations. A missing entry fails the
identity plan before UUID allocation or mutation; regular transaction rollback
continues to own failed edits. This does not establish a latency bound for large
prefixes or confer source roles on macros with ambiguous inferred fields.
Cold and offline source-role preparation now retains one advancing top-level
paragraph-entry checkpoint per detached document plan. It executes only the
previously unvisited complete paragraphs between increasing source addresses;
nested positions still prepare inside their own paragraph from the unmodified
entry, and backward queries restart at the document baseline. The retained
entry also captures isolated reference, auxiliary and attachment maps, not just
variable and contract state. A focused real-evaluator test covers preceding
definitions, WITH-local shadowing, later reassignment and backward lookup.
This amortizes monotone paragraph prefixes; it is not a hard time budget for
large individual paragraphs, macro analysis or non-monotone access.
The editor now owns one of these providers per identity transaction rather
than reevaluating the whole document for every custom role. It reopens the
provider after Artifact binding may have mutated source. Authored document
preambles are evaluated before custom-role entries in the editor as well as
offline, and preamble-local reference/auxiliary/attachment writes are retained
inside the detached preparation maps. The focused real-evaluator regression
also covers a macro defined only in an invisible document preamble.
The old editor-wide `prepare_source_contracts` pass is gone. Online activation,
BufferActor replacement, new-document creation, namespace/interop creation,
and offline migration pass the immutable builtin scope as the fallback only;
their custom-role provider owns any required style, preamble and local-binding
analysis. Documents with only native or explicit roles no longer pay for a
second eager preamble analysis just to validate source identities.
Offline source-identity initialization, label-cleanup verification, UUID
assignment, write preflight, post-write validation and Artifact source
classification all use the same BODY-relative role provider. Each phase uses
its document's actual source path and package-resolution policy, rather than a
single global preamble scope. This is code-path alignment, not a new Vault
migration run.

The attachment and source-outcome implementations have successful
production-library builds and focused temporary probes. Application deployment
evidence is recorded separately in the implementation README; these checks do
not establish completion of prepared consumers or asynchronous inference.

## Objective And Non-Goals

Replace DRD's mixed schema, editing, presentation and inference responsibilities
with one native authority. Removing Scheme files or renaming C++ classes alone
does not achieve this: much of DRD was already C++.

- Preserve existing v3 document meaning; no Notes migration is required.
- No new macro language, expression interpreter or generic policy engine.
- No new mandatory database and no prerequisite on the proposed `.ac` cache.
- No requirement to infer every helper macro. Native nodes and shipped
  interactive wrappers have explicit editing contracts.
- No menu-triggered analysis, synchronous cross-actor environment requests or
  whole-vault scans to maintain contracts.

## Four Responsibilities, One Resolved View

| Responsibility | Source of truth | Examples |
| --- | --- | --- |
| Structure | Existing v3 schemas, progressively shared with named field descriptors | Required fields; mathematical constraints; typed metadata |
| Editing | Compiled policies consuming those fields | Cursor entry/exit; deletion; row selection; optional slot creation |
| Presentation | Ordered package/local declarations | Inline/block behavior; child environment; source-to-render mapping |
| Relationships | Existing semantic registries and native group data | Categories; ordered alternatives; explicit conversion operations |

These are not four registries independently defining a node. A prepared view
resolves their contributions for one binding and scope. A style cannot change
core source invariants. A transformation must convert fields, not merely rename
a tag because two tags belong to the same group.

The replacement is not a new general evaluator. Keep normal macro execution,
bridge invalidation, native math editing and v3 validation in their existing
owners. Contracts describe their established boundaries. Only the observations
needed to select/reuse those contracts belong to this subsystem; unrelated
rendering or external side effects are not made replayable by this project.

Data remains data: labels, groups and static field declarations can use JSON.
Algorithms, mathematical constraints and editing operations remain C++. Do not
duplicate the enunciation registry or move C++ procedure names into JSON for
dynamic invocation.

Logical fields include metadata, not only child indices. `enunciation.name[i]`
and `scripts.right-subscript` are field addresses; a positional path is their
current storage projection. A render occurrence identifies which displayed
instance refers to that source field. It is not a new persistent UUID.

### Field Model And Mutation Authority

The existing `native_fields` registry is the structural entry point, not a
second schema beside the v3 codec. Migrate duplicated shape and property checks
into its shared descriptors incrementally; leave domain validation in the native
node model. The codec, constructors and editor consume the same shape rules.
Embedded JSON defines names, cardinality, property types and static categories.
Compiled policies own actions; JSON must not contain executable procedure names.

A logical address consists of an owner-local source anchor, a field key and,
where necessary, a repeated-element address. The anchor uses the existing tree
observer/rebasing mechanism. List indices are projections at a source revision,
not durable identities: inserting an earlier `name` slot must rebase the selected
element or invalidate its mapping, never silently select its new occupant.
Storage projection handles both tree children and metadata rich text. Core
field keys are shared by producers and consumers, not repeated magic strings.

Do not collapse these independent properties into the old accessibility integer:

| Property | Authority |
| --- | --- |
| Legal persisted structure | Native shape and domain validation |
| Caret entry and navigation order | Editing policy plus current source/display mapping |
| Writable in this view | Source ownership, view permissions and field policy |
| Visible in this occurrence | Actual expansion/layout |
| Search/index projection | Explicit semantic projection, independent of editability |
| Supported transformation | Registered native operation with input/output constraints |

For example, a big operator's glyph is valid data but not a text slot reached by
Backspace; an enunciation name is editable rich text without being an ordinary
body paragraph; a hidden field can remain valid source without a screen box.
Being displayed once does not establish unconditional macro-argument access.

Contracts describe an operation's inputs and applicability; they do not perform
the operation. Delete, split, row insertion and variant conversion remain native
editor transactions, with one undo step and the existing save/index notification
path. Unsupported operations remain unavailable. They must not fall back to
renaming tags or exposing raw structural macro arguments. No new generic
mutation interpreter is introduced.

## Runtime Ownership

The evaluator is the definition authority. It records actual execution, not a
second scan that tries to reconstruct execution from the final environment.

Three objects have separate lifetimes:

1. **Evaluation checkpoint:** the matching environment, effective definition
   bindings and authored declaration contributions at an execution boundary.
2. **Prepared view:** that checkpoint's explicit facts, applicable inferred
   facts and source/render field mappings. Commands retain this immutable view.
3. **Analysis service:** dependency-keyed reusable results for many checkpoints.
   Selecting a cursor scope does not rewind or overwrite the service.

Immutable expression values can be shared by several definitions. Equal values
do not imply the same binding or producer. An actual definition execution creates
a binding; admitted cache replay reuses the captured binding. A producer's
source revision and relevant execution inputs determine whether replay is valid.

Each document execution owner has an incarnation. A reopened document cannot
accept work from its previous owner. View-specific mode, permissions and host
presentation stay separate from source semantics. AVD/transclusion combine a
prepared source view with host presentation; peek restricts editing without
mutating a shared contract.

## Current Implementation Boundary

The current implementation has native descriptor storage, declaration/binding
event replay, temporary scopes and root exception rollback. Preamble and cached
cursor environments retain matching contract state; analysis results survive
selection of an earlier snapshot. Bridge admission now checks captured variable
and descriptor reads in execution order, and replay restores their combined
outgoing effects. WITH bridges now also retain an identity-bound body-entry
boundary and can replay its admitted prefix without reevaluating the binding
expressions. Owner-side source-instance records now connect unique atomic source
positions to these boundaries and their argument/file context. Editor preparation
selects them after layout and on cursor movement; this is a partial consumer
cutover, not a complete preparation lifecycle.

Descriptor storage now separates native defaults/authored rules from materialized
inference projections. Analysis reads the former and publishes only the latter;
creating a declaration no longer copies computed parameter names or analysis
diagnostics into its source. A fresh analyzer rebuilds projections without
borrowing them from an unrelated parent environment. Both layers remain part of
retained checkpoints. The implementation now also has per-target declaration
chains, captured bindings, event-identity withdrawal and group recomposition.
Focused production-library checks cover snapshot isolation and real bridge
declaration-only deletion through checkpoint restoration and admitted replay.
Binding-local applicability, pending attachment and package-default priority
are now implemented and have focused probe evidence. Declaration executions now
retain exact frozen source, location, decoded contribution when available, and
their applicability/diagnostic outcome. Real bridge checks cover valid, invalid,
malformed, corrected and deleted source through normal checkpoint restoration
and replay. This is not yet complete producer ownership across all preparation
modes or complete input receipts.
The direct withdrawal API and checkpoint reconstruction are distinct mechanisms;
neither alone completes that lifecycle.

The code still has the following concrete gaps:

| Current path | Required replacement |
| --- | --- |
| `typeset_preamble` captures an environment/input/contract checkpoint after live analysis | Complete baseline binding provenance and detached demand preparation |
| `typeset_prepare` restores that pair; bridge replay checks captured reads | Complete derived-state/external-provider dependencies and source-bound execution records |
| `WITH_PACKAGE` executes and captures the package's actual write set | This replacement is implemented; complete the remaining field-preparation owners rather than reintroducing an assignment-only rewrite |
| Physical hits select a captured occurrence before native cursor correction; exploratory checkpoints are separate from the published `cur[path]` entry | Complete metadata/progressive/source-mode fields, remaining explicit-policy callers and selection continuity across changed layout frames |
| `restricted_exec` remains the fallback for unprepared positions | Complete folded/progressive/source-mode preparation and remove the heuristic path |
| `drd_update` analyzes the cursor environment | Publish command state during preparation and remove menu-triggered analysis |
| `macro_analysis::synchronize` consumes tracked value candidates and declaration changes | Select facts by complete binding/context dependencies rather than one current name-keyed graph |

The transitional analyzer now retains its results separately from their live
projection and reconciles declaration changes on snapshot selection. This fixes
the lost-delta problem without copying the analyzer per cursor. It still has a
single current name-keyed input graph. Preparation now reads tracked candidate
names rather than scanning all live values; it is not yet the final
binding/context-keyed service. Cached analyzed cursor entries select their
recorded facts directly, without repeating analysis.

### Complete Definition Inputs

The next implementation prerequisite is complete resolution at a checkpoint,
not merely a longer list of tracked macros. Default callables now have a reusable
baseline, ready-value writes synchronize matching bindings, and explicit whole
environment replacements rebuild their callable baseline at that boundary.
Package restoration retains evaluated values and contracts together. Ordinary
non-callable values still live in the value/presence layer rather than being
duplicated into the macro table. The complete detached name-resolution view,
source-revision producer ownership and dependency receipts are still missing.
Package inclusion origins are now recorded, but do not supply those other
identities. These are ownership gaps, not scheduling bugs.

The `evaluation_inputs` journal now accompanies default/replacement input,
direct writes, checkpoints, package restoration, temporary scopes and rollback.
The synchronous adapter consumes its candidate names; the legacy importer
records names while reading assignments, not by a final environment scan. A
failed analysis discards its partial incremental engine and restarts from the
complete selected input. This is candidate-name tracking, not complete binding
provenance or detached analysis. Baseline changes and journal compaction still
have the proportional cost described below.

Use one checkpoint builder for the following inputs:

| Input | Required producer and behavior |
| --- | --- |
| Built-in environment | An immutable native baseline built alongside default values; instantiate owner-local evaluator values without rediscovery at each document open or sharing live legacy trees |
| Package execution | Ordered evaluated assignments and authored declarations, retaining package origin and inclusion occurrence |
| Document initial values | An explicit owner-local initial-values producer layered over the package checkpoint |
| Body assignment or local binding | A source-bound execution producer; actual execution creates a binding, admitted replay selects the recorded binding |
| Existing checkpoint restoration | Select its paired values, bindings and contributions; restoration is not a new assignment |
| Isolated value-only environment | An explicit replacement boundary builds a new baseline once; it cannot pretend to restore missing historical declarations |

All name slots must distinguish missing from present non-callable, including
present `UNINIT` where the evaluator distinguishes it. Do not intern every scalar
name as a node tag or deep-copy every scalar into a second definition table.
The checkpoint's value/presence layer supplies resolution slots; only callable
bindings need macro payloads. Scalar values used by analysis become detached
dependencies on demand. Share the same assignment record with variable replay
rather than recording two competing histories.

Binding identity and value identity remain separate. Name resolution records the
consulted scoped slots, including negative lookups. A nearer scalar assignment
must suppress an outer callable even if that scalar was never previously a
macro. Scope exit selects the outer slots again; it is not a deletion of their
definitions. Removing an execution producer withdraws its effects, so the earlier
binding becomes visible instead of manufacturing a new definition from a final
environment scan.

The builder publishes a complete checkpoint and its changed slots/contributions
together. Checkpoint selection reports a branch change, not fabricated source
edits: analysis results for both branches may remain reusable. An absent entry in
an incomplete transitional tracker must never become an authoritative negative
lookup. Remove the scan only after defaults, package loading, initial values,
temporary environments and headless loading all enter through these boundaries.

### Separate Change Channels

There are three change channels with different consumers, not three copies of
the environment. Produce them from the same evaluation transaction:

| Channel | Records | Consumer |
| --- | --- | --- |
| Execution effects | Ordered assignments, scope entry/exit, reads and declaration effects, including equal assignments | Replay admission and outgoing checkpoint construction |
| Resolution changes | Changed scoped slots/bindings and authored contributions, including withdrawals | Context selection, dependency invalidation and provenance |
| Value candidates | Coalesced names whose value or presence may differ between checkpoints | Transitional value analysis and detached input capture |

An equal-valued assignment can create a new binding without a value change. A
declaration can change behavior without writing any variable. Leaving a scope
reveals an old binding rather than creating a new one. Therefore a name/presence
journal cannot replace either of the first two channels.

Candidate lists may conservatively include unchanged names. Consumers compare
only those candidates against the matching checkpoint; they must not acknowledge
a new baseline before the corresponding inputs have been successfully consumed.
Failure retains the old acknowledged point and retries from a complete input,
not from a partially mutated adapter. Worker consumption and UI publication have
separate acknowledgements.

Journal compaction is storage maintenance, not a semantic change. The current
journal compares different roots by enumerating their native name sets; this is
a valid transitional fallback but is still proportional to the environment's
name count. It must not be advertised as bounded incremental preparation. The
final dependency service tracks consulted slot revisions and binding identities;
compacting history preserves those revisions. Do not manufacture changes to all
slots merely because a chain reached its retention limit.

Checkpoint selection is a branch switch. Compare dependencies against the
selected checkpoint, retain reusable results for other demanded branches, and
do not reverse the evaluator by replaying a synthetic list of edits. Execution
records and journals are retained only while referenced by live checkpoints or
bounded reuse caches; limiting one chain's depth alone does not bound total
memory retained by old snapshots.

### Package Cache Decision

The former style cache pair (`H`, merged DRD locals) cannot reconstruct producer
or declaration provenance. Its disk reader/writer and `get_locals/set_locals`
encoding have now been removed. Old cache files are ignored, not migrated;
package evaluation supplies the in-memory checkpoint.

The new owner-thread package cache pairs values with their contract snapshot,
including currently tracked evaluated bindings. Editor startup, menu typesetting
and offline preamble loading use that pair. Returned descriptor projections are
independent drafts, not shared mutable analyzers. Keys include the source
directory and module-execution policy, and existing style invalidation retires
the cache. Complete per-input dependency admission and owner-local producer
instantiation remain to implement; this is not yet the complete package protocol.

The final package protocol shares immutable payloads while owner-local execution
identities remain local. Only reuse a checkpoint when its captured inputs still
apply. A cache miss follows normal package loading, not a special inference pass.

`WITH_PACKAGE` now uses normal evaluated package execution in a temporary scope.
The old `with_package_definitions` assignment-extraction helper and both rewrites
have been removed. Package declarations, nested inclusions and conditional
initialization participate in the captured scope. The obsolete standalone
Style/Evaluate engine does not emulate this owner boundary.

The scoped package boundary must have an explicit write set. Evaluate the
package once against the caller's incoming values, capturing every assigned name
and declaration target, including nested inclusions and targets with no variable
write. Install that completed paired state for the body. On exit, restore those
names and targets from the incoming checkpoint; preserve unrelated body effects,
as ordinary WITH does. A changed dependency invalidates the package record;
opening a menu does not reload it. The implementation has focused nested-package,
restoration and package-source invalidation evidence in the implementation log.

Persisting the new checkpoint is optional follow-up work. It must retain authored
contributions, origins, dependency receipts and relative binding references, not
pointer identities or just resolved descriptors. Loading remaps identities and
validates package inputs and representation version. No new cache database or
`.ac` integration is required to finish DRD removal.

### Checkpoint Placement And Replay

Use existing evaluation/bridge boundaries. Retain a base checkpoint, ordered
child records and scopes required by prepared occurrences. Do not take a full
environment/map snapshot per character, field query or declaration setter.

After a source edit, invalidate the affected execution records and prepare from
the nearest valid preceding boundary. A removed declaration must disappear even
when it previously produced no environment patch. Subsequent records are reused
only if their sources and ordered inputs still match. Equality of rendered text
or of one environment patch is not a convergence criterion.

Inputs include variable reads, callable/non-callable/missing name resolutions,
native descriptor and group reads, and revisioned external inputs when available.
Reads after local writes depend on those writes, not on the block's incoming
value. `provide` records the presence check even when it makes no assignment.
An assignment remains an effect even if its value already matched. The current
variable/descriptor admission must not be mistaken for coverage of every input;
direct derived-state reads and external effects still need classification.

Group queries now retain their actual result: a membership boolean, a selected
subgroup, or an ordered member list. Admission checks that result against the
draft at the query's execution position, including earlier declarations and
local scope exits. These are separate dependencies: adding an unrelated member
does not invalidate an individual membership read, while reordering members
invalidates a list read. The query carries its owning `contract_scope`; shared
projection storage is not evidence that two owners are interchangeable.

Syntax lookup now records the explicit descriptor and, only when that lacks a
syntax property, the selected environment value/presence. VALUE and OR_VALUE
lookups use this same name-resolution path without interning scalar names.
Tree-form lookup uses its receiving scope rather than the thread's unrelated
current DRD. This closes a concrete indirect environment-read gap, not all
derived-state reads.

Admission works on a private draft before applying either environment or contract
effects to live state. It interprets captured effects, not source expressions.
An unknown external dependency makes the relevant record non-reusable; it does
not justify rerunning external actions during cursor navigation or inference.

Transactions publish the completed state and occurrence mappings together.
Partial execution identifies its boundary explicitly and cannot be reused as a
complete bridge result. Failure restores the incoming pair. This is editor-state
consistency, not rollback of external effects.

## Occurrence Preparation, Not Cursor Reexecution

The next implementation slice must establish this path before moving inference
onto a worker:

```
source mutation / explicit preparation
  -> owner evaluation transaction
  -> source-bound execution records + field occurrence mappings
  -> immutable prepared context
  -> layout/focus/command-state publication

cursor move over a prepared occurrence
  -> select its context
  -> publish command state (no source execution)
```

An execution boundary is not a screen box. A prepared field occurrence names
its source owner/anchor, field address, expansion occurrence, source revision
and boundary in an admitted execution record. Multiple displayed copies of one
argument retain separate occurrence contexts but the same editable source.
Generated punctuation has no editable address. No internal persistent UUIDs
are added for these mappings.

The boundary retains a base checkpoint and shared record references, including
the active local/argument scopes. It is not a full environment copy at each
glyph. Owner preparation can materialize the contexts currently needed by
visible/focused fields; queries only select already prepared results. Large
preparation must yield between existing evaluation units, not execute a whole
document inside a getter. Resumable evaluation is work to implement, not a
property supplied by the current snapshot class.

Three cases require explicit integration rather than a generic `exec_until`:

| Case | Decision |
| --- | --- |
| WITH body | Capture its evaluated bindings and inner boundary during real execution. Selecting the body does not reevaluate its right-hand sides or perform the outer-scope exit. |
| Heading-folded source | Preserve the source-bound execution record independently of the zero-child `folded-hidden` screen placeholder. Folding affects presentation, not the definitions available after that region. |
| Macro argument or repeated display | Retain the actual argument/source stack and each expansion occurrence. A source path alone cannot choose the call-site context. |

Do not add a second whole-document evaluation pass to repair folding. Retain
records from normal evaluation, and let owner preparation execute a genuinely
unprepared source region through the existing evaluator without constructing
its hidden boxes. Visible layout must consume that admitted execution rather
than execute its semantic effects again. This handoff is required before the
folded path is switched; source-level conditional branches retain their normal
evaluation semantics and are not treated as UI folding.

Read-set coalescing may combine equivalent dependencies, but must not erase
source/occurrence boundaries. Event-vector offsets alone cannot identify a
producer across edits. An admitted prefix supplies the next record's incoming
state; successful admission returns its outgoing effects so the owner does not
walk the same log again merely to apply it. Admission and application occur in
one owner operation, or require revalidation after any intervening mutation.

On a cache miss, prepare from the nearest valid source boundary. Do not use the
current `restricted_exec` ten-child approximation, expand that scan to the whole
prefix, or treat `bridge_with::my_exec_until` as a general read-only query: its
prepared prefix is reusable, but its miss path still evaluates source. Unknown
external dependencies invalidate reuse, not permission to rerun external actions
for cursor metadata. Already evaluated effects can remain part of their exact
checkpoint even when they cannot be replayed under different inputs.

## UI Publication And Stale Frames

Owner preparation precedes focus, footer, toolbar and menu snapshot publication
for admitted occurrences. `drd_update` and synchronous menu-triggered analysis
are removed. Initial-value queries read their prepared baseline; displayed-field
queries select the boundary index produced by execution or admitted replay.
The remaining variable-query fallback for missing occurrences still executes
the old approximate prefix. Removing it requires explicit source preparation
for cold positions and freshly edited fields, not simply another getter rename.

Keep the last frame visible while replacement layout is pending, but route edits
through a current source address. Use existing observer rebasing where it proves
the address still valid. If its source or occurrence mapping is no longer valid,
request owner preparation and leave that mapping-dependent operation pending;
never apply the click/key to a guessed neighbor or an old source path. Ordinary
native-field typing must not wait for optional macro inference. Revalidate queued
intent against the current selection rather than replaying stale keystrokes into
a different field.

An inferred fact arriving later can dirty the affected command/layout consumers;
it does not republish an old source frame. AVD/transclusion use the source owner's
semantic checkpoint and the host's presentation overlay. They neither borrow the
last focused document's environment nor wait synchronously on another actor.

## Declaration Lifecycle

Retain authored contributions separately from resolved descriptors. Every
contribution identifies its producer revision, scope, attachment and affected
properties. A merged value alone cannot support deletion, replacement or an
explanation of precedence.

The current `declared` lookup names the native-default/authored descriptor layer,
not a complete provenance record. The declaration chain is an initial
storage mechanism, not the finished ownership protocol. Captured MACRO signatures may establish field
positions for applying a declaration without body inference; that does not turn
the signature into an authored strict-arity invariant. `publish_inference` writes
a separate materialized projection. Explicit field labels take precedence over
inferred formal names, and withdrawing a projection does not withdraw a rule.
The next integration must add actual contribution ownership, not treat either
two descriptor maps or an event-pointer history as the finished lifecycle.

- Native invariants cannot be overridden by styles.
- Package presentation defaults follow inclusion order.
- A local macro-field declaration attaches to its local binding, not globally
  to its name. Explicit environment-parameter metadata has a separate target role.
- A macro-field declaration preceding its definition remains pending in its
  owning scope; parameter metadata does not require a callable definition.
- Inference fills missing facts; it never overrides explicit facts.
- Replacing a declaration first withdraws the superseded contribution inside
  the transaction. An invalid replacement contributes a diagnostic, not the
  previous revision's value. A valid lower-priority rule may become visible.

Groups follow this same lifecycle. Removing a declaration must not leave stale
group membership after its field rules have been withdrawn.

### Scope Is An Evaluator Boundary

Do not interpret every nested syntax node, bridge or macro call as an isolated
scope. Match the existing evaluator's observable assignment behavior:

| Boundary | Entry | Exit |
| --- | --- | --- |
| Ordinary body sequence or macro expansion | Continue the incoming name slots; retain a distinct producer occurrence | Effects remain available to the following source, unless enclosed by an explicit scope |
| WITH | Evaluate right-hand sides in the existing evaluator order, then shadow exactly the bound names | Restore those names and their contract attachments; preserve effects on other names |
| Temporary evaluation | Select an isolated paired environment/contract draft | Discard the draft and restore the caller |
| USE_PACKAGE | Execute once in a package-origin frame | Restore ambient origin, but retain package contributions in the enclosing environment |
| WITH_PACKAGE | Evaluate and install the package's captured write set for its body | Restore the overlaid names and declaration targets; preserve unrelated body writes |

A right-hand side can itself assign a variable before WITH entry. Those effects
are part of the incoming state and must not disappear when the body exits. A
prefix selected inside WITH performs entry, but not exit; replaying a completed
record performs both. Source selection must never rerun a right-hand side merely
to reconstruct its context.

For a shadowed macro name, an outer pending rule is unavailable inside the new
binding and cannot be consumed there. The outer rule is selected again on exit.
A pending rule for an unrelated name remains owned by that name's existing slot;
merely executing inside another WITH does not make it temporary. This makes
"expires on scope exit" precise without changing ordinary assignment semantics.

Environment-parameter metadata follows the slot rather than each assigned value.
Changing `font-size` locally, for example, must not erase its type description.
A shadow starts with the enclosing slot's effective metadata as defaults;
metadata declared inside that shadow is restored on exit. Native-node
presentation may be locally overlaid, but native structural constraints remain
immutable. The final representation needs slot/producer receipts for these
cases; retaining a name-keyed chain alone does not establish that ownership.

Producer identity is an owner incarnation plus an execution occurrence and its
source revision, not a tag name, text hash or current vector offset. Repeating a
package inclusion or macro expansion gives distinct producers even when their
source text is equal. Admission may retain the original occurrence identity;
actual reexecution creates its replacement transactionally. No producer IDs
need to be persisted in Notes.

Maintain a producer-to-contributions index alongside ordered declaration chains
for each attachment target. These chains are the authored authority; per-property
winner indexes are derived accelerators, not a second mutable rule store.
Initially, recomposing the affected target's chain is acceptable. Removing a
producer visits its contributions and the affected targets, not every contract.
Preserve ordered group membership contributions;
duplicate membership from two producers stays present until both are withdrawn.
An inferred value never enters these authored stacks. Withdrawn explicit rules
expose a lower rule or pending inference, not an old unvalidated inferred value.

Attachment is explicit, not inferred from declaration order alone:

| Attachment | Replacement and scope behavior |
| --- | --- |
| Native invariant | Attached to the native kind; package/local declarations cannot weaken it |
| Package default | Owned by the package inclusion; revalidate against a later applicable signature before using it |
| Bound local macro rule | Attached to the resolved callable binding; does not transfer to a new binding merely because its name is equal |
| Pending local macro rule | Attaches to the next matching callable definition in that scope, for subsequent checkpoints only; expires on scope exit |
| Environment-parameter metadata | Explicit parameter role targeting a scoped variable slot, not macro arguments; applies to scalar or macro-backed values as declared |

For an overridable property, nearest applicable local scope wins over package
defaults; later authored contributions at the same precedence win. Inference
only fills an unclaimed property. Keep the losing contributors so withdrawal
reveals the correct preceding value. Resolve and validate one declaration as a
unit before publishing it; invalid fields must not leave partial group or field
effects. Diagnostics identify producer, target binding and field selector.

Keep invocation signature, storage layout and editable surface distinct.
MACRO formal count is not a new strict arity validator; XMACRO's tuple formal is
not automatically an editable source child. Resolve selectors against the actual
layout before applying any part of a declaration. Fix contradictory shipped
declarations in their packages, not with node-name exceptions in the resolver.

### Identity, Attachment And Ordering

Keep the following identities separate; none requires a persistent document UUID:

| Identity | Meaning and lifetime |
| --- | --- |
| Declaration payload | Immutable decoded rules; may be shared by equal declarations |
| Execution producer | One source revision executed in one owner/scope/expansion occurrence |
| Definition binding | One evaluated assignment; equal-valued assignments remain distinct |
| Contribution | One declaration effect of a producer, with attachment and selectors |
| Checkpoint position | Its order in a particular admitted execution sequence |

An event `shared_ptr` can remain an implementation handle inside a retained
record. It is not the complete producer key. File names and paths are diagnostic
locations, not identities: paths move after edits, and repeated expansions can
have the same source path. Reuse the existing owner incarnation, source observer
and expansion record; add revision/attachment information there instead of
inventing a parallel source-tracking system.

Admission of a record against its incoming checkpoint selects its recorded
bindings and contributions. It does not append another copy, recapture bindings
from the final environment or move the same event to the end of an already
materialized chain. Actual reexecution creates a replacement record. Relative
precedence follows the new execution sequence, not allocation order or a counter
copied from another checkpoint branch. Unchanged source can still need a new
execution when an observed input changes.

Resolve the target role before choosing attachment. Native node presentation
targets an existing native kind. Macro-field rules target callable bindings.
`parameter` and `macro-parameter` declarations explicitly describe environment
parameters: these select scoped variable slots and their metadata, not a
callable's argument layout. The current packages include length, color, string
and boolean parameter metadata, so requiring every declaration target to be
callable would remove real functionality. Encode this role in the decoded native
declaration, not through a list of special parameter names in the resolver.
Parameter value changes do not redefine the metadata of their selected slot;
slot shadowing and withdrawal select the appropriate metadata scope.

For macro-field rules, attachment comes from the evaluator's explicit execution frame:

- Native registration produces invariant/default entries, never user rules.
- Package initialization produces defaults owned by that inclusion occurrence.
- Ordinary document/local execution attaches to the callable resolved in that
  frame. A different binding with the same spelling does not inherit the rule.
- With no applicable callable, a local declaration is pending in that scope.
  It may attach to a subsequent callable definition there; it never rewrites an
  earlier checkpoint. Scope exit discards unbound pending contributions.

Presence of a scalar and absence of a name are distinct resolution results.
Neither permits reaching past that scope to attach to an outer callable.
Package defaults are intentionally different: they can apply to a replacement
binding, but selectors must be resolved against that binding's signature/layout.
Diagnostic source locations must not be used to guess package versus local
attachment. Package loading and local entry already own that distinction.

An immutable package-inclusion frame now records its resolved source and parent
inclusion separately from `cur_file_name`. Declaration and definition events
share that frame; a real repeated include creates a new frame while admitted
effects retain theirs. This is provenance, not complete source revision identity.
The current resolver uses it for package-default applicability; full producer
replacement still requires source/occurrence ownership. Package frames must not
change document-relative URL evaluation or require a new context allocation for
every ordinary WITH/glyph.

A captured binding serves two separate purposes: explaining the declaration's
original selectors and deciding whether its attachment applies now. Replaying
old selectors with a stored signature alone does not establish applicability.
Named selectors resolve to logical fields; legacy numeric selectors are decoded
against that declaration's applicable layout and are not silently retargeted to
a new field when a definition changes shape.

### Replacement Transaction

The owner replaces execution records, not individual merged descriptor values:

1. Locate the invalidated source/expansion records and their owned effects.
   Start a private draft from the nearest valid incoming checkpoint.
2. Withdraw superseded contributions and select/recompute the affected name
   slots. Preserve lower-priority rules and unrelated records.
3. Evaluate changed source through the existing evaluator; admit later cached
   records only when their ordered reads and attachment inputs still match.
4. Resolve affected declarations as whole units. Publish fields, memberships,
   diagnostics, bindings and occurrence mappings at the same execution boundary.
5. Notify consumers from the resulting changed keys. A declaration-only edit
   must notify even when no environment variable or rendered text changed.

A malformed declaration replacement is a completed source diagnostic with no
active contribution from that revision. Withdraw its predecessor, expose valid
lower rules, and keep native editing available. A subsequent otherwise-valid
declaration that becomes invalid because an earlier layout disappeared is also
inactive, with its field and group effects withdrawn together. Retain its source
so it can become applicable again after a later valid change.

Retirement must not depend on successfully decoding the replacement's target.
The invalidated execution record already identifies its old contributions. A
syntax error before the new target is known still retires those contributions
in the successful diagnostic publication. Store the source diagnostic against
the new producer revision, with no fabricated descriptor. Decode errors and
selector/layout errors use this declared error path; unexpected evaluator
exceptions use rollback instead.

This is different from an unexpected execution failure: abort that draft and
retain the last published frame. Its receipt still names the old source revision,
so it cannot authorize edits at a now-stale mapping. Do not claim the current
source was successfully prepared. Do not catch arbitrary exceptions and turn
them into declaration diagnostics. Root rollback remains the existing owner's
responsibility, including the distinction from irreversible external effects.

The current normal-layout path now retains a `declaration_execution` record
even when decoding fails before a target exists. A decoded but invalid
contribution is retained as inactive source with a diagnostic, not thrown away.
Replay compares its execution outcome after resolving its contribution: an
earlier declaration can change selector validity without changing any variable
or macro binding. Changed validity rejects the cached record and rebuilds its
error/normal output. Exact frozen source and execution identity survive admitted
replay; normal incoming-checkpoint reconstruction and bridge replacement retire
the superseded record. This uses existing execution ownership rather than a
second path-keyed registry or a global diagnostic table.

The public mutation boundary is producer replacement/withdrawal. Raw descriptor
replacement is reserved for bootstrap native baselines and transitional callers
scheduled for removal. It must not be a normal escape hatch that erases history
or promotes a resolved value into authored truth.

### Group And Diagnostic Projections

For each group/member pair, retain its contributing declarations. The visible
member occupies the earliest active contribution's position in the group's
ordered sequence; removing that contribution may reposition it to the next one.
Static registry membership retains its static position. Group membership is not
a last-writer scalar property, and duplicate contributions must not create
duplicate menu entries. Only applicable, valid declarations contribute.

Maintain reverse membership keys on contributions so withdrawal can dirty the
affected groups. Rebuild a dirty group's ordered projection once per transaction,
not all dynamic groups for every field setter. Group reads have receipts just
like descriptor reads; a cached variant menu cannot remain valid because only
the descriptor revision was checked.

Diagnostics belong to producer revisions and carry source location, selected
binding, selector, reason and any relevant preceding contribution. Publish their
changes once; caret queries do not log them. Invalid source declarations,
unprepared contexts and failed worker tasks remain separate statuses.

### Applicability And Fact Status Are Different

Each authored contribution has one applicability state:

| State | Meaning | Resolution effect |
| --- | --- | --- |
| Pending | No callable is available for an eligible macro-field rule | Retain its owning attachment; contribute nothing |
| Dormant | A binding-local rule belongs to a different binding than the selected one | Retain it for a checkpoint that selects that binding; contribute nothing |
| Active | The attachment applies and the entire declaration validates | Contribute fields and groups at its declared precedence |
| Invalid | Syntax, selector, layout or authority constraints fail | Publish a source diagnostic; contribute no part of the declaration |

Withdrawing a producer removes its contribution from the new state rather than
adding another permanent "deleted" entry. Old snapshots may retain their old
state. A changed binding can reactivate a valid package default; it cannot
transfer an already attached local rule. Pending rules attach once in their
own slot, not to every later definition with that spelling.

Fact status is separate: a prepared fact may be resolved, awaiting inference,
dynamic, or unavailable because its context is not prepared. An invalid authored
rule can coexist with a valid lower-priority fact. A dormant rule is not an error,
and a worker timeout must not be reported as malformed document structure.

## Queries And Pending State

Hot queries consume a prepared view. They do not execute Scheme, scan the
environment, perform I/O, enqueue analysis or repair missing context.

Distinguish resolved, pending, dynamic/ambiguous and invalid-declaration states.
Missing preparation is a caller lifecycle state, not malformed source. A worker
failure is a task diagnostic, not proof that the node is invalid.

Native editing and explicit shipped policies remain available while optional
inference is pending. An operation requiring an unestablished mapping is
unavailable for that reason; it must not guess a field or expose a structural
descriptor as editable macro text. A previous frame may remain visible, but
edits use mappings valid for the current source revision.

### Avoid A Layout And Inference Cycle

Normal expansion creates actual display occurrences; it must not await a
field-wide inferred editing contract before it can create them. Conversely,
the analyzer consumes detached definitions and explicit contracts, never boxes
or a forced layout. This removes the possible cycle in which layout waits for
inference while inference waits for layout to discover the editable fields.

| Operation | Required authority while inference is pending |
| --- | --- |
| Native math/metadata editing | Native schema and compiled editing policy; optional inference is irrelevant |
| Typing in a displayed macro argument | A current occurrence mapping to a writable source field plus view permissions |
| Navigation between displayed occurrences | Current occurrence mapping and explicit boundary policy |
| Generic macro field insertion or transformation | Explicit signature/layout and policy, or the specific resolved inferred fact it requires |
| Menus, toolbar and footer | Already published command snapshot; no preparation or worker request inside a getter |
| XML validation and structural indexing | V3 schema and semantic fields; no requirement for editor inference or layout |
| HTML/PDF export | Normal native conversion/expansion; inability to infer editing behavior is not an export error |

An actual occurrence proves where that display points and its concrete context.
It does not prove that a parameter is displayed in every branch, nor that two
displays have the same mode. A click carries the displayed occurrence identity;
keyboard movement carries the selected identity until it crosses a boundary.
For a source-only jump with several displays, preserve a still-valid selected
occurrence or use an explicit navigation destination. Without either, prepare
the source-level context and leave display-specific operations unresolved;
do not pick the first match. This is particularly important for repeated macro
arguments, AVD and transclusion.

### Minimal Consumer Interface

Use a small typed interface rather than reproducing every DRD getter and setter.
The following are responsibilities, not a requirement for another class hierarchy:

| Interface | Allowed work |
| --- | --- |
| Schema lookup by native kind | Read immutable shape, fields and metadata types; usable without an editor |
| Owner preparation for a source/occurrence | Evaluate an unprepared region through the existing owner, capture context, publish mappings and optional analysis demand |
| Prepared field lookup | Read contract facts and their status from a retained immutable view |
| Command applicability | Combine prepared facts, selection and permissions without mutating or scheduling |
| Native edit operation | Resolve current source addresses and perform the existing atomic edit |
| Owner analysis completion | Validate receipts and publish accepted facts with targeted invalidation |

A field result carries its status separately from its value. `pending` does not
mean false, inaccessible or malformed. Dynamic/ambiguous results may still carry
concrete occurrence mappings; invalid declarations identify their offending
producer while leaving native structure intact. Diagnostics expose the field,
selected binding, winning declaration and any conflicting contributors. They do
not repeatedly log one warning for every caret query.

The positional compatibility facade is a one-way projection from this authority.
New consumers must not write through it or cache a second independently mutable
descriptor. Migrate by consumer family; remove each facade operation once its
last real caller is gone instead of adding a native equivalent for every legacy
function.

## Asynchronous Analysis, After Lifecycle Cutover

Use the existing bounded native executor and actor delivery infrastructure.
Workers receive detached immutable definitions and dependency inputs, never
live legacy trees, editor pointers or a mutable DRD environment.

Per owner, coalesce dirty work and permit one engine batch in flight. Capacity
exhaustion leaves work pending; it does not run inference inline. Capture large
changed inputs resumably on owner turns rather than pretending that deep-copying
them is free because later analysis runs elsewhere.

A result identifies the owner incarnation, binding/context, dependency receipts
and prerequisite result components. Publish only dependency-valid components;
a recursive component is atomic. Rejecting a callee candidate also rejects
callers that require it. Unrelated edits need not discard independent results.

The owner acknowledges acceptance and rejection explicitly. Consuming input is
not equivalent to publishing its result. A rejected candidate can be offered
again when appropriate even if its value has not changed. Close cancels demand
and invalidates delivery without joining a worker from the editor.

### Dependency Receipts And Context Reuse

Do not use a document-wide revision as the only analysis key, or use expression
equality as a substitute for binding identity. Each candidate records the
specific inputs it consumed:

| Receipt | Checked at publication or reuse |
| --- | --- |
| Name resolution | Consulted scope slots, including absence/non-callable results and selected binding |
| Definition input | Binding identity and detached payload revision |
| Authored property | Applicable attachment and the selected property's revision |
| Group query | The queried group's effective membership/order revision |
| Environment value | Presence and revision of the actual scalar input read |
| Analysis prerequisite | The exact result component used, not merely its macro name |
| External input | The producer's existing revision token, or explicitly non-reusable |

Negative lookups are dependencies: inserting a nearer definition invalidates a
previous outer resolution. Equal values can retain value-analysis results while
binding-attached applicability changes; reuse requires the appropriate receipts
for each, not a single equality shortcut.

Prepare an immutable resolution context handle from the evaluator's scope
boundary. A request uses that handle and its demanded binding. Cached results
may be reused in another prepared context only after their observed receipts
match. This avoids both a whole-environment context hash and one analysis per
cursor position. Dependency checks and context interning occur in scheduled
preparation/publication, never inside a field getter.

Declaration resolution is authoritative owner work. Macro-body inference is
optional worker work. A change to an explicit accessibility rule must become
visible when its owner transaction publishes; it must not wait for an inference
batch. Expensive affected-target recomposition is preparation work with an
explicit pending boundary, not lazily executed by the first menu query.

### Worker Publication Protocol

Keep the asynchronous boundary explicit:

1. The owner prepares demand from authoritative bindings and records the relevant
   slot, declaration, group and external-input revisions. Capture shares detached
   payloads already available from execution; it never hands out mutable trees.
2. A worker has exclusive access to its analysis-engine instance. A request carries
   an immutable input base plus deltas valid for that base, or a complete demanded
   input when that base is unavailable. Queue coalescing must not skip a delta
   while pretending its successor is self-contained.
3. The result contains candidate facts, their dependency receipts and prerequisite
   result identities. Computation updates worker-private reuse state only.
4. On delivery, the owner checks its incarnation and each demanded dependency.
   Source edits unrelated to these receipts need not reject the result. A recursive
   dependency component is accepted atomically; dependents require either its
   accepted candidate or the exact still-valid prerequisite already published.
5. Publish accepted facts and affected-consumer notifications in one owner turn.
   Reject stale candidates without executing source or rebuilding layout in the
   callback. Retain/reissue outstanding demand separately from computation state.

Cancellation is cooperative between bounded analysis units. A long recursive
component needs a work budget and a resumable continuation or an explicit dynamic
result; a thread-pool queue alone does not bound its CPU usage. Queue capacity,
retained payload bytes and reusable-result bytes are separate budgets. Use
existing scheduling facilities rather than another private thread per document.

AVD and transclusion subscribe to immutable source-owner publications. If the
owner is closed, persisted source can be prepared by the normal background source
loader with its own incarnation; hosts do not synchronously request editor state.
Unsaved source comes from its live owner. Host permissions/presentation never
change the source contract or become durable source declarations.

### Analysis Input And Cost Boundaries

Preparation supplies a detached resolution view for the demanded bindings and
their statically discoverable dependency closure. Literal calls, explicit rules
and relevant literal environment reads expand that closure. Unsupported computed
calls remain dynamic; the worker must not call back into a live editor or run
the evaluator to discover their targets. Capture proceeds in owner work units
and reuses frozen payloads from completed execution records.

An analysis request is identified by binding and call context, not just macro
name or the current cursor path. Its receipt records the name slots, explicit
properties and environment inputs actually used. The same `f` calling `g` under
two different local bindings of `g` has two analyses; changing unrelated text
does not invalidate either. Do not create a distinct analysis for every character
position. Context interning/reuse occurs during preparation, never by comparing
whole environments from a query.

Keep three counters distinct: submitted input sequence, completed computation,
and accepted publication. Preserve only live-demand inputs, retained checkpoints
and reusable results within the cache budget; the worker is not an unlimited
history of every visited cursor context. Result eviction can cause later demand
to become pending, but must not invalidate a snapshot retained by a command.

The cost goals have specific boundaries, not a blanket claim of constant time:

- A query reads its prepared snapshot and requested fields only.
- Same-context cursor movement does not replay the document prefix or copy the
  full environment. Boundary materialization belongs to owner preparation and
  is shared across occurrences using that context.
- Ordinary body typing does not enumerate all definitions. Definition edits
  touch changed inputs and affected dependency components; large fan-out is
  scheduled work, not hidden inside lookup.
- Layout-independent async publication dirties only consumers of the changed
  facts. It does not rebuild every frame in the completion callback.

The current `contract_event_prefix` selection and checkpoint restoration do not
yet meet the cursor cost goal: finding a new prefix and materializing its state
can walk/copy substantial data. Reuse scope-boundary checkpoints and prepared
contexts in the existing owner lifecycle before removing the fallback. Do not
describe the existing sidecar alone as a complete performance solution.

## Implementation Units And Removal Obligations

| Unit | Deliverable | Remove in that unit |
| --- | --- | --- |
| A: Evaluation lifecycle | Complete base/body/occurrence checkpoints, ordered input admission, contribution withdrawal | Environment-only contract restoration and last-cursor contract authority |
| B: Owner preparation | Explicit interactive package policies; prepared command/render views | Menu `drd_update`, contract use of `restricted_exec`, full-environment discovery |
| C: Detached analysis | Context-keyed facts, bounded scheduling, validated component publication | Synchronous live-environment analysis adapter |
| D: Consumer completion | Named fields and native policies in editor, conversion/indexing and composed views | Positional DRD facade, obsolete storage/cache encoding and remaining duplicate dispatch |

Do not begin by adding a worker around `synchronize()`. Do not keep expanding
the transitional positional API instead of completing unit A. Existing
`drd-props` source syntax can decode into native declarations without retaining
the DRD implementation; removing a persisted spelling is a separate format
decision.

Check uncertain boundaries, not registry constants: removed producers with no
variable writes; no-write `provide`; local shadow restoration; invalid replacement;
cached versus actual execution; stale async results; close/reopen; cold headless
loading. No permanent test suite or repeated full build is required for this
design document.

### Next Concrete Handoff

Finish the declaration draft against the producer lifecycle before extending its
surface. Default/value candidate tracking and the paired package cache are now
present; their missing producer/receipt boundaries still need completion. That
yields authoritative bindings and contribution deltas, not an async wrapper
around the environment scanner. In the same lifecycle unit, WITH entry tokens
and unique atomic source occurrences must be extended to the remaining missing,
folded, source-mode, compound and ambiguous positions. Complete these owners
rather than expanding the positional facade:

| Owner | Work |
| --- | --- |
| Default/package/initial-value builders | Publish complete resolution checkpoints and retire value-only style-cache restoration |
| Evaluator and local scopes | Capture producer identity, evaluated bindings, argument/source frames and inner boundaries |
| Bridge/concat expansion | Bind source fields to those boundaries; retain them across admitted replay |
| Heading-fold projection | Keep source records when hiding boxes; do not derive semantics from the screen placeholder |
| Editor preparation | Select/materialize the prepared occurrence before publishing command state |
| Declaration resolver | Retain authored contributions and withdraw superseded producers, including groups |

Then remove the contract dependence on `restricted_exec` and all `drd_update`
callers together with the obsolete path-only cursor contract cache. Only after
that ownership change should detached inference replace the synchronous adapter.
Native field-policy consumer migration can proceed independently where its
source schema is already explicit, without inventing another contract registry.

Use three observable checkpoints in delivery order:

1. Defaults, cold/warm package loading and temporary/headless scopes resolve the
   same callable/non-callable/absent slots without an analysis-time environment
   scan. Deleting a declaration-only producer removes its contribution too.
2. Compound/metadata and folded/repeated-display preparation selects a matching
   source context, with no query-time evaluator fallback. Then remove menu,
   cold-source-adoption and hybrid-inserter `drd_update` calls, not just the most
   visible caller.
3. Replace the name-keyed synchronous analysis adapter with dependency-keyed
   detached batches and accepted-result publication. Finally migrate remaining
   positional consumers and delete the DRD facade and old cache encoding.

These are implementation exit conditions, not instructions to add three test
suites. Use evidence for uncertain ownership transitions; do not build or test
merely for editing this plan.

The few uncertain acceptance cases are concrete: two displays of one macro
argument under different local bindings; a folded preceding declaration; a
WITH right-hand side with an observable assignment; and an edit invalidating
the focused mapping before a worker result arrives. Check these at the real
boundary when implementing it. This is not a request for a broad new test suite.

Completion is observable: editing, menus, rendering and headless consumers read
the same prepared authority; no query performs inference; scopes do not leak;
deleting a producer removes its contributions; and the old DRD facade is gone.
The current worktree has not reached that boundary.

### Bounded Implementation Slices

Use these slices to prevent another indefinitely growing transitional API:

| Slice | Completion evidence | Not a substitute |
| --- | --- | --- |
| A1: Declaration storage | Distinct equal executions remain distinct; removing a contribution reveals the correct rule/group; retained snapshots do not mutate | An unused withdrawal helper or only whole-target reset |
| A2: Producer integration | Deleting/replacing a declaration-only source record retires its effects through the normal bridge path; local rebinding does not inherit bound rules | A file/path label, name-keyed stack or final-environment scan |
| A3: Resolution receipts | Cold/warm packages, pending declarations and local exits select matching bindings and ordered inputs | An async wrapper around `synchronize` |
| B: Prepared consumers | All supported focused fields have an explicit context or pending state; menus and cursor queries do not execute source | A unique-atomic-only occurrence sidecar |
| C: Detached inference | Edits/close can make a result stale without accepting it or blocking the owner; native typing remains available | Running the current mutable analyzer on another thread |
| D: Removal | No production consumer uses the positional DRD authority or its mutable setters | Renaming the facade while retaining its ownership |

The current declaration storage has focused A1 evidence, including actual cached
bridge deletion after restoring its incoming checkpoint. A2 now also has evidence
for binding attachment, pending rules, local prefix replay and normal bridge
source replacement, including invalid and undecodable replacements. Remaining
preparation modes and complete dependency receipts are not covered by that
evidence. Its whole-target chain
replay and copy-on-write maps remain transitional:
first mutation after a shared snapshot can copy a whole map; a group change can
rebuild the whole dynamic group projection. Neither is bounded merely because
the public method operates on one target. Measure these costs at preparation
boundaries if they become material; do not replace containers speculatively or
take a new shared full-map snapshot for every glyph.

For A1/A2, the highest-value uncertain scenario is one real source sequence with
two declarations for a macro, a local redefinition and a cached suffix. Delete
the earlier declaration, then replace the later one with an invalid selector.
Inspect the exact selected binding, surviving fields, membership and diagnostic
after each owner publication. This exercises real invalidation/attachment, not
just direct calls to a new storage API. A focused temporary probe is appropriate;
do not create a broad permanent acceptance suite for this plan.

Do not mix these slices with changing persisted source spellings, a new package
language, `.ac` serialization or unrelated editor cleanup. Keep the same runtime
authority throughout; remove old consumers as each slice reaches them. There is
no user-visible migration step for Notes in this project.

### Next Change And Stop Condition

The normal bridge source-replacement path now has focused evidence, including
an undecodable replacement and a cached failure becoming valid after a preceding
layout declaration changes. Group membership/order/subgroup queries and syntax
fallback reads now also have focused evidence. The remaining admission audit
must distinguish contract-policy context from general rendering state:

| Observed input | Current owner and remaining boundary |
| --- | --- |
| Accessibility/writability mode | Thread-local `drd_mode.cpp` policy, consulted by tree navigation and DRD getters; make policy selection explicit in prepared command context rather than equating it with a descriptor revision |
| Frame and font-relative units | `env_semantics.cpp`, `env_length.cpp` and their native consumers; cached derived fields are not automatically reads of their original variables |
| Reference and attachment reads | Provider identity, key presence and values are recorded separately from variable reads; reference writes still prohibit cache reuse, but do not prohibit reconstructing a completed execution's variable/contract context |
| File/external callbacks | Their actual provider/context determines reuse; do not rerun arbitrary callbacks solely to answer a cursor query |

Inspect the real owner before adding receipts. Do not claim a whole-renderer
replay system is a prerequisite for DRD removal, or silently treat these inputs
as covered by the group change. Complete prepared contract-policy contexts and
classify unsupported execution reuse at its owner. Do not repeat completed
attachment/source-replacement probes or add a worker around the remaining
synchronous adapter as a substitute for that boundary.

For each subsequent change, the old owner must leave with its replacement:

| Production entry point | Replacement owner | Removal condition |
| --- | --- | --- |
| `with_package_definitions` and both WITH_PACKAGE rewrites (removed) | Normal evaluator with a captured scoped package write set | Implemented: declarations, nested inclusions, reads and body scope use one evaluated record |
| `prepare_cursor_environment` atomic/path-only selection | Field occurrence preparation in bridge/concat plus editor publication | Compound, metadata, folded and repeated occurrences have explicit selection or pending state |
| `drd_update` in menu update, source adoption and hybrid insertion (removed) | Prepared command/field publication; independent cold source identity preparation | Implemented: menus read prepared state, hybrid preparation requests detached work, source adoption no longer depends on the cursor or approximate prefix |
| `restricted_exec` for contract context | Owner preparation from admitted execution boundaries | No supported command depends on approximate prefix execution |
| Name-keyed live `macro_analysis` adapter (replaced) | Detached service owned by `edit_env`, with captured resolution receipts | Worker publication and stale-result retirement have focused evidence; large-input capture/admission still need bounded preparation |
| `drd_info` / positional `tag_info` contract operations | Native fields and prepared facts | Their last consumer is migrated; no parallel mutable authority remains |

Do not remove evaluator functionality merely because DRD also used it: the
normal layout evaluator and its rendering responsibilities remain. Likewise,
do not promise a numerical latency target from the architecture alone. The
enforceable boundary is that hot lookup cannot evaluate, scan all definitions,
copy a whole environment, block on a worker or perform I/O. Profile a concrete
remaining bottleneck when implementation evidence calls for it.

Native syntax support, `.ac` persistence and unrelated document migrations are
not prerequisites for these removals. A stage ends when its old execution path
is gone, not when a new API exists beside it.

## Scope Of This Revision

The design refinement does not authorize another Notes migration, add a
persistent cache format, or implement a second evaluator. The subsequent
candidate-input integration removes the ordinary full-value scan, not the
remaining lifecycle obligations. Complete binding/contribution receipts and
occurrence preparation, move optional analysis off the owner, and remove old
consumers in that order. Verification evidence belongs to the implementation
record; the design alone is not a completion claim.

The design-only refinement did not establish new application-runtime evidence.
Declaration history, package provenance and binding-attachment evidence are
recorded in the implementation README; they do not establish the remaining
producer-replacement, prepared-consumer and asynchronous-publication obligations.
