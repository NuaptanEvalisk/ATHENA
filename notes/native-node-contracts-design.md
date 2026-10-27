# Native Node Contracts: Design Decisions And Cutover

This is the implementation entry point for replacing DRD. Detailed semantics,
analysis transfer rules and implementation evidence live in
[`src/Data/NodeContracts/README.md`](../src/Data/NodeContracts/README.md).
This document does not declare the replacement complete. It makes the next
ownership changes explicit rather than adding another independent subsystem.

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
| Preparation | Atomic/compound entry and exit contexts are indexed during real execution and admitted replay; prepared queries no longer reconstruct event prefixes or copy complete variable maps. Physical hits carry occurrence tokens through Unicode shaping and admitted reuse, including changed roots and merged spans; completed progressive prefixes and root outer boundaries are queryable, incomplete nodes have no exit context, and edits invalidate the execution base. Metadata inputs receive detached matching values/contracts before initialization, with live slot refresh; explicit conversion and interactive graphics evaluation restore matching state in temporary scopes | Cold-position preparation, newly edited fields, pending behavior for non-admitted occurrences and removal of query fallback |
| Inference | Detached workers consume immutable binding/value/declaration resolutions; recursive groups and their callers require matching receipts; preparation retires stale published facts while new results are pending; analysis status is separate from schema types; exact admitted display hits can enter macro fields without waiting for inferred access, while authored/native restrictions still apply | Budgeted large-input capture/admission and remaining pending-state consumer policy |
| Consumers | Native cursor navigation, search and selection receive explicit access/writability policy; all DRD read getters, global access modes and `drd_update` are removed; identity planning takes an explicit native scope; the refcounted `drd_info` owner is removed, and editor/evaluator share one native variable/contract state; menus no longer request analysis; cold identity preparation has its own style/init/preamble context, while metadata buffers do not create a document identity graph | Remove the isolated `restricted_exec` variable-query fallback; complete missing-occurrence preparation and remaining ambient query selection |

Interactive environment refresh now requests detached analysis instead of
running `synchronize` inside `typeset_preamble`. Cold style/source identity and
offline preparation still use explicit synchronous analysis. Command/focus
snapshots remain unavailable without an admitted cursor context; publication
does not infer a mode by scanning source. Native action dispatch prepares dirty
layout before checking those snapshots, matching keyboard dispatch. The right
footer retains its last prepared field description while document statistics
remain independent. Page-dimension getters read committed layout geometry, not
the currently selected field's evaluator.

Native delimiter, accent and variant content now uses the same environment
scopes in layout, complete evaluation and both partial-evaluation routes. This
does not authorize partial evaluation from a getter. MathBlock display scopes
and MathGrid row numbering/label scopes are now shared by layout and AVD
semantic projection. Direct table, row and cell layout also records its source
boundaries, including wrappers that bypass the generic concater. Cold/newly
edited table-field preparation still remains separate work.

Macro completion selection now captures the invocation context before replacing
the name. Argument preparation and recursive activation retain that bounded
context, and command lookup happens before the replacement. The name field's
display-only text style is not the invocation environment. General compound
commands and command actions that query a newly created field still need their
own prepared inputs; this is not a blanket pre-edit environment fallback.

Inferred projections carry detached input/dependency evidence through scope
inheritance and local restoration. A new analysis owner validates those receipts
instead of clearing all inherited facts while its worker runs. Effective values
and evidence use separate persistent maps, so renewing a receipt does not make
every unchanged contract a value delta. This resolves that particular cold-fact
loss; source identity handling of genuinely pending/ambiguous inference remains
unfinished, as do query fallback removal and bounded large-input admission.

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
