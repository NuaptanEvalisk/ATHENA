# Native Node Contracts

This replaces DRD's mixed structure, editor, macro-analysis and Scheme group
registries. It does not change the v3 document format or migrate the user's
vault. The implementation is being integrated by ownership boundary, removing
each old authority when its replacement is connected.

## Reading This Design

Start with [design decisions and cutover](../../../notes/native-node-contracts-design.md)
for the implementation sequence and its concrete removal obligations. This file
contains the detailed protocols and the chronological implementation evidence.

The entry-point design also specifies the complete definition-input boundary,
retirement of value-only style caches, declaration attachment/precedence, and
the separation between actual display editing and optional field-wide inference.
Those are target decisions, not additions to the implementation evidence below.

The model and protocols below describe the target, not capabilities inferred
from class names in the worktree. `Integrated` and the subsequent transition
notes record implementation evidence; the latest worktree status is at the end.
In particular, immutable storage and replayable events do not yet establish
complete input coverage, prepared occurrence selection or asynchronous
publication.

The replacement has three externally meaningful boundaries:

1. The evaluator prepares a matching environment/contract checkpoint and records
   the effects of actual execution. This is the only definition authority.
2. Native analysis fills missing facts for those bindings and contexts. It does
   not discover definitions by running macros or scanning a GUI environment.
3. Commands, menus and render consumers read prepared snapshots. A getter cannot
   repair a missing checkpoint, enqueue analysis, or silently reuse an old fact.

No new document format, vault migration, generic rule language, mandatory cache
database or user-facing update command is part of this replacement.

The implementation priority is correctness of prepared state, then removal of
query-time work, then asynchronous inference. A new descriptor class, frozen
payload, or background task alone does not complete any of those boundaries.
The next bounded deliverable is specified under `Next Lifecycle Cutover` below;
do not keep extending the positional adapter instead of completing that unit.

## Model

- Structure contracts name fields independently of their physical storage:
  child slots, repeated children and typed metadata properties. Existing v3
  validators remain authoritative until their constraints are moved here;
  there must not be two independently maintained validators.
- Editing policies govern semantic cursor stops, insertion, deletion and
  selection. Existing native math/table policies should consume named fields;
  they are not replaced with another scripting language.
- Presentation contracts describe inline/block behavior, child environments
  and source-to-render mappings. Styles cannot rewrite core structure rules.
- Groups and transformations provide semantic categories, ordered alternatives
  and explicit conversion operations. Display names and static relationships
  are data; changing a kind with a different structure requires an operation,
  not an unchecked tag rename.

Content, editability, search participation and link-target identity are separate
properties. A metadata title can be editable and searchable without receiving a
UUID or participating in radioactive matching. Peek/transclusion permissions
come from the view context, not from mutations to a shared node descriptor.

## Ownership

Built-ins are immutable. Styles and document-local definitions contribute
scope-specific descriptions with explicit precedence and declaration locations.
Commands and render operations use one matching definition/contract generation.
Do not publish new macro definitions with an old editor contract. A matching
contract can have pending inferred properties: generation consistency does not
mean waiting for analysis before accepting an edit or rendering the new source.

Core nodes require explicit declarations. Macro analysis is limited to known
composition and parameter mappings, considers every branch, and reports
ambiguity rather than guessing. Updates follow declaration dependencies instead
of scanning the full environment on menu refresh. Background analysis receives
detached data, never another actor's reference-counted legacy tree handles.

Reusable style results may be cached; document-local results can later use the
planned disposable .ac cache. No on-disk cache is a schema authority. Queries
must not perform I/O, Scheme calls or inference once startup has loaded data.

## Dependencies

The existing nlohmann JSON implementation (MIT) owns JSON parsing and the
existing v3 codec/node model owns AST and typed metadata handling. Guile's
reader was used for the one-time extraction of Scheme declarations, not a
handwritten Scheme parser. General JSON Schema libraries describe JSON values,
not ATHENA's live editor addresses, observer-aware mutations or macro scopes;
they do not replace these domain contracts. No new parser or dependency is
introduced.

## Why The Existing Model Must Change

DRD is already substantially native: `Data/Drd/drd_info.cpp` and `tag_info.cpp`
implement its core. Removing Scheme declarations alone is not the replacement.
At the start of this replacement, the implementation coupled several different
authorities:

- `tag_info` packs cardinality, cursor borders, accessibility, environments and
  freeze flags into one positional descriptor. Metadata fields are outside
  this model. Base cardinality has six bits and repetition has four; these
  encoding choices must not become schema restrictions in the replacement.
- `drd_info_rep::arg_access` examines only the first result branch of IF and
  VAR_IF, and can select the first accessible occurrence of an argument.
  Neither choice establishes a contract for every rendered occurrence.
- `heuristic_init` repeatedly scans the whole environment, stopping after up
  to eleven passes. `edit_interface_rep::update_menus` invokes `drd_update`,
  which executes the environment up to the cursor and runs this inference.
  A menu query consequently depends on computation and mutation.
- Setters silently ignore frozen properties. Their history is not retained,
  so callers cannot explain which declaration won. The current var-type
  setter and freezer even use `freeze_with`, coupling two distinct properties.

These are concrete reasons to replace the model, not just rename the classes.
This design does not propose a new typesetter, macro language or general-purpose
constraint solver.

## Contract Shape And Authority

A resolved node contract is composed of four parts; they need not be four
independently allocated runtime objects:

| Part | Owns | Does not own |
| --- | --- | --- |
| Structure | Named fields, storage bindings, cardinality, value types, semantic roles | Colors, view permissions, inferred source identity |
| Editing | Cursor stops, selection units, insertion/deletion policy identifiers | Font metrics or executable scripts in JSON |
| Presentation | Inline/block behavior, field environment overrides, mapping requirements | The persisted node's shape |
| Relationships | Ordered groups, alternatives, available transformations | Permission to change a tag without converting its fields |

`NodeKind` identifies a native label or a scoped macro. `FieldId` identifies a
logical field, not a physical child number. A field's storage binding describes
one child, a repeated child range, or a typed metadata property/list element.
Repeated layouts explicitly describe prefix, repetition unit and suffix. The
AST's existing child order stays unchanged; no migration follows from adding
these names.

For example, `scripts.base` and `scripts.right-subscript` bind existing children;
`enunciation.body` binds the existing body, whereas `enunciation.name[i]` binds
metadata rich text. An absent optional metadata field is not an invalid node or
a fictitious child. An empty on-screen placeholder creates the field only when
an edit requires it, using existing property conventions.

The existing v3 grammar and metadata schemas are the starting authority:
`math_tree_v3`, `document_node_model` and the enunciation registry. Initially the
contract API delegates to them. When a constraint is moved into a shared
descriptor, the old validator becomes a consumer of that descriptor; it must
not retain a second table. Cross-field mathematical constraints remain native
validation functions. XML remains responsible for encoding, not editor policy.

Static groups, labels and declarative field facts belong in data where they
are not already owned by a registry. Native code owns algorithms and domain
constraints. Policy identifiers select compiled implementations; JSON does not
contain function names to evaluate, expressions or a miniature editing language.
Existing registries are referenced, not copied into a new JSON file.

## Resolution And Scope

Each resolved property records its value, origin and applicability. Origins
include a built-in descriptor, a package declaration, a document/local
declaration, or an analysis result tied to a definition revision.

There is no universal last-writer-wins rule:

- Core structure and semantic editing invariants cannot be overwritten by a
  style. A style can change a fraction's appearance, not turn its numerator
  into a delimiter descriptor or make Backspace expose implementation data.
- Presentation properties use the existing package inclusion order, followed
  by document and lexical overrides. Later explicit overrides are recorded
  with their provenance. Duplicate contradictory properties within one
  declaration are errors, not order-dependent accidents.
- A user macro's signature comes from its definition. Its explicit editing
  declaration overrides inferred editing information only when consistent
  with that signature. Analysis fills gaps; it never replaces explicit facts.
- Entering and leaving a local definition scope changes the snapshot used for
  that region. Redefining a macro invalidates analysis of its old body, even
  when its name and arity are unchanged.

Group membership has an ordered list for presentation and a membership index
for hot queries. Scope additions are merged once, not traversed on every
keystroke. A node may belong to several groups. Variant commands name the
relevant transformation family; they must not choose an edit operation merely
because a recursive group search happened to find that group first.

`node_group_scope` queries native ordered group extensions. The former DOCUMENT
attribute storage is removed. Evaluation checkpoints still need to connect
these extensions to the actual declaration/scope lifecycle.

### Concrete Ownership And API Boundaries

The following are target interfaces, not claims that the current adapter already
implements them. In particular, a priority enum on mutable DRD properties is
not the final resolution model:

| Owner | State | Consumers |
| --- | --- | --- |
| Process | Immutable core field schemas, policy IDs and group data | All threads |
| BufferActor or headless document owner | Definitions, source anchors, scope checkpoints, definition revisions | Its own evaluator and editor |
| Contract snapshot | Immutable resolved facts, dependency versions, provenance and analysis status | Commands, layout, workers and indexers |
| Analysis worker | Detached changed definitions and dependency summaries | Returns a result packet to the owner |
| View | Occurrence mappings, interaction permissions and pinned frame version | Hit testing, focus and navigation |

A resolved lookup takes a scope handle and node kind. Native kinds use a stable
native key; extension names resolve to a definition binding within that scope.
A global interned tree label identifies a spelling, not a definition. Two
documents defining the same macro name must not share inferred facts merely
because their tree labels match.

Field lookup returns a descriptor and storage binding. Resolving that binding
against a particular source node produces an owner-local address. Workers may
retain descriptors, never those addresses or source observers. Mutation remains
inside the editor's existing transaction/undo machinery; the registry is not
a second AST and does not perform edits.

Typed property rules live in the contracts layer together with their existing
definitions, not in an additional parallel schema. The document model, codec,
property dialogs and title-slot editor consume those rules. `native_fields`
does not depend on the higher-level document identity/transaction model.
Domain validators remain compiled functions referenced by the owning node
schema; parsing XML, validating source and deciding cursor stops stay separate.

### Resolution Decisions

Authority and overridability are independent. `builtin > declared > inferred`
is useful as a transitional guard but insufficient as the final algorithm:

| Property category | Resolution rule |
| --- | --- |
| Native shape, field binding, required metadata | Core schema only; incompatible declarations are rejected |
| Macro signature | Evaluated definition owns the formals/layout; a declaration may constrain but not silently change it |
| Semantic editing invariant | Compiled policy of the node kind; a style cannot expose descriptor children |
| Presentation default | Core supplies the default; ordered package/document/local declarations may override it |
| Inferred behavior | Fills an undeclared fact for the exact binding/context revision |
| View permission | Intersects resolved editing capability; read-only/peek cannot be overridden by a declaration |

Each property kind declares its resolution rule in native code. Do not build a
universal configurable merge language. A declaration is resolved and validated
as one unit, then installed; changing arity followed by accessibility must not
expose the intermediate descriptor. An invalid replacement reports the failing
field and remains an invalid declaration, not an unreported reuse of its old
contract. Core source editing and unrelated valid contracts remain usable.

Provenance includes the declaration anchor/package, its revision, the winning
scope layer and direct inference dependencies. Retain the relevant overridden
declarations through their scope entries for an explain query; do not copy an
ever-growing override history into every field lookup.

Native field names are stable API keys, not stored UUIDs or process-global
numeric ABI promises. Descriptor indices may be compact within one immutable
snapshot. A command retaining such an index also retains its snapshot; a cache
from another descriptor generation must resolve names again. Repeated field
addresses use existing source observers, not an ordinal presumed stable after
insertion or deletion.

### Scope Checkpoints And Definition Events

An environment write is not necessarily a new declaration. Current evaluation
replays assignments, restores patches and temporarily swaps WITH bindings.
Hooking every `write_update` or `assign` call to a new analysis revision would
turn repainting and cursor movement into perpetual invalidation.

The owner records declarations by source anchor, declaration revision and
evaluation context. Package declarations also record package identity and
inclusion order. Sequential assignments create scope checkpoints for the
following region; WITH introduces temporary bindings and exit restores those
bindings, while preserving writes to other names made by its body.
The lookup scope must reproduce existing evaluator binding semantics, including
calls under a local override; the analyzer must not assume definition-site
lexical binding where evaluation uses the active environment.

Re-evaluating an unchanged declaration in an unchanged input context reuses its
binding/checkpoint. Editing, removing or undoing a declaration invalidates the
affected region and its dependent bindings. Moving the cursor into an already
evaluated region selects its published checkpoint without re-inferring it.
An unevaluated region is prepared by the normal owner-side environment work,
not by a contract getter or menu query.

The evaluator distinguishes declaration application, scope entry/exit and
checkpoint restoration. Cached bridge environment patches must carry the same
scope effects; an assignment hook alone cannot observe their replay. This is a
native evaluation lifecycle, not a second parser that tries to discover every
assignment by rescanning the document.
Dynamic definitions produced by normal evaluation are captured with their
actual binding and context; the analysis worker never executes the producer.
Changes in dependencies of that producer invalidate the resulting binding.
If dependencies cannot be described statically, that definition's analysis is
marked dynamic, rather than reusing a result inferred from one observed value.

Scope snapshots share unchanged immutable entries. Do not copy the entire
environment for each checkpoint or retain every historical snapshot forever.
Old snapshots live only while a command, frame, worker packet or existing undo
state actually retains them. Closing a document cancels queued analysis and
prevents publication into its former owner; no GUI thread waits for completion.

A dependency key is a resolved binding in a scope, not merely a macro spelling.
For example, a wrapper calling `g` under `WITH(g, ..., body)` cannot reuse an
analysis obtained when `g` resolved in the parent scope. Initially, results are
scoped to their checkpoint. Sharing across scopes is allowed only when their
recorded binding/environment dependencies resolve identically. Do not use a
whole-environment hash as a shortcut for that dependency check.

Record unsuccessful lookups too: an unresolved call to `g` depends on the
resolution slot for `g`. Defining or shadowing `g` must invalidate that result,
even though there was no callee definition to attach an edge to earlier.
Checkpoint restoration and undo select existing source/binding state, but each
publication has a monotonically distinct owner-local generation. A late result
cannot match merely because a name, path or revision counter was reused.

## Editing Addresses And Render Occurrences

An editing address consists conceptually of an owner-local node anchor, a
`FieldId`, an optional repeated-item position, and a position inside the field.
The anchor uses existing observer-aware editor identity. It is not a request
to assign every node or metadata item a persistent UUID. Structural edits
rebase addresses through the existing mutation machinery; a list index or
source path is not a stable identity by itself.

A render occurrence separately records the source field, its owning document
and scope, the expansion occurrence, and the generation of the mapping. One
source field can appear several times. Hit testing chooses an occurrence;
editing still modifies the one authoritative source. Generated punctuation and
decorations are explicitly not source text. Existing inverse paths and bridge
machinery remain in use while these distinctions are made explicit.

This separation matters for:

- Empty subscript and semantic slots: a visible placeholder has a real field
  address and baseline, not a guessed neighboring source path.
- Focus outlines: geometry comes from occurrences of the selected field/node,
  not a range from the document start to the cursor.
- AVD and transclusion: the host supplies layout context, while source shape
  and definition scope come from the owning document. Displaying a source
  under host fonts does not reinterpret its semantic fields.
- Peek and read-only views: the view restricts interaction. It cannot mutate
  the shared contract or grant editing permission to a read-only source.

Semantic navigation and visual navigation remain distinct. The editing policy
supplies legal stops and boundary transitions; the typesetter supplies geometry
for Up/Down and pointer hit testing. A boolean `accessible` cannot do both jobs.

Reuse existing native math, table and slot editing implementations. Policies
express their existing operations rather than replacing them with generic tree
deletion: enter an inline formula before deleting its content, remove a native
symbol atomically, unwrap an accent appropriately, navigate grid cells/rows,
and preserve the chosen nested-script field. A structure-changing variant is
an observer-aware conversion within the existing undo transaction. A tag-only
rename is allowed only for a declared shape-preserving transformation.

Empty authored fields are valid editing states when the v3 grammar permits
them. Semantic incompleteness is not structural corruption. Commands must not
manufacture invalid INACTIVE wrappers around native math nodes. Save/export
must still report genuinely invalid source, rather than silently repair it.

## Semantic Roles Are Not Permissions

Keep the following decisions separate, even when their defaults coincide:

- Is this authored content, descriptor data or a generated value?
- Can this view enter and edit it?
- Is its text included in search or artifact extraction?
- Can radioactive matching decorate this particular displayed occurrence?
- Is it a persistent link target, or does its enclosing semantic node own it?

Consumers query common field roles and apply their own documented policy. The
contract registry does not acquire hardcoded knowledge of every indexing
feature. For example, enunciation names/attributions contribute to artifact
matching, their editable slot occurrences suppress radioactive decoration, and
their owner enunciation remains the target identity. These are compatible
decisions, not exceptions to a single `content` flag.

## Incremental Analysis And Publication

Built-in nodes and shipped macros with an interactive editing surface need
explicit contracts. Static macro analysis is a convenience for user-defined
composition, not a prerequisite for ordinary core-node editing. Internal
rendering helpers need no invented editable interface.

Analysis understands argument forwarding, known node composition, literal
environment overrides and statically resolvable calls. Every conditional
result branch and every argument occurrence participates. Compatible facts are
joined; differing environments or visibility are retained as conditional or
occurrence-specific information, not arbitrarily taken from the first branch.
Unsupported dynamic calls require an explicit contract for the affected
behavior. Analysis does not execute EXTERN, Scheme or arbitrary macro code.

Definitions and relevant environment keys form a dependency graph with reverse
edges. Changing a definition invalidates its dependants. Ordinary body typing
does not. Scoped definitions in the body invalidate their affected scope;
formatting-only changes invalidate presentation dependencies, not core schema.
Recursive components are handled together using a finite analysis domain:
information only advances toward a fixed point or an explicit ambiguity.
There is no arbitrary eleven-pass limit and no full-environment menu scan.

### Analysis Facts And Join Rules

Analysis status and a property's inferred value are separate. Status is pending,
resolved, unsupported-dynamic or invalid-declaration. A resolved result can
itself report ambiguity for a particular behavior. Missing analysis is not a
negative fact: pending accessibility must not be published as inaccessible.

For each formal field, the analyzer records:

- Uses in source-content positions versus descriptor/data positions.
- Whether an editable occurrence exists on every branch, some branches, or
  none. A branch without the parameter participates as absence.
- Applicable value constraints and environment overrides by occurrence.
- Direct dependencies on resolved callee bindings, explicit declarations and
  environment keys used to establish those facts.

One occurrence in text mode and another in math mode is not a reason to choose
the first mode, nor a reason to reject an otherwise valid macro. The occurrence
mapping supplies the active mode for direct editing. An operation requiring a
single field-wide mode is unavailable until an explicit policy resolves it.
Likewise, conflicting inferred types are reported as conflicting constraints;
they are not silently widened into permission for arbitrary tree mutation.

The fixed-point domain contains finite classifications and literal/symbolic
environment facts present in the analyzed input. Equal facts join to themselves;
different values join to a conflict summary with declaration/occurrence evidence.
An argument-dependent override can retain a formal-field reference, but the
analyzer does not evaluate it. Unknown computed expressions are dynamic facts.
Recursive propagation stores summaries and direct dependency edges, not ever
longer expanded call paths or copied occurrence lists. Cycles therefore cannot
grow the analysis domain indefinitely. Concrete repeated occurrences are
created by expansion and belong to the render mapping, not the fixed point.

### Transfer Rules, Not One Generic Merge

Control-flow combination and successive worklist refinement are different
operations. The implementation must not use one boolean OR or one map merge
for both:

| Operation | Occurrence rule |
| --- | --- |
| Sequential, unconditionally rendered children | An occurrence is guaranteed if at least one child guarantees it |
| Conditional alternatives | An occurrence is guaranteed only if every possible alternative guarantees it; missing else means absence |
| Callee parameter forwarding | Compose the callee's occurrence summary with the caller context, without inventing a source occurrence |
| Repeated arguments | Use the actual repetition range/layout; identity forwarding differs from a call that also receives the item index |
| Unresolved or dynamic expression | Preserve the affected fact as pending/dynamic, never as proven absence |

Environment summaries distinguish inherited value, explicit literal,
formal-field reference, conflicting occurrences and unresolved computation.
An absent key in a *complete* summary means inheritance; an absent result from
an unfinished callee does not. Only complete, compatible occurrence facts
produce a field-wide override. Intermediate inherited values must not become
permanent conflicts when a callee later resolves to a literal.

The finite analysis domain is defined over classifications and references to
input expressions, not arbitrarily constructed trees. A recursive formatting
operation must not repeatedly concatenate `tformat` trees to search for a fixed
point. Retain a symbolic context operation or classify the dependent fact as
occurrence-specific/dynamic. Actual evaluation still renders the expression;
the analyzer is not an alternative evaluator.

Keep source validity separate from analysis precision. A valid macro used in
both text and math is not an invalid source node. Incompatible inferred usage
constraints affect operations needing a single contract, with an explanation;
they must not become a schema error merely by storing `TYPE_ERROR` in the old
descriptor. Conversely, an explicitly malformed native math node is a source
error regardless of whether any inference has run.

The worker uses a dependency worklist and recomputes affected strongly connected
components. Redefinition starts those components from explicit facts; it must
not retain old inferred facts simply because the previous solution was more
informative. Removed calls remove reverse edges too. Unsupported constructs
affect only the facts that depend on them, not all facts for the document.

Publication must address the period before analysis finishes, not merely reject
stale worker results afterward:

1. On the BufferActor, apply the definition change and obtain its explicit
   signature/declarations. Publish these with the new definition generation.
   Inferred facts dependent on the changed definition become unresolved in
   that same generation; old facts are not carried forward as current.
2. Queue only the changed definitions and relevant dependency data. Coalesce
   superseded jobs. Worker input is detached standard-library data, not shared
   legacy `tree`, `string`, observer or editor handles.
3. While inference is pending, ordinary source editing continues. Behaviors
   established by explicit contracts remain available. Operations requiring
   unknown inferred facts are unavailable with a precise reason. A generic
   argument editor remains available deliberately; normal Backspace does not
   fall into raw macro editing as a fallback.
4. Return the inferred delta and the exact scope/definition dependency
   revisions used. The owner publishes it only if those revisions still match.
   Unrelated text edits do not discard useful analysis.
5. Each command and layout job pins a matching definition/contract snapshot.
   New mappings replace old mappings at publication boundaries; pointer events
   on a retained frame are resolved/rebased from that frame's source mapping,
   never interpreted using unrelated new child positions.

Queries are const lookups. They neither start inference nor wait for a worker,
execute Scheme, load files, or mutate descriptors. GUI menus consume published
command state. Background indexers consume the same detached contract facts;
they do not evaluate a different offline approximation of the document schema.

Snapshot sharing applies only to immutable, thread-safe data. Macro trees and
live expansion state remain actor-owned. The paired generation is the contract;
it does not require making the legacy tree implementation globally thread-safe
or cloning the entire document on every edit.

### Publication Protocol And Noninteractive Consumers

Each owner publishes a bundle containing the scope revision, definition
bindings, explicit contracts, and current inferred deltas. A command captures
one bundle at entry. Definition-changing commands publish their next bundle at
transaction completion, with dependent inferred facts already invalidated.
There is no interval in which a new body advertises an old inferred contract.

Worker input is a detached native representation of only the changed
definitions and required dependency summaries. Its header identifies the owner
lifetime, scope, definition revisions and explicit-contract revisions. These
are logical versions, not timestamps or per-edit hashes. The returned packet
lists the versions it read, including prerequisite analysis results when those
results supplied a fact. Publication compares those dependencies, not the
whole document's text revision; unrelated body typing must not starve analysis.
Superseded requests coalesce by binding/scope, with cancellation checked between
analysis units. The result enters through the existing owner's command queue,
never a worker mutation of editor state.

One bounded process-wide analysis executor serves owners; do not create a
thread per document, definition or menu refresh. Each owner has at most one
active analysis batch and a coalesced dirty set for its next batch. Work units
yield between components/definition traversals so a large style does not starve
other documents. Cancellation also applies during input preparation. Closing an
owner invalidates its lifetime token; completion posts by identity and cannot
keep the editor alive or wait for its destruction.

The result packet groups dependent inferred facts by dependency component.
Publication validates and installs a component atomically, never half of a
recursive solution. Independent still-current components may be retained when
another component is stale; a component depending on a rejected component is
not independent. Read sets include binding resolution slots,
explicit-contract revisions and relevant environment keys, including negative
lookups. A rejected component returns to the coalesced dirty set; it does not
trigger a synchronous recomputation in the completion callback.

Detaching inputs is part of the performance design, not free work merely
because subsequent analysis runs on a worker. Reuse immutable detached forms
for unchanged definitions. Prepare changed inputs in bounded owner tasks, not
by serializing the complete style environment from a keystroke handler. A
partially prepared definition is revision-tagged and is abandoned if edited
again; no worker reads a concurrently changing legacy tree. Initial package
preparation happens during package loading and is reusable across documents.
Publishing a result installs immutable entries and schedules affected work;
it does not immediately retypeset every dependent occurrence in that callback.

Worker inputs use native scalar/layout facts and the existing `node::frozen_tree`
ownership mechanism for changed definition expressions and environment values.
That mechanism captures an immutable copy and hands readers independent trees;
the engine's working trees belong exclusively to its analysis owner. Reuse it
instead of introducing a second expression codec that could lose metadata or
misinterpret a native mathematical node. Snapshot comparison must use the
const-reference frozen comparator, not ordinary tree equality, which copies
non-atomic array handles. No live tree handle, observer, DRD handle, XML or
Scheme text crosses the analysis interface. This copies changed definitions,
not the document AST or the whole style environment on every request.
Existing evaluation establishes the definitions; the worker does not execute
unsupported expressions or Scheme to discover additional bindings.

Define interface responsibilities before replacing their storage:

- `resolve(snapshot, kind)` and field/capability lookups are pure reads.
- `apply_declarations(owner, transaction)` creates the next scoped bundle and
  invalidates affected inferred facts before publication.
- `prepare_analysis(owner, dirty)` incrementally detaches changed inputs.
- `analyze(packet)` has no editor, GUI, Scheme or resource-loader access.
- `publish(owner, result)` checks lifetime/dependencies and installs valid
  components, then schedules the necessary consumers.

These are boundary descriptions, not instructions to hand-write Scheme glue
or add another framework. Use the existing BufferActor queue and generated
bindings. Persisted `.ac` support is optional and must not gate this pipeline.

A layout job holds a matching owner-local definition view and immutable
contract snapshot. A retained frame may remain visible while replacement work
is pending. An input event identifies its displayed frame/occurrence, then
rebases its source address through existing edits. If that source was removed,
the event is discarded; it is not redirected to the document start. The GUI
does not dereference an actor-owned tree to perform this check.

Headless export and indexing use the same resolver and analyzer, without
depending on a running GUI or its cache. Core validation and structural indexing
need no inferred editor behavior. A batch operation that genuinely needs an
inferred fact may await preparation on its own worker/owner, outside hot query
APIs. Interactive commands do not wait. Export may use ordinary expansion for
presentation without pretending that expansion establishes editable source
fields. An unsupported editor inference is not, by itself, an export error.

AVD/transclusion occurrences carry a source-owner contract binding and a
separate host presentation context. Host fonts and placement can vary; host
macros cannot change the stored source's field shape. The actual expansion
scope must be carried explicitly rather than reconstructed later from whichever
document happens to have focus. Peek restricts interaction in its view context.

### Invalidation Matrix

| Change | Required work | Work that must not happen |
| --- | --- | --- |
| Plain body text | Existing source/layout/index updates | Whole-scope contract analysis |
| Macro body or signature | Invalidate binding and reverse dependants | Reuse old inferred facts until the worker finishes |
| Explicit contract | Resolve affected facts, invalidate dependent analysis | Silently keep an incompatible declaration |
| Local binding enter/exit | Select or prepare the corresponding scope checkpoint | Modify a process-global descriptor |
| Package replacement/order change | Resolve affected package chain and dependent scopes | Trust a cache keyed only by style name |
| Font/color change | Existing presentation invalidation and any recorded analysis dependency | Revalidate unrelated core structure |
| Undo/redo | Restore source transaction and matching binding revisions | Treat replay as an unrelated new global declaration |
| Document close | Cancel owner jobs, release unreferenced snapshots | Publish to a reused owner identifier |

## Concrete Integration Protocol

This section narrows the architecture above into implementation decisions. It
describes work still to be done, not properties of the current synchronous
adapter. Do not replace `synchronize()` with `post()` and call this complete.

### Identity And Versions

Keep these identities distinct; none requires a persisted UUID on internal nodes:

| Identity | Meaning | Lifetime |
| --- | --- | --- |
| Owner incarnation | One document execution owner, including headless owners | Never reused by a later open of that document |
| Declaration anchor and revision | Authored declaration or evaluated producer, with its source/package origin | Existing observer-aware source identity plus owner-local revisions |
| Evaluated value | Immutable captured expression that several producers may share | Content storage only; pointer/value equality does not prove binding identity |
| Definition binding | The result of a particular producer revision in its dependency-valid execution context | Reused by admitted replay, replaced when its producer or relevant inputs change |
| Scope checkpoint | Effective bindings at an evaluation boundary | Immutable and shared while referenced |
| Analysis key | Definition binding plus effective call context and analysis implementation version | Reusable only under matching dependency resolutions |

A publication generation orders snapshots; it is not a global cache-validity
test. Plain text edits may publish layout/source state without invalidating
macro analysis. Undo can select an earlier immutable declaration/checkpoint,
but it does not reuse an old owner publication number.

Dynamic name lookup must match evaluation. If `f` forwards to `g`, the same
definition of `f` can have different contracts inside and outside a local
override of `g`. A first implementation keeps these analysis keys separate;
dependency-equivalent sharing is an optimization, not a prerequisite. Missing
`g` records a name-resolution slot, including its scope, so adding it invalidates
the previous unresolved result.

Do not precompute every macro in every source checkpoint. Core contracts and
explicit signatures are immediately available. Owner preparation schedules
inference for used macro bindings and their dependencies, and reuses already
resolved contexts. Getters never create those requests. A cursor moving between
existing occurrences selects their checkpoint rather than building a new one.

Separate the producer's evaluation dependencies from the definition's use
dependencies. An assignment that reads `x` to construct `f` depends on `x` when
executed. An already constructed body of `f` that calls `g` depends on the
effective `g` at the call site. Capturing the first does not capture the second.
The same frozen body may therefore back distinct bindings and analysis contexts.

Source anchors belong to the existing source/bridge lifetime. Generated
definitions identify their producer occurrence, not an invented UUID on the
output expression. Ordinal position in a previous event list is only a lookup
hint: inserting another event must not turn it into proof of common origin.
Deleting a producer retires its effects through the enclosing evaluation record;
undo may reuse a retained old binding but still publishes a new generation.

### Resolution States And Preparation

The complete runtime resolver distinguishes `absent`, `non-callable` and a
macro binding with its signature. A scalar assignment shadowing a macro is
`non-callable`; it must not expose the outer macro's editing contract. Absence
is a completed lookup with a dependency, not the lack of a cached entry.

The callable tracker alone is not a complete name resolver. Defaults and bulk
replacement callables are now populated at their input boundary, but an ordinary
scalar that never shadowed a tracked macro need not have a binding entry. A null
entry therefore does not mean `absent`. Negative lookup dependencies require
the paired value/presence layer; do not remove the old adapter until every public
checkpoint exposes that complete resolution.
The final public query API does not expose an implicit fallback to live env reads.

Completeness comes from the checkpoint builder at defaults, package execution,
initial values and explicit replacement boundaries. Restoring a paired snapshot
does not execute new assignments. Presence/non-callable scalar slots belong to
the value-resolution layer; they do not require a second frozen macro payload
or a node tag for every variable. The old style-cache environment/merged-DRD pair
has no sufficient declaration provenance. Its reader and writer have been removed;
it is not translated into invented producers. In-memory package checkpoints now
retain values and existing tracked bindings together. Completing their baseline
provenance and dependency admission remains work; new disk persistence is not a
prerequisite.

Preparation is an explicit owner operation at package load, declaration change,
occurrence expansion and cursor-context preparation. It constructs missing
checkpoints and registers analysis demand. Rendered occurrences retain the
prepared scope handle. Menu construction only reads published command state;
headless consumers request preparation explicitly outside the query API.

| Query outcome | Meaning | Consumer behavior |
| --- | --- | --- |
| Explicit/resolved fact | Current binding/context establishes the behavior | Use the fact and retain its snapshot |
| Pending inferred fact | Preparation has queued or is capturing required input | Keep explicit editing available; no synchronous inference |
| Dynamic/ambiguous fact | Analysis cannot establish this particular behavior | Require an explicit policy for operations needing it |
| Invalid declaration | This authored rule failed decoding or resolution | Report its origin and withdraw its contribution |
| Missing prepared context | The caller has not obtained a matching checkpoint | Return an explicit preparation result; never substitute the focused document's scope |

`Missing prepared context` is a caller/lifecycle state, not a source error or an
analysis fact. Likewise, analysis failure is task state with a diagnostic; it
does not label a valid macro structurally invalid. Pending and dynamic behavior
never mean `ACCESSIBLE_NEVER`, and ordinary deletion never exposes raw descriptors
as a fallback. Core nodes and shipped interactive wrappers remain usable through
explicit policies while optional inference is unavailable.

### Environment Execution And Bridge Replay

The actual integration points are broader than `exec_assign`:

| Existing path | Required contract-side effect |
| --- | --- |
| `exec_assign`, `exec_provide`, evaluated macro producers | Record the resulting declaration with producer/input dependencies |
| `exec_with` and `bridge_with` | Enter the same local binding layer; restore it on full evaluation exit |
| Partial `exec_until` into a WITH body | Select the inner checkpoint for the cursor; do not prematurely restore the outer one |
| `bridge_rep::typeset` cached path | Admit and apply combined variable/contract effects with monitored writes |
| `bridge_rep::exec_until` cached end | Admit and apply the same combined effects for end traversal |
| Preamble/default/style environment restoration | Select its captured base checkpoint, not invent fresh declarations |
| Package loading and headless evaluation | Produce the same declaration records without requiring a GUI owner |

Capture a bridge's input checkpoint, actual resolution dependencies, and
contract effects with its existing environment change cache. Reuse those
effects only when the bridge source and recorded input dependencies match.
An unrelated environment value changing does not invalidate the effects.
Replaying a cached declaration selects its existing binding; it does not emit
a new semantic revision. Invalidation and disposal follow the bridge's existing
source notifications rather than a second scan of the whole source tree.

Nested scope exit restores only the bindings introduced by that scope, matching
the evaluator. Do not restore the entire incoming environment and accidentally
discard assignments to other names. WITH right-hand sides are evaluated before
installing its bindings, as `exec_with` currently does. Use one native scope
event implementation in evaluator and bridge paths, not two copies of these
semantics.

`typeset_exec_until` currently reconstructs a cursor environment using
`restricted_exec`, which examines at most ten preceding children. That shortcut
is not a legitimate checkpoint producer. Replace the contract-related cursor
lookup with captured bridge/evaluator checkpoints and owner-side preparation
of a missing region. Do not just remove the ten-child limit and execute all
preceding source from a getter. Existing unrelated environment query behavior
can be migrated separately, but must not feed a second contract authority.

### Paired Checkpoints And Cached Effects

An evaluation checkpoint pairs the owner-local environment state with its
definition bindings, explicit declarations and contract snapshot. An environment
patch alone is not such a checkpoint. The environment side remains actor-owned;
only its detached semantic inputs and immutable contracts may cross threads.
Preamble restoration, repeated layout passes and cursor preparation must restore
both sides together. Never reset contracts to the preamble while retaining an
analysis adapter that believes the body definitions have already been applied.

The native cache record contains these distinct parts:

| Part | Purpose |
| --- | --- |
| Source anchor/revision and evaluation occurrence | Identify the executed producer, not just its textual macro name |
| Input read set | Name resolution, declarations and environment values actually needed to reuse the effects |
| Ordered variable/binding/declaration effects | Produce one matching outward environment/contract result |
| Scoped child records | Retain inner checkpoints without exporting temporary bindings as outer effects |
| Completion state | Distinguish a complete reusable result from progressive or failed evaluation |

These are records of normal evaluation, not a second expression interpreter.
An assignment effect contains the evaluated binding identity/revision; a
declaration effect contains decoded native rules and its origin. A local-scope
record identifies the temporarily bound names, their saved resolutions and the
body record. Replaying a producer does not rerun Scheme. If its input reads
cannot be established, normal evaluation must run it again; absence of a read
set is not permission to reuse arbitrary effects.

A parent references immutable child records in execution order rather than
copying every descendant event into every ancestor. This avoids depth-times-
event-count storage and preserves scope boundaries. The cache's outward effect
summary excludes restored temporary bindings; inner occurrence checkpoints are
retained separately for navigation and editing. Full replay applies the outward
summary. Partial traversal selects the matching inner checkpoint and follows
the existing evaluator when preparation is still needed.

WITH evaluation has a precise sequence: evaluate its right-hand sides using
the normal evaluator before installing the temporary bindings; save and install
those bindings; execute the body; restore just the temporarily bound names.
Declarations attached to a temporary definition cannot leak onto the restored
definition. Assignments and declarations affecting other names retain their
ordinary sequential effect. A cursor inside the body selects the inner state;
it does not perform the full-execution exit prematurely. Repeated names in a
WITH use the evaluator's existing ordered installation/reverse restoration,
not a deduplicated map with different semantics.

Check a cached record's source and input dependencies before applying either
its variable or contract effects. A mismatch invalidates that record
and delegates to normal owner-side evaluation. A complete replay reuses existing
binding identities; it does not create fresh declaration revisions. An unchanged
output value alone is insufficient for reuse when provenance or dependencies
changed. Conversely, unrelated text edits do not invalidate a matching record.

#### Ordered Read Dependencies

A flat list of names/values read anywhere in a block is insufficient. Reads may
observe earlier writes in that same block or a temporary WITH binding. Preserve
their dependency on the effect that supplies them:

- An incoming read records the resolution slot or environment key and revision
  observed in the input checkpoint. Negative reads record absence explicitly.
- A read satisfied by an earlier local effect refers to that effect/binding;
  it does not require the incoming environment to have the same value.
- A child record retains its own incoming dependencies. Parent composition
  resolves them against the effects preceding that child; it does not flatten
  them all into dependencies on the parent's starting environment.
- Temporary restoration is parameterized by the replay input. Do not restore
  concrete outer values captured during the original execution into a different
  incoming scope. Unrelated writes from the body remain outward effects.

For example, `assign x=1; assign y=value(x)` does not require incoming `x=1`.
Conversely, `provide f=...` depends on whether `f` was already present even if
the previous execution made no write. A record with no effects can still have
dependencies, occurrences and required invalidation; an empty event vector is
not evidence that it can be discarded.

Admission checks source identity/revision, completion and ordered dependencies
against a private resolution draft. It applies no live environment patch or
public contract effect until admission succeeds. The draft interprets only
captured binding/declaration/scope effects, not source expressions or Scheme.
Accepted child records may be reused without copying their descendants; the
first mismatching record returns to normal owner evaluation. Replay has a cost
proportional to the records it visits, not an asserted constant-time guarantee.

Reads of untracked external state make that producer non-reusable unless the
existing provider supplies a revision/invalidation token. Do not invent a
filesystem watcher, hash the vault, or run external code during admission.
This affects reuse of that producer, not the validity of its rendered source
or unrelated macro analysis.

On interrupted or failed evaluation, do not mark the record reusable or publish
a partial definition/contract pair. Scope cleanup restores temporary bindings
on exceptional exits. Discarding an event log by itself is not rollback of
effects already applied to the environment. Recovery resumes from a matching
checkpoint through the existing owner evaluation boundary, not by independently
repairing the two stores.

#### Checkpoint Commit Boundary

Use one evaluation transaction for the environment patch, binding/declaration
effects, and the corresponding prepared occurrence scopes. The transaction
starts from a matching checkpoint, accumulates a mutable owner-local draft and
publishes the completed bundle once. Nested captures retain child effects;
they are not independent public commits. Full and partial execution are
different completed results: a partial result identifies its cursor boundary
and cannot masquerade as a reusable full bridge result.

On failure, discard the unpublished draft and recover evaluator state from the
same paired checkpoint. WITH cleanup alone does not undo unrelated body writes.
Recovery must account for those writes through the existing environment patch
machinery before another command or layout uses the state. This promises
consistency of ATHENA's evaluation state, not rollback of external side effects;
do not rerun external operations merely to reconstruct contract metadata.

Preamble/default restoration selects a complete base pair. Low-level scratch
formatting changes either remain explicitly outside declaration evaluation or
participate in a scoped transaction; they cannot silently erase definition
tracking and still advertise a complete checkpoint. Audit and classify those
write call sites once during integration, rather than teaching every low-level
write to synthesize a new declaration or scan the environment.

#### Checkpoint Selection Is Not Analysis Rollback

Keep three objects separate: the evaluation checkpoint (environment, bindings
and authored rules), the immutable facts prepared for it, and the owner's
analysis service. The service can retain dependency-keyed results for several
checkpoints. Selecting an earlier cursor scope does not roll that service back
or treat its latest result as the selected scope's result.

The current adapter's `known_contracts`/definition diff state cannot be paired
with an independently restored descriptor map. It may conclude that nothing
changed and fail to republish facts erased by restoration. During cutover,
remove this reliance on last-applied global state: results are selected by
binding/context keys, and checkpoint preparation records the exact fact set
available for that checkpoint. Missing inference is explicitly pending.

Cursor selection is read-only when its prepared occurrence exists. When it
does not, the owner prepares from the nearest valid evaluation boundary before
publishing command state; neither a menu getter nor a descriptor lookup walks
the source. A cursor-only move must not execute an assignment a second time.
An old displayed frame may remain visible during preparation, but an edit
requiring a field mapping must use a mapping valid for the current source.

Restoring the document's base pair starts a new evaluation pass; it does not
clear the analysis service or invalidate unrelated cached results. Temporary
export/expansion scopes remain separate execution owners as described below.
This distinction avoids copying an analyzer at every cursor position and avoids
reusing a body contract after resetting only the environment to the preamble.

#### Prepared Occurrence Handoff

The public preparation boundary returns a ready view, pending preparation or
an origin-bearing failure. It is distinct from a pure query on a ready view.
The following are conceptual interfaces, not declarations already implemented:

```
owner.prepare(source_anchor, field, occurrence_context)
  -> ready(prepared_view) | pending(request) | failed(diagnostic)

prepared_view.query(field, property)
  -> resolved(fact) | pending_inference | dynamic(reason) | invalid(origin)

owner.admit(execution_record, input_checkpoint)
  -> reusable(outgoing_effects, occurrence_contexts) | needs_preparation(reason)
```

`prepare` is invoked by the owner lifecycle, not indirectly by `query`. A native
field's explicit policy can be ready while optional inference remains pending.
Missing source preparation is not reported as inaccessible or invalid source.
Admission produces the effects it checked; apply them in the same owner operation
instead of validating one draft and independently reconstructing another. A
yield or mutation between admission and application requires renewed validity.

Each prepared occurrence links a source field and expansion instance to an
execution boundary, including the active argument and local-scope frames. For
a cursor inside WITH, that is the inner state before scope exit, not the block's
outward effect summary. Repeated displays of one argument can have different
contexts and geometry without gaining separate editable source identities.

Checkpoint materialization belongs to preparation. Records share immutable
children and sparse boundary references; do not copy full variable maps into
each box or glyph. Read-only dependency coalescing cannot discard occurrence
positions. In particular, the current event-vector ordinal is not a producer
identity and a coalesced read set is not a source execution trace.

The existing heading-fold projection replaces hidden source with a zero-child
`folded-hidden`. Its screen bridge therefore cannot recover that region's
semantic state. Retain source-bound execution separately from visible boxes.
For a region not yet evaluated, owner preparation uses the existing evaluator
and hands its admitted record to subsequent layout; a second whole-document
pass or a second execution of visible semantic effects is not acceptable.
Folding is a presentation choice, not source-level conditional evaluation.

Likewise, the unprepared path in `bridge_with::my_exec_until` reevaluates binding
right-hand sides. Its admitted body-entry prefix now avoids that work on a hit,
but the method is not a general pure lookup of an existing occurrence. Reconstruct
from recorded evaluated effects or select the prepared checkpoint; execute source
only as the normal owner preparation for invalidated/unprepared source. An
external effect lacking reusable inputs cannot be rerun merely to answer a
cursor/menu query. Its already committed exact checkpoint remains selectable.

Publish focus/toolbar/footer/menu state after selecting the prepared view.
The current `update_menus` order, which publishes and only then invokes
`drd_update`, must change with the lifecycle cutover. Cold source adoption and
hybrid insertion also need prepared contracts before removing their calls.
A stale frame may remain visible, but edits require a source mapping that is
current or successfully rebased by the existing observer machinery. Reject
guessed stale paths; do not block normal explicit-field typing on inference.

The immediate slice and file ownership are listed in the entry-point design's
`Next Concrete Handoff`. This handoff is not implemented by the paired cursor
snapshot or the ordered replay work recorded below.

### Declaration Validation And Replacement

Decode syntax first, then resolve field selectors against the target's actual
signature, then resolve authority, and finally install the whole declaration.
This ordering replaces the transitional sequential setter behavior:

- An out-of-range field selector is an error, not a silently ignored rule.
- Repeated properties addressing different fields are valid. Identical repeated
  assignments are harmless; conflicting assignments to the same resolved
  property/field are errors. An all-fields selector overlaps each named field.
- Compatible explicit presentation overrides follow package/scope order. Core
  shape conflicts report an error instead of disappearing behind a freeze flag.
- A declaration before its macro definition retains a target-resolution
  dependency. Signature-dependent facts remain unresolved until the definition
  is available; it does not invent a valid zero-argument signature.
- Redefinition revalidates applicable declarations against the new signature.
  A declaration on a locally shadowed binding is not transferred by spelling to
  a restored outer binding. Ordered package defaults remain distinguishable
  from declarations tied to a particular local definition.

Replacing a previously valid declaration with an invalid one publishes an
invalid-declaration result for that origin/revision and withdraws its formerly
contributed facts. Do not present its previous descriptor as current merely
because a draft failed validation. Independent core schema facts remain valid;
ordinary source editing remains available. Removing the declaration reveals
the next applicable declaration through resolution, not through reconstruction
from an already merged record. Thus authored declarations and resolved records
must remain distinct even though there is only one authoritative resolver.

A declaration's attachment is explicit: a native kind, an ordered package
default, or a particular document/local binding. A declaration made before a
definition has an unresolved attachment in its declaration scope. Resolving it
later does not convert it into a process-wide rule for that spelling. A changed
signature revalidates only applicable declarations; leaving WITH restores the
outer binding's declaration set, not the inner set copied onto its name.

Withdrawing an invalid replacement removes that origin's contribution; a
lower-precedence valid default may still supply a value. The query then exposes
that default's provenance and the invalid overriding declaration's diagnostic.
It must not attribute the old override's value to the new revision. This is
normal resolution, not a silent last-known-good fallback.

#### Signature, Layout And Selector Rules

Do not equate a macro's formal-parameter count with a new strict runtime arity
rule. `edit_env_rep::exec_compound` currently supplies UNINIT for missing MACRO
arguments and does not bind extra actual arguments; XMACRO receives the complete
application through one formal. Native contract integration does not silently
change those evaluation semantics or reject previously loadable v3 source.

Keep the following representations separate:

| Representation | Authority | Purpose |
| --- | --- | --- |
| Invocation signature | Evaluated MACRO/XMACRO definition | Formal names and argument binding |
| Source storage layout | Native schema or resolved macro declaration | Named child/property addresses and repeated/optional storage |
| Editable surface | Explicit policy plus established analysis facts | Which source fields have usable editing mappings |
| Positional projection | Transitional DRD facade only | Compatibility for consumers not yet moved to named fields |

For a plain MACRO with no layout declaration, its formals establish the default
named child fields. An explicit optional/repeated layout must have an explained
mapping to the actual source fields and definition; equality of two integer
counts is neither necessary nor sufficient. XMACRO's tuple formal is not an
editable child named after that formal: its child schema requires an explicit
layout or an established argument-mapping result. Core metadata fields continue
to come from the existing typed property schema, not macro argument inference.

New package data selects logical field names. Native property fields, child
fields and repeated field prototypes use typed storage bindings, not parsed
strings that happen to resemble paths. Old numeric selectors are decoded at
the compatibility boundary according to their existing descriptor semantics;
do not reinterpret them globally as physical child indices. A named selector
must not acquire uniform/split descriptor aliasing merely because an old
consumer uses a compressed positional projection.

Resolve all selectors before installing any rule or group contribution. Check
conflicts on the resolved logical field/property, including overlaps with an
all-fields selector. Two spellings selecting the same field cannot evade this
check. Projection to an older consumer may lose precision, but cannot rewrite
the authoritative named fields or invent editability for another field.

Before enabling stricter declaration validation, classify conflicting shipped
package declarations at their source. For example, `title-base.ats` declares
repeated layouts for `doc-author` and `doc-title-options`, whose definitions are
ordinary single-formal MACROs. Decide and correct the intended package behavior;
do not add node-name exceptions to the resolver or silently discard the rules.
This asset cleanup is distinct from changing the evaluator's calling convention.

#### Sequential Attachment And Withdrawal

A pending declaration is owned by its producer and declaration scope, not a
global queue indexed only by spelling. A later definition in that same active
scope can resolve it for subsequent checkpoints. Earlier checkpoints remain
unchanged. Finalizing a package publishes its final checkpoint; it does not
retroactively mutate snapshots captured while that package was executing.

A local declaration attached to an existing binding stops contributing when
that binding is shadowed or replaced. A package-default declaration is a
separate attachment kind: it can be considered for a later applicable binding,
but must be resolved and validated again. A pending declaration that leaves
its local scope cannot later attach to an unrelated outer definition.

Retain authored contributions independently of the merged descriptor. On an
edit, remove all contributions from the superseded producer revision, then
resolve its replacement in the unpublished transaction. This includes groups,
field rules and inferred dependants, not only arity. An invalid replacement
publishes its diagnostic and the remaining valid contributors together; it
does not keep the old group membership or half of the old field rules.

Unresolved attachment is not a syntax error. Report it when the relevant
preparation boundary needs the unresolved rule or the owning package finishes,
with the declaration origin and missing target/field. Diagnostics are keyed by
producer revision and reason, not emitted on every cursor or menu update.

### Owner And Worker State Machine

The owner keeps authoritative declarations, published snapshots, a coalesced
dirty set and at most one admitted analysis batch. A worker-owned engine keeps
detached definitions, its dependency graph and candidate results. Candidate
state is not published state. Task state (queued, running, completed, failed or
cancelled) is distinct from a property's analysis status; an engine failure is
not evidence that a source declaration is invalid.

1. A declaration-changing transaction installs its explicit signature and
   declarations, invalidates dependent inferred facts, and publishes a pending
   snapshot together. Reverse dependency edges include declaration, resolution
   and environment reads. Invalidation has work proportional to affected fact
   sets; do not claim that this fan-out is free or hide it inside a lookup.
2. Owner preparation captures changed inputs against those revisions. Each
   admitted batch receives a monotonically increasing sequence. Coalescing is
   by binding/context: create-then-delete may disappear before admission;
   delete after an admitted create must remain as a removal in a later batch.
3. The native engine consumes batches in sequence for that owner. An engine
   never runs concurrently with itself. A bounded Boost.Asio executor follows
   the existing native resolution worker implementation; scheduling does not
   share mutable DRD or GUI state or require a new dependency.
4. The worker returns candidate components, their input read sets and links to
   prerequisite candidate components or previously accepted result revisions.
   It sends the result by owner incarnation through the actor mailbox.
5. The owner accepts dependency-valid components in dependency order. Each
   recursive component is atomic. A caller may use a callee accepted in the
   same publication, or an already published matching result; it may not use a
   rejected or unpublished candidate. Invalid components remain pending and
   enter the next coalesced dirty set.
6. A publication receipt identifies accepted and rejected candidates. The
   worker may retain reusable computation, but must not treat rejection as
   successful publication. A still-applicable result must be deliverable again
   even if its inferred value did not change. Input consumption and result
   acceptance have separate sequence/status tracking.

Bound the ready queue by owners, not keystrokes or definitions. A full queue
leaves the owner's dirty set pending; it neither loses a delta nor runs the job
inline. Executor admission must wake pending owners fairly when capacity becomes
available. Completion delivery similarly retains one pending result per owner
until mailbox delivery succeeds or that incarnation closes; it cannot silently
drop the only completion and leave the owner marked busy forever.

Closing cancels future units and invalidates publication, without joining the
pool or retaining an editor pointer. The shared pool is drained/stopped at
process shutdown, not on document close. A cancelled traversal discards its
unfinished candidate component; checkpoints of completed work may remain
worker-private. Yield between traversal/worklist units, not only between SCCs,
because one large recursive component can otherwise monopolize a worker.

### Detachment And Publication Costs

`node::frozen_tree` is the existing transfer representation, but constructing it
performs a deep copy. Moving inference to a worker does not move that copy off
the BufferActor. Reuse already frozen definitions and detach only actual
changes. Do not traverse every environment entry to discover those changes.

Normal definitions can be captured as one unit. Large changed definitions need
revision-tagged, resumable capture on owner turns, using observer-aware source
anchors and private partial output. No mutable source handle escapes to a
worker or remains presumed valid across intervening edits. If the source
changes, discard that capture and coalesce the replacement. The current
one-shot frozen-tree constructor does not already supply this facility; do not
advertise a bounded preparation budget until its actual copy path supports it.

Publication installs immutable entries and marks affected consumers dirty. It
does not synchronously expand every dependent macro, serialize caches or lay
out all occurrences. Explicit contracts keep core typing/navigation available
while inference is pending. A displayed occurrence may supply concrete mode
and geometry; its mapping does not prove a field-wide property across branches.

The current COW `std::map` storage is a transition, not proof of incremental
publication cost. `contract_scope::replace` copies the local map on a shared
write; retaining a snapshot before each declaration can make a declaration
sequence quadratic. Gather one evaluation/publication transaction in one
mutable draft and freeze it once, rather than publishing after each setter.
Share immutable package layers across document owners. Intermediate cursor
checkpoints retain ordered effects and the state they actually need; they do
not each materialize a complete environment or descriptor table.

Measure changed-entry copying, input detachment, dependency invalidation and
consumer scheduling separately. A worker cannot compensate for a full-map copy
on every owner publication. If batched local layers still require copying a
large document-wide map for small edits, replace that storage boundary with a
suitable established persistent container before claiming bounded owner work;
do not hand-write a new persistent map or hide an unbounded overlay chain in
`resolve`. The exact container is an implementation decision requiring evidence,
not a new schema or public API dependency.

An analysis failure is also a completion: report one revision-scoped diagnostic,
leave the affected facts unavailable with a reason, and clear the in-flight
state. Retry for a changed input or explicit retry request, not every menu tick.
Do not convert an analysis exception into an editor-wide termination or silently
reuse the old contract.

### Publication And Lifetime Details

The owner keeps reverse dependencies for accepted facts, not only a dirty list
of changed macro names. A definition transaction withdraws affected inferred
facts before publishing the new binding. A pending candidate's additional
dependencies are validated on return; an unaccepted worker graph cannot grant
validity to currently published facts. Negative resolution dependencies cover
the consulted name slots, including inherited layers, so creating a nearer
binding or changing a previously absent outer binding invalidates the result.
Changing an unrelated name need not invalidate every lookup in that scope.

For each batch, the worker receives the acknowledged published-result revisions
it may use as prerequisites. After completion, the owner responds with a receipt
even when it accepts nothing. The next batch reconciles that receipt before
using retained candidates. If candidate `B` depends on candidate `A`, rejection
of `A` also prevents publication of `B`; equality of `A`'s old and new values
does not replace dependency validation. A rejected but still useful candidate
can be offered again with current prerequisites, without waiting for its value
to change. Do not acknowledge all results merely because the input was consumed.

Snapshots and packets retain immutable data, not their editor owner. Frame,
checkpoint, undo and worker references determine ordinary object lifetime;
there is no indefinitely retained per-keystroke history table. Owner close
removes demand and queued work and invalidates completion delivery. Existing
views may retain a detached frame, but cannot issue edits through a dead owner.
Lifetime checks do not substitute for dependency checks on a still-live owner.

AVD/transclusion preparation obtains a source semantic snapshot and host
presentation context separately. It must not synchronously ask another
BufferActor for a live environment while holding its own evaluation context.
Use prepared detached source state delivered through the existing owner
boundary; until it is ready, retain the prior frame or show pending content.
That displayed state is not permission to edit against an unrelated snapshot.
Source semantic changes and host presentation changes invalidate their own
dependencies. Peek adds view restrictions without cloning or modifying schemas.

### Data Placement And Cutover

Keep `node-fields.json` and `node-groups.json` as the native static authorities.
Built-in C++ node semantics and mathematical cross-field constraints stay in
compiled policies. Shipped macro editing/presentation declarations belong to
their package, keyed by macro and named formal field, and load as package data
with its inclusion order. They are not a second globally overriding field
schema. Preserve the existing enunciation registry as its own authority.

Classify shipped macros by their actual editing surface: native-backed forms
use native policies; interactive wrappers need explicit contracts; internal
rendering helpers do not need invented editable fields. Do not recreate every
inferred descriptor as an independently maintained JSON record. For dynamic
macro families, the native/package constructor supplies a parameterized explicit
contract with the generated signature, not an inference-dependent cold start.

Use the following cutover units and remove the replaced authority in each:

| Unit | Deliverable | Old responsibility removed |
| --- | --- | --- |
| Scoped resolution | Immutable declarations/checkpoints plus bridge replay, a DRD-shaped read facade if still needed | Writable `tag_info`/group overlays as a separate authority |
| Editor preparation | Explicit shipped contracts, occurrence checkpoint selection, declaration events | Menu `drd_update`, full-environment comparison, contract reliance on `restricted_exec` |
| Asynchronous inference | Sequenced input, native result statuses, component read sets and publication receipts | Synchronous live `analyze_environment` and conflicts encoded as schema `TYPE_ERROR` |
| Other consumers | Shared resolver in export/indexing, named fields and native transformations | Alternate offline inference, Scheme variant registries/dispatch and legacy cache records |
| Final removal | Consumers name contracts/policies directly | DRD implementation and temporary facade |

Connect declaration events before removing menu synchronization, and replace
the scoped storage before enabling asynchronous publication into it. Until
then the existing synchronous adapter is explicitly transitional; do not run
two writable systems in production as a permanent comparison mechanism.

The difficult acceptance boundaries are cache-hit versus re-executed scopes,
local shadowing with two simultaneous views, deletion/undo while analysis is in
flight, rejection of a callee result, owner close/reopen, and headless loading
without an editor cache. Instrument actual inputs/publications for these
uncertainties. A registry row count or repeated build proves none of them.

## Caching And Diagnostics

Cache resolved package contracts and analysis results with their actual
dependencies, package order, scope inputs and implementation/data version.
Do not use a document's filename or a style name alone as proof of validity.
Use existing resource invalidation and revision information; no per-keystroke
hashing of documents or vault scans are introduced.

Cache writing is queued from already prepared immutable data. Queries never
serialize a snapshot. Future .ac integration can persist document-local
analysis, but this replacement neither requires .ac to exist nor introduces
another mandatory database. Cache versions are disposable, not a compatibility
promise. A cache miss is distinct from an invalid declaration.

Diagnostics identify node kind, logical field, failed constraint and the
winning/conflicting declarations. Document errors include a source path and
nearest meaningful UUID when available. An explain query should answer why a
field is read-only, why a macro cannot be inferred, or why a transformation is
unavailable without requiring a renderer backtrace. Repeated consumers should
not log the same declaration error on every cursor or menu update; the
diagnostic belongs to the declaration revision.

## Implementation Order And Exit Criteria

1. Centralize native groups and alternatives; remove their Scheme registries.
2. Introduce field descriptors over existing v3 schemas and native structures.
   Replace bitfield/freeze storage with resolved properties and provenance.
   A temporary DRD-shaped API may forward to the new authority, not maintain
   independently mutable old and new stores.
3. Connect cursor/selection/deletion and metadata field addressing, preserving
   established native editing policies. Connect role consumers, not just UI.
4. Move shipped macro declarations to explicit contracts. Integrate scoped
   definition events, dependency analysis and matched publication. Remove
   heuristic_init and all menu-triggered contract updates.
5. Move remaining variant queries/dispatch to native transformations. Remove
   the adapter, obsolete declarations, old cache encoding and DRD implementation
   when the last consumer has moved.

This order is a migration of runtime ownership, not a bulk rename. At each
stage one implementation owns a given fact. Existing persisted v3 content must
retain its meaning; no Notes remigration or v2 compatibility project is part
of the change. Shipped .ats declarations can be migrated as repository assets.

### Removing The Transitional System

The next implementation units have concrete removal obligations:

1. Move shared property rules and remaining primitive field layouts to contracts.
   Replace the corresponding old shape tables with consumers in the same change.
   The current fixed/repeated descriptor must gain optional and
   prefix/repetition/suffix layouts before replacing primitives that need them.
2. Introduce scope bundles and declaration lifecycle integration. Replace mutable
   document/group overlays with those bundles. The temporary DRD-shaped facade
   forwards to the new authority; it cannot keep a parallel writable map.
3. Add detached dependency analysis and matched publication. Replace live
   preamble, cursor/menu and offline analysis entry points together. Removing
   `arg_access` and `heuristic_init*` is only the algorithm transition;
   menu-triggered `drd_update` must also disappear when these consumers use
   the new owner lifecycle, not merely after adding a worker.
4. Route remaining editing/variant consumers through fields and compiled policy
   operations. Remove old storage, freeze-era/cache representations and Scheme
   registration modules. Any retained Scheme UI bindings are generated native
   queries or commands, not another registry or inference implementation.

Existing persisted v3 declaration syntax, including a `drd-props` node where
present, is an input-format question rather than justification for retaining
the DRD implementation. Decode it into the new declaration model at the native
boundary. Keep no second evaluator or mutable registry behind that spelling.
Changing the persisted spelling requires a separately authorized format change;
this replacement does not silently make existing documents unloadable.

Verify uncertain boundaries with focused cases: scope replay versus a real
redefinition; IF branches with different modes; repeated argument occurrences;
recursive dependency invalidation; stale publication after edit/undo/close;
and cold export without a GUI. These checks establish the ownership and
representation invariants, not coverage targets or copies of JSON constants.
No build or test is necessary just to edit this design document.

The following are concrete semantic acceptance examples, not a requirement to
add one permanent test per row:

| Situation | Required observable result |
| --- | --- |
| Two declarations evaluate to the same macro body | Payload may be shared; origin and binding-specific declarations remain distinguishable |
| Cached PROVIDE previously saw an existing name, then that name is removed | Its presence dependency invalidates replay; it must now execute the assignment |
| A local macro shadows an outer macro and the cursor enters/leaves its body | Each occurrence uses its own binding; the outer declaration is restored without undoing unrelated body writes |
| A macro becomes a scalar, then is undone while analysis is running | No callable contract leaks through the scalar state; late publication requires the restored dependencies and current owner lifetime |
| A declaration's field selector becomes invalid after a signature edit | Its old contribution disappears and its origin is diagnosed; core editing and independent declarations remain available |
| Worker returns a caller derived from a rejected callee candidate | Neither dependent result is published as current; independent valid components may publish |
| A new view opens or headless export runs with no cache | Normal preparation supplies the same native semantics without menu refresh or a live GUI environment |

Completion means that live editing, headless conversion, indexing, AVD and
transclusion all query the same authority; no hot query executes Scheme or
performs inference; scope changes cannot leak into another document; and core
editing cannot accidentally expose structural descriptor children. Focused
checks are reserved for unresolved mapping, scope, publication and persistence
questions, not assertions that repeat JSON tables or trivial accessors.

### Next Lifecycle Cutover

The next implementation unit must deliver paired preamble/body/cursor state,
not another independent inference feature. Its boundaries are:

1. Produce a complete base checkpoint when package/default/init evaluation
   finishes. Inventory bulk environment restoration sites once; classify each
   as base restoration, temporary evaluation or tracked source execution.
   Do not infer complete name resolution from the partial producer tracker.
2. Capture producer origins, ordered reads, binding/declaration effects and
   completion at existing evaluator/bridge boundaries. Retain dependency-bearing
   records with no writes, especially PROVIDE. A removed contract-only producer
   invalidates its suffix even if its environment patch was empty.
3. Admit cached records against their source and inputs before applying either
   half of their effects. After an edit, rebuild the affected sequence from its
   nearest valid checkpoint. Stop propagation only when the outgoing paired
   state is equivalent for the remaining consumers, not when rendered text or
   the environment patch alone happens to match.
4. Resolve declaration attachment/replacement in those transactions. Publish
   the matching occurrence scopes and explicit facts; select inferred results
   by dependency-valid binding/context keys, never by the last cursor's map.
5. Route contract consumers through prepared occurrences. Remove contract
   dependence on `cur[path]` and `restricted_exec`, and remove menu-triggered
   `drd_update` when shipped interactive policies and owner preparation supply
   their replacement. Do not remove the call first and leave cold macros inert.

Only after this unit works on the owner should inference move to the bounded
worker protocol above. During the unit, the synchronous adapter is explicitly
transitional; it cannot remain a second resolver behind the new query API.
No additional disk-cache format, public scripting API or general rule engine
is required to reach this boundary.

Use concrete evidence for the uncertain transitions: deleting a contract-only
producer, replaying a no-write PROVIDE after its input disappears, entering and
leaving a local shadow, replacing a declaration with an invalid one, and
returning to a previously prepared cursor scope. Existing probe infrastructure
is sufficient; this is not a request for a new comprehensive test suite.

## Integrated

- `node_groups` reads `ATHENA/misc/node-groups.json`, validates references,
  alternate conflicts and cycles, and resolves ordered, deduplicated groups.
  The registry contains only standard-library values and is shared immutably.
- Scheme consumers call generated native bindings. Group definitions and
  alternate maps are no longer populated by Scheme evaluation or lazy imports.
- Native section navigation, deletion classification and whole-document
  folding use the registry directly.
- Style-generated lists declare membership through `drd-props/group` without
  an EXTERN callback. `node_group_scope` reads native ordered group extensions,
  not a DOCUMENT attribute or a global mutable registry.
- `contract_scope` now owns runtime descriptor storage. Its entries are const
  native records containing scalar facts, field vectors and immutable payloads;
  `rel_hashmap<tree_label, tag_info>` is removed. Owner-local writers replace
  complete entries. Pinned snapshots share unchanged records and cannot observe
  later writes to their source scope or an inherited scope.
- Scope drafts use copy-on-write standard-library maps, not a custom persistent
  container implementation. A declaration or an analysis result batch installs
  its draft as a whole. The first edit with a retained snapshot copies the local
  entry map, not all inherited descriptors or their immutable expressions.
  This is storage-level isolation, not yet definition-generation publication.
- Standard contracts are shared through immutable native snapshots across
  owners, instead of serializing and reparsing `tag_info` records per thread.
  `tag_info` remains only a temporary standard-declaration/cache builder.
  The source role classifier and inference adapter read native records directly.
- Native group extensions are part of the snapshot, with declaration order
  preserved. The disposable style cache now stores entries and groups separately
  in the `contracts-v2` namespace. Cached value decoding uses a draft too.
- Descriptor storage no longer uses packed bitfields or process-global
  environment interning. Stored environment values use `node::frozen_tree`;
  legacy tree queries return detached copies rather than mutable shared payloads.
- Freeze flags and freeze methods are removed. Setters distinguish inferred,
  explicit and native authority; equal-priority explicit declarations can
  override earlier declarations without letting inference overwrite them.
  Declaration locations and finer presentation override rules remain to be
  connected when the scoped descriptor API replaces the positional API.
- EXTERN descriptor queries do not create labels or mutate the registry.
- `native_fields` owns the named physical fields of the v3-specific math nodes,
  enunciations, programs, fractions/roots, mathematical presentation primitives,
  content containers and table structures. `ATHENA/misc/node-fields.json` is embedded at build
  time so cold decoding needs neither a resource lookup nor style execution.
  Standard editing descriptors, native math shape validation, math search
  projection and source identity classification consume that same declaration.
- Metadata declarations for enunciation/program properties and the v3 math
  nodes' required presentation properties reside in that same embedded data.
  The old document-model property tables and their public getters are removed;
  the general property validator and native math validator consume the common
  types/presence rules. Mathematical constraints such as valid delimiter glyphs
  and numbering policy values remain native domain logic.
- Enunciation slot eligibility and script/fraction/root insertion query named
  contracts rather than their own field-name/position lists.
  Presentation does not implicitly tighten persisted content roles: making a
  formula look inline is not grounds to reject an existing structured body.
- Layouts support fixed children, optional trailing children and repeated
  groups before or after fixed children. Resolving a suffix field requires the
  actual node arity; for example, `tformat.body` follows its format entries.
  Repeated groups can contain more than one field. The positional facade is
  generated from the same prototype, rather than another handwritten table.
- Source cardinality and an editing policy's minimum cardinality are distinct:
  an empty CONCAT/DOCUMENT can remain valid source, while editor commands
  preserve at least one editable item. Similarly a root-only TREE can remain
  source without changing the editor's initial root-and-branch shape.

The integrated stage has been built with `-j16` and deployed with the local
executable installer. A focused native probe exercised inherited-scope
isolation, explicit override versus inference, native shape protection and a
detached cross-thread cache round trip with more fields than the former packed
encoding could hold. A headless runtime probe loaded the changed text, math,
folding, graphics, source, poster and documentation editor modules and exercised
the generated group/variant bindings. A subsequent cold probe, with no resource
directory available, exercised XML round trips, required math properties,
singleton rich names versus attribution lists, optional root fields, grouped
repetition and table-format suffix addressing across editing/source-role
consumers. It exposed and removed an unnecessary enunciation-resource lookup
when resolving already declared native nodes. These checks do not establish
completion of the remaining scope and incremental-analysis work below.

## Remaining Integration

- Replace the remaining positional core with named structure/presentation
  contracts and declaration provenance.
- Extend the shared field API to the remaining primitive/macro layouts and
  context-dependent mathematical metadata (such as grid-row/cell overrides).
  Two-sided fixed prefix plus suffix around one repetition group remains to be
  integrated when replacing the positional facade; current layouts support
  a fixed prefix or a fixed suffix, not both at once.
- Finish dependency-directed analysis, scoped definition events and
  generation-matched asynchronous publication. The current owner-local
  worklist is not that publication architecture.
- Migrate the remaining Scheme variant/edit dispatch and field queries, then
  remove the DRD storage adapter and old DRD implementation.
- Exercise genuine representation, scope and concurrency uncertainties with
  focused checks. Do not add tests that merely repeat the declaration tables.

Completion requires the above runtime paths to be replaced, not just new
classes or a passing registry check. No compatibility copies of the old Scheme
group declarations are retained.

### Current Analysis Transition Is Not Completion

The worktree now contains `macro_analysis_core`, replacing the old eleven-pass
`heuristic_init` and first-occurrence `arg_access` implementation. It tracks
reverse dependencies, clears affected inferred facts and uses a worklist.
Pending callee environments do not become inherited empty environments, and
changes to native usage summaries requeue dependants even when the positional
projection has not yet changed. `map-args` follows the executor's identity
forwarding, indexed calls, exclusive upper bound and outer-container context;
variable suffix fields consider the possible enclosing arities.
This is an integrated algorithm transition, not the final analyzer architecture:

- The owner-side `macro_analysis` adapter still compares a complete environment
  and invokes analysis synchronously. It no longer implements inference:
  `macro_analysis_core` receives immutable definition/contract deltas and returns
  result packets without reading or mutating `drd_info`/`tag_info`, interning
  labels or invoking Scheme. Source snapshots are reused until their values
  change. Core working trees are private to the analysis owner, not shared
  editor trees.
- The shared `field_layout` now owns descriptor indexing, minimum cardinality,
  cardinality checks and prototype field counts for both native analysis and
  the positional facade. Input and result facts preserve their authority, so
  projecting a result does not turn an explicit constraint into inferred data.
- Dependency entries currently use names, not the scoped binding/read-set
  model above. Negative resolution slots and evaluation lifecycle integration
  remain to be implemented.
- `edit_interface::update_menus` still calls `drd_update`, which executes the
  environment up to the cursor and calls `analyze_environment`. Renaming the
  analysis entry point has not removed menu work.
- Results now update immutable native `node_contract` entries through one draft,
  but still carry the positional facade's properties. Pending versus absent
  facts, occurrence-specific environments and inferred conflicts need their
  native representation before removing this adapter.

The transition has been built with `-j16` and deployed through the local
executable installer (build ID `97e003f4237c3e4b18531ab2e5dc49c559f784fc`).
A temporary native probe exercised a 64-definition forwarding chain,
redefinition/removal and restoration, branch conflicts, conditional visibility,
pending callee environments, recursive formatting convergence, separate-owner
definitions, source-view independence and bounded/identity argument mapping.
After separating the core, those checks passed again. A detached packet was
also processed on a separate thread after modifying/discarding its original
source; the result retained both the inferred environment and the literal
value's typed metadata, without a DRD owner or resource directory. Snapshot
matching uses the existing frozen comparator, which does not change shared
legacy array reference counts.
A temporary runtime check created a scratch editor through the GUI owner and
ran its commands on the BufferActor: real `generic` style initialization,
section/list layout, `document-bis` and `space-separated` access all succeeded.
The runtime check uses `exec-buffer` and preserves the editor ownership
boundary. No Notes documents were modified and no permanent test suite was
added. These checks do not establish asynchronous or scope-aware completion.

### Immutable Storage Transition

The subsequent storage replacement is built and deployed as
`7a062d0bb4a3f114ac0b304850d8c08062c6b496`. A focused native probe checked pinned
snapshot isolation, uncommitted drafts, ordered group/cache round trips,
detached query payloads, and reading a snapshot on another thread after its
original DRD owner was released. Existing dependency-analysis probe cases also
passed against the new store. The final publisher writes each result directly
into a native draft and shares frozen environments, without constructing live
`tag_info` records or repeatedly invoking legacy setters.

The final binary loaded and laid out a scratch `generic` document with section,
strong/emphasis and list content, then did so again with its generated
`contracts-v2` style cache present. Both actor-owned runtime runs completed
without diagnostics. This is evidence for the storage/cache integration, not
for the still-unimplemented declaration checkpoints or asynchronous publication.
The current declaration decoder still uses the transitional property-priority
rules; per-property provenance, signature validation and source context remain
part of the scoped resolution work.

### Declaration Replay Integration

Native `drd-props` decoding and bridge capture/replay now live in
`contract_declaration`, `env_exec`, `bridge`, `bridge_surround` and
`bridge_docrange`. Nested captures retain immutable child blocks rather than
flattening all descendant events into each ancestor. Cached replay traverses
these blocks in execution order and records the reused block once. Incomplete
capture preserves the previously completed log; this does not yet provide
paired environment/contract rollback.

The decoder rejects contradictory scalar/attribute/environment/layout rules
within one declaration, including overlapping all-field/index selectors.
Selectors are checked against an explicitly supplied layout. Arity is applied
before field rules regardless of source property order. Shipped `hidden*` no
longer contains a contradictory inaccessible override that the old freeze logic
ignored; its hidden behavior is retained. The legacy `border=no`
input retains its inner-border policy. Ordinary declaration application stages
one descriptor, not a copied scope that would force a full-map COW write for
every declaration; combined group updates still use a transactional draft.
Interactive decoding errors render as error nodes, and legacy import translates
them into codec diagnostics.

A temporary native probe, linked against the production library, exercised real
document and surround bridges: initial typesetting, cache-hit replay, cached
end traversal and partial retypesetting after a source replacement. It also
checked declaration order/conflicts, retained child block identity and incomplete
capture. All passed; the probe did not become a permanent regression suite.
The log is `/tmp/athena-contract-events-probe.log`.

The formal binary was built with `-j16`, then loaded and laid out a scratch
generic document on its BufferActor, including section, strong/emphasis and
list content. That check completed without declaration diagnostics
(`/tmp/athena-contract-events-runtime.log` and
`/tmp/athena-contract-events-runtime-result.txt`). The repository installer
deployed build `60d08cd8303622f3c248e046bf2cba96e0c6c85a`, retaining its predecessor.
No user vault was modified.

This remains preparatory work, not the scoped lifecycle described above.
Events currently contain decoded declarations and named WITH scope boundaries.
Definition identities and dependency-checked cache admission are not represented.
Application still uses the transitional priority guards; selectors without an
explicit layout still need binding/signature resolution. Invalid replacement
withdrawal and cross-declaration provenance are not implemented. The checks
above do not establish any of these unfinished properties.

The next completion boundary is a paired evaluator/bridge lifecycle with
binding events, local restoration, invalid-declaration withdrawal and valid
cache replay. Only after that boundary, and explicit contracts for shipped
interactive macros, can menu-driven `drd_update` be removed. Async execution
comes after the same inputs and publication rules work correctly on the owner;
moving the current full-environment adapter to a worker is not that boundary.

### Local Environment Scope Integration

`Typeset/Env/local_environment` now owns WITH binding installation and exit in
the evaluator, both partial evaluator paths, bridge, inline concater and lazy
typesetter. All right-hand sides execute before installation. Saved values are
captured after those evaluations, so an assignment performed by a later RHS
survives the scope's exit. A previously absent variable becomes absent again,
not a present UNINIT placeholder. Full execution and exceptional unwinding
restore temporary names; partial execution that reaches the requested inner
position deliberately retains them. Unrelated body assignments remain intact.

The corresponding contract restore operates only on those names, retaining
their original immutable descriptors and group membership while preserving
unrelated declarations/group additions. Cached blocks carry local target names;
replay restores them against its own incoming scope, not a descriptor snapshot
copied from the original execution. An unchanged scope uses shared-state
identity to avoid descriptor-table copying or resolving target names for normal
formatting WITH expressions. This identity check is not an asynchronous
publication token or a definition identity.

The temporary production-library probe covered evaluator, inline and lazy
typesetting, actual bridge cache replay, partial evaluator/bridge traversal,
exceptional scope exit, missing variables, retained unrelated effects, group
restoration and RHS side effects. All passed; evidence is in
`/tmp/athena-contract-local-scope-probe.log`. PROVIDE's evaluator now checks the
evaluated variable name, matching its inline execution path. Unknown group
declarations become input diagnostics instead of escaping the declaration error
boundary as an unrelated exception.

The production binary was rebuilt with `-j16`; the scratch generic-document
BufferActor check also passed without diagnostics
(`/tmp/athena-contract-local-scope-runtime.log` and its `-result.txt` file).
The repository installer deployed build
`c15bfa7dcf1f5d22bbcdfd03965d1df770a8b5c1`, retaining the previous executable.

This does not yet establish paired preamble/body checkpoints, definition-source
provenance, local macro signature invalidation or dependency-valid result
publication. Non-WITH low-level environment changes and assignments still need
the definition lifecycle. Root evaluation recovery is separate from restoring
the temporary names of a WITH. Menu synchronization remains until these
replacement responsibilities are connected.

### Current Worktree: Evaluated Value Capture

The worktree additionally contains `evaluated_definition`, definition events,
and `track_definition` integration for assignment and local bindings. The
capture reuses frozen expressions through an earlier event position or the
currently tracked matching value. Raw writes invalidate mismatching tracked
values; bulk environment restoration resets tracking. The synchronous analysis
adapter still compares the full environment and may share a matching captured
expression instead of freezing it again.

This is partial evaluated-value storage, not the binding/provenance model in
this design. The records currently lack producer revisions and ordered input
read sets; pointer reuse is not evidence for final cache admission. A missing
record is untracked, not a completed negative lookup. These distinctions must
remain explicit while the old preparation adapter exists.

The next implementation unit is therefore paired checkpoint production and
dependency-valid replay, including empty-effect records and actual declaration
origins. Finish that unit before removing `drd_update` or dispatching inference
asynchronously. Separately, the existing COW map and synchronous frozen capture
remain cost boundaries; neither establishes bounded per-keystroke work.

The design refinement on 2026-10-26 changes this document only. It does not
build, test, deploy, commit, or establish correctness of the worktree's pending
implementation changes. Earlier build evidence above applies to the stages
explicitly named there, not automatically to later edits.

### Temporary Evaluation Boundary

`temporary_environment_scope` now forks an owner-local evaluator's native
contract scope while retaining its original analyzer untouched. The temporary
scope owns its variable/source maps and monitored-write log, selects the same
native context for legacy query consumers, and disconnects the caller's bridge
event capture. Nested scopes restore their immediate caller. Normal and
exceptional exit restore the original environment, definition records, contract
owner, analysis engine, argument stacks and effect-capture entry point together.

`edit_typeset::exec` uses this boundary for temporary expansion against a supplied
environment. `typeset_as_document` uses it separately for each page-reference
stabilization pass. The latter keeps derived references/auxiliary output needed
by subsequent passes, but does not carry definitions and declarations from the
end of one pass into the beginning of the next. Its temporary typesetter is also
released on exceptional exit. These are temporary execution scopes, not WITH:
all their variable/contract effects are discarded, whereas WITH preserves body
writes to names it did not temporarily bind.

When given a replacement variable map, this boundary clears the partial
definition tracker instead of retaining bindings from a different input.
Native syntax lookup reads the temporary variable map, including assignments
made during execution. This does not assert that the replacement map has a
complete source-provenance checkpoint; preparation of those checkpoints remains
unfinished.

The production-library probe passed definition-record replay, unchanged-source
reexecution, source edits, local shadowing and scalar replacement. It also
checked nested/exceptional temporary execution, native query-context restoration,
original analyzer identity, and absence of temporary writes/declarations from
the caller's patch/event log. Evidence is in
`/tmp/athena-contract-temporary-probe.log`; no permanent test was added.

This does not replace preamble/body/cursor checkpoint production or admit bridge
caches through ordered input dependencies. Root progressive-layout recovery and
asynchronous publication remain separate work. The synchronous full-environment
adapter is still present; isolating its temporary owner is not its removal.

The production binary was built with `-j16`. A scratch BufferActor run exercised
generic section/list layout, temporary `texmacs-expand` declarations, real print
layout and restored editing queries. It completed without diagnostics; evidence
is in `/tmp/athena-contract-temporary-runtime.log` and
`/tmp/athena-contract-temporary-runtime-result.txt`. The repository installer
deployed build `31141ced0a6a3f120c4948d44f6abceabd72920b`, retaining its predecessor.
No Notes documents were opened or changed, and no user ATHENA process was stopped.

### Definition Values And Execution Bindings

The worktree now separates `definition_value` (the immutable evaluated payload
and its signature) from `evaluated_definition` (one execution binding). An
assignment or local binding installation creates a new binding even when its
value equals the previous one. A matching earlier event position can reuse the
payload only. Actual cached event replay retains the captured binding, and
WITH exit restores the saved outer binding, including equal-valued shadows.

This removes value equality as a claim of definition identity. It does not
yet supply producer source anchors, input dependencies or cache admission:
the existing bridge still decides when replay is allowed. Those boundaries
must be connected before binding identity is sufficient for result publication.

The captured signature also establishes the default positional layout for a
fixed MACRO when a declaration supplies no explicit layout. Field selectors are
checked against that resolved layout before scope mutation. This is not named
field resolution, deferred attachment or automatic withdrawal of a declaration
after signature replacement. XMACRO's tuple formal is not treated as one child.

The previous design-only refinement separated checkpoint selection from
analysis-service state and defined the next lifecycle cutover. The code changes
in this section are subsequent work and are not covered by its documentation-
only status or the earlier deployed build's verification.

The updated temporary production-library probe distinguishes actual cached
bridge replay from reexecution by checking event-block identity as well as
binding identity. Its assignment fixture now resets `old_patch` at each pass,
as `typesetter_rep::typeset` does; previously equal-value binding reuse could
hide reexecution in that fixture. The corrected check passes cached replay,
invalidated producer execution with shared payloads, same-valued assignment
and local shadowing, outer restoration and pinned snapshot preservation.
Signature-based selector resolution and rejection before scope mutation also
pass. Evidence: `/tmp/athena-contract-binding-probe.log`. No permanent test
was added; these checks do not establish dependency-valid cache admission or
the remaining paired checkpoint lifecycle.

The production binary was built with `-j16`. A scratch BufferActor run passed
generic section/list layout, temporary expansion and print-scope restoration
without diagnostics (`/tmp/athena-contract-binding-runtime.log` and
`/tmp/athena-contract-binding-runtime-result.txt`). The repository installer
deployed build `3adc11d4b785411f382f395972db2d4cb6243fc7` and retained its
predecessor. No Notes document was accessed and no user ATHENA process was
stopped. Full-environment synchronization, paired base/body/cursor checkpoints,
declaration withdrawal and asynchronous publication remain unfinished.

### Root Evaluation Commit Boundary

`evaluation_environment_transaction` now retains the incoming variable map,
native contract scope, syntax-query environment, source/argument stacks, link
context and enclosing event position. Evaluation writes into owner-local map
copies and COW contracts. Success keeps those changes; failure restores the
incoming state together and discards events appended to the enclosing capture.
This differs from `temporary_environment_scope`: successful root effects must
survive, and the existing analysis owner is not replaced on each layout pass.

The root `typesetter_rep::typeset` owns this transaction. If a child or pager
throws, it rolls back before recreating the bridge from the source and clears
partial stack/progressive state. Completed child records from that failed pass
cannot subsequently be mistaken for the successful root's cache. Pager lifetime
is exception-safe. Normal progressive passes still commit their evaluated
prefix; an incomplete root does not become a reusable complete bridge.

This is an exception boundary, not a claim that the supplied starting state is
already a complete source checkpoint. Base/body/cursor preparation and cached
read-set admission remain separate work. Reference/auxiliary derivation retains
its existing behavior; a failed pass is marked incomplete and cannot trigger
the successful-pass reference cleanup. External effects are not rolled back.
The map copying occurs once per root pass, not at every bridge or declaration;
it is still a cost to replace as checkpoint storage is integrated, not evidence
of bounded owner-side preparation.

A temporary production-library probe injects an exception after a real child
bridge has executed assignments and a grouped declaration. It verifies rollback
of values, binding identities, native scope, syntax lookup and link context;
the failed bridge is replaced and a subsequent real root pass commits normally.
A separate enclosing-capture case verifies that rollback also removes appended
events and restores an interrupted argument stack. These checks passed alongside
the existing targeted scope/replay checks in
`/tmp/athena-contract-transaction-probe.log`. They exercise root failure and
retry, not dependency-valid checkpoint selection or whole-application rollback.

The `-j16` production build also passed scratch BufferActor section/list layout,
temporary expansion and print-scope restoration without diagnostics; evidence
is in `/tmp/athena-contract-transaction-runtime.log` and its `-result.txt` file.
The repository installer deployed build
`69fcf67a96b1e112469ed0ae0ce01f9e99ab34c6`, retaining the previous executable.
No Notes document was accessed, no user process was stopped, and no permanent
test suite was added.

### Native Query Dependencies In Bridge Records

Native descriptor queries now record immutable entries in the active capture
for their actual scope. A missing descriptor records a negative dependency;
an as-yet-uninterned name is retained by spelling without interning during the
query. Queries outside capture keep the direct descriptor-read path. The
collector is owner-thread-local and does not follow queries into another scope.

Reads share the existing ordered declaration/binding/block stream. Admission
walks a private native scope, applying preceding effects and restoring WITH
targets before checking subsequent reads. It stops at the first mismatch,
without modifying the live environment or native contracts. A failed
declaration retains a read dependency even though it contributes no effect,
so fixing its target layout can invalidate the cached error rendering.

Repeated reads are coalesced between effects. Read-only child blocks merge
their query sets into the parent instead of duplicating a descendant log at
every level; the child retains its own cache record. Effectful blocks keep
their order and scope boundaries. These reads pin descriptor entries, not full
environment or contract maps. This avoids a full-map snapshot per query.

Ordinary bridge reuse, document-range replay and surround-prefix reuse check
these native dependencies before patching the environment. A mismatch returns
to normal evaluation; merely changing an unrelated descriptor does not force
recomputation. The document-range fallback stays within its own child range.

This is native-descriptor admission, not complete cache admission. Source and
environment validity still use the existing bridge machinery; ordered variable
reads, definition-producer dependencies, group-query dependencies and external
provider revisions are not established by this step. `contract_events_applicable`
must not be used to bypass those other checks or to claim that the incomplete
definition tracker provides negative name resolution.

The temporary production-library probe passed ordered read/write admission,
WITH restoration, negative descriptor lookup, read-only coalescing and recovery
from a previously invalid field selector. A real bridge consumer kept its cache
for unchanged and unrelated contracts, then recomputed after only its queried
descriptor changed, with no environment patch. Existing targeted binding,
scope and root-transaction checks also passed. Evidence is in
`/tmp/athena-contract-queries-probe.log`; no permanent test was added.

The production `-j16` build passed the scratch BufferActor layout, temporary
expansion and print-scope check. The runtime log
`/tmp/athena-contract-queries-runtime.log` was empty, and its result file reported
`actor scratch layout, temporary expansion and print scope restoration: OK`.
The local executable installer deployed build
`75267b9fe37755f5d896a803b7f5f381994b43d0`, retaining its predecessor. This did
not access Notes or stop a user process. It does not establish the remaining
paired checkpoint, variable-read or asynchronous publication contracts.

### Paired Preamble And Cursor State

`evaluation_environment_checkpoint` now captures owner-local variable/source
maps, the native contract/binding scope, argument context and current file
context together. Restore also points syntax lookup at the restored variables.
It does not copy the analysis engine or make legacy trees transferable. Callers
refresh derived font/page state after applying their own overlays.

The editor's preamble and cursor caches now hold these pairs instead of bare
environment maps. Each root preparation restores the preamble pair. Resetting
an init value invalidates the base instead of modifying only one cached
variable; rebuilding the base clears old cursor entries. Compound counter
evaluation restores the same base into its separate evaluator. Cursor cache
entries retain whether their analysis has been prepared, so revisiting one
selects its saved facts instead of running another full analysis scan.

The synchronous analysis adapter retains results independently from their
projection into a live scope. Before preparing a different scope it reconciles
changed descriptor inputs, distinguishing authored facts from inferred output.
After synchronizing definition inputs, it can reproject an unchanged result
removed by checkpoint restoration. It withdraws retained results for removed
definitions. Shared expression values do not replace the restored definition
binding identity.

A temporary production-library probe verified recovery from an unanalyzed
snapshot without rebuilding the analyzer, switching between different explicit
local rules and inferred child environments, and restoring variables, syntax
lookup and binding identity together. A real root bridge then executed a
contract-only producer, removed it and prepared again from the base; its rule
did not survive. The checks passed in
`/tmp/athena-contract-checkpoints-probe.log` after the `-j16` library build.

This is not the full lifecycle cutover. Cursor snapshots are still constructed
by `restricted_exec` and `get_env_child`, not captured occurrences; the analyzed
flag uses the existing editor cache invalidation, not a new dependency receipt.
Menu `drd_update` still performs first-time preparation. The adapter still
compares environment values and has one name-keyed input graph, not separate
binding/context keys. Snapshot selection compares native descriptor maps;
it is not bounded changed-entry publication. Baseline producer provenance,
ordered variable reads, declaration attachment/withdrawal within a sequence,
and checkpoint-aware temporary export preparation remain to be integrated.

The linked production binary also passed a scratch BufferActor case that adds
then removes a contract-only source declaration, with standard editing contracts
remaining accessible. Temporary expansion and print-scope checks passed in the
same run. `/tmp/athena-contract-checkpoints-runtime.log` was empty and the result
file reported `actor paired base restoration and contract-only producer removal: OK`.
The repository installer deployed build
`6094cf9e9f2d101afde2414a94cb8881a4603f12`, retaining its predecessor. No Notes
document or user process was touched; no permanent tests were added.

### Ordered Variable Inputs And Combined Replay

The event stream now records variable presence/value queries and writes alongside
native descriptor queries, declarations and definition bindings. Presence-only
queries distinguish absence from a present UNINIT value without requiring its
old value to remain equal. Writes are captured before the evaluator's equality
shortcut, so an assignment that originally changed nothing still remains an
effect when replayed against a different input.

`resolve_evaluation_effects` walks these events against a private native scope
and a variable overlay. A read following a local write sees that write, not the
original input. Scoped exit restores only the temporary targets to the actual
incoming state; writes to unrelated names survive. Admission does not mutate
the live evaluator or execute source expressions. Bulk environment restoration
inside capture records a non-replayable barrier because its complete effects
are not yet represented by this log.

Ordinary bridge replay, document-range replay and surround-prefix replay now
use `edit_env_rep::can_replay_events`. On a hit, `replay_contract_events` applies
the combined outgoing variable/contract result instead of first applying the
legacy final environment patch. Parent capture receives the completed block
once, with observer capture suspended while applying its effects. The legacy
`changes` map remains in the existing invalidation machinery; it is no longer
an independently applied semantic output on those cache-hit paths.

This is still a transition. Admission and application currently resolve the
events separately; the prepared-occurrence handoff above requires returning and
consuming one admitted result. Direct reads of derived state, group queries,
revisioned external inputs, complete source/binding origins and occurrence
boundaries are not established here. WITH preparation also retains conservative
outer-value read dependencies. No claim of complete read coverage or bounded
owner-side work follows from recording common environment accessors.

The temporary production-library probe passed no-write PROVIDE, presence-only
reuse after a value change, negative lookup versus present UNINIT, originally
no-op assignment followed by a dependent read, changed incoming value rejection,
and local-scope replay restoring the new outer value while preserving unrelated
writes. It also compared the full environment after real cold and cached bridge
execution, alongside the existing scope/rollback checks. Evidence:
`/tmp/athena-contract-inputs-probe.log`. No permanent test was added.

The production library and executable were built with `-j16`. The scratch
BufferActor layout, temporary expansion/print scope and contract-only producer
removal checks passed; `/tmp/athena-contract-inputs-runtime.log` was empty and
`/tmp/athena-contract-inputs-runtime-result.txt` reported
`actor paired base restoration and contract-only producer removal: OK`.
The repository installer deployed build
`7aac3e2631441ef2fad3146eb0b0225f0e5b5e02`, retaining its predecessor. No Notes
document was accessed and no user process was stopped.

The accompanying design refinement makes source-bound occurrence preparation
the next concrete unit, including folded source, WITH argument evaluation and
command-state publication order. These remain target behavior, not outcomes of
the replay probe. `restricted_exec`, menu `drd_update`, synchronous environment
analysis and the DRD facade have not yet been removed.

### Execution Boundaries And WITH Entry Prefixes

`evaluation_boundary_handle` identifies one point in an actual captured
execution. It is not an event-vector index, a source path or a persistent UUID.
A bridge owns the association between this token and its source body; cache
replay retains it and real reexecution creates a new token. Source validity
still belongs to the bridge's existing notification/completion machinery.
Selection is relative to that owning bridge record. Reusing one child record
at multiple display positions still requires a separate expansion-occurrence
identity; a boundary token alone is not a global hit-testing address.

`contract_event_prefix` selects the record up to that exact token. It retains
completed preceding children, including their local-scope exits, and removes
the exits only from the ancestors that remain open at the selected point.
The selected prefix does not contain the later body or suffix. It shares child
logs rather than copying their descendants. Missing or unrelated tokens do not
select an approximate position. Read-only blocks containing boundaries are
retained instead of being flattened into their parent's dependency set.

WITH bridges mark their body entry after evaluating/installing the bindings
and before executing the body. Their cached partial-entry path replays the
admitted prefix and then continues into the child; it no longer evaluates the
binding right-hand sides again when that prefix matches. The bridge retains
one derived prefix for its current event block. This does not take an entire
environment snapshot per node. A miss still uses normal source evaluation.

`replay_contract_events` now validates and applies the same private result in
one owner operation, returning false without changing variables, contracts or
the enclosing capture if admission fails. Ordinary bridge, document-range and
surround replay consume this result directly. Surround link cleanup retains a
separate read-only admission check before its typesetting capture; that is not
a second application of effects.

The temporary production-library probe passed nested open/closed scope prefix
selection, unrelated-token rejection, rollback of boundary bookkeeping, and
admission failure after a preceding private write. A real WITH bridge selected
its body repeatedly with the original RHS and inner binding identities; a
source edit produced and selected new identities. Full cached replay still
closed the scope and executed its retained body effects. Existing targeted
scope/cache/rollback checks passed in the same probe. Evidence:
`/tmp/athena-contract-boundaries-probe.log`. No permanent tests were added.

This is an execution-record foundation, not the editor occurrence cutover.
The current editor still reconstructs cursor context through `restricted_exec`
and does not call the typesetter's cached partial-entry API. Inline/argument
occurrence mappings, active macro/source frames, folded-source ownership and
the prepared command-publication path are still required. Do not describe this
change as eliminating cursor-time source execution or removing `drd_update`.

The production library and executable were built with `-j16`. Scratch BufferActor
layout, temporary expansion/print scope and declaration removal passed with an
empty `/tmp/athena-contract-boundaries-runtime.log`; its `-result.txt` file
reported `actor paired base restoration and contract-only producer removal: OK`.
The local executable installer deployed
`fee758d0a3f4bb864bde4632f8bf2d73b4b733bd` and retained the predecessor. No Notes
document was accessed and no user ATHENA process was stopped.

### Actor-Owned Source Occurrences

Bridge caches now retain a separate `source_occurrence_log` beside the detached
native effect log. The concater records accessible atomic source nodes at their
actual evaluation boundaries. Each occurrence retains its source value and
owner-local argument/source frames, current file and security context; it does
not copy a full environment for each character or publish legacy tree handles
to workers. Source strings have one record per evaluated occurrence, not per
character. A repeated expansion remains multiple occurrences.

The sidecar index shares child blocks. Keys are relative to each bridge's live
source anchor, so inserting a preceding paragraph relocates a cached child
without rewriting all descendant keys or retypesetting it solely for this
index. Child ranges have prefix maxima for lookup when expansion ranges overlap.
Already ordered entries do not require sorting. Source outside an enclosing
record's anchor is not indexed as if it belonged there. The index is a local
source lookup, not complete cross-document source ownership or hit testing.

Bridge hits retain the source block; surround-prefix hits retain their own
source block too. Root rollback truncates both the native events and occurrence
sidecars. Temporary expansion/print scopes disconnect both capture sinks. A
source boundary is selected only when its associated source value still matches
and its owning completed bridge record is available. Multiple displayed copies
of one source address return no unique occurrence, rather than choosing the
first expansion's environment.

`prepare_cursor_environment` runs on the owner after layout, on cursor/focus
changes, before menu publication and after explicit `typeset_forced`. For a
unique atomic source occurrence in a non-folded completed layout, it prepares
from the matching base and recorded prefix, restores the captured call context,
and selects the resulting checkpoint. No source expression is executed by this
successful preparation path. Moving within the same atomic source can reuse
the prepared checkpoint; cursor caches retain their native trace identity so a
pre-layout heuristic entry cannot conceal a newer completed occurrence.

This is the first editor consumer cutover, not removal of the old subsystem.
Folded/incomplete layouts, source editing, compound boundaries, metadata field
addresses and ambiguous displays still need explicit preparation/selection.
`restricted_exec` remains their fallback, and `drd_update` still performs
synchronous optional analysis. Preparation can still traverse a prefix from the
base and copy variable maps for a newly selected context; sparse intermediate
checkpoints and bounded preparation are not supplied by this index. Do not call
every getter pure or claim complete incremental cost on this basis.

The temporary production-library probe passed actual macro argument capture,
restoring outer argument/file context, retained cache identity, relocation of
an untouched cached paragraph after insertion, ambiguous repeated display, and
paired rollback/temporary-scope isolation. The existing targeted scope/cache
checks passed too. Evidence: `/tmp/athena-source-occurrences-probe.log`. These
checks did not open Notes and did not add permanent tests.

The production build used `-j16`. A scratch BufferActor placed a declaration
after twelve preceding source nodes, moved the cursor into a `strong` argument,
then within that text and out of the macro. Its environment queries observed
the late assignment, the inner bold scope and the restored outer scope. This
distinguishes the integrated path from the old ten-child heuristic. The earlier
temporary/print and producer-removal checks also passed in that run.
`/tmp/athena-source-occurrences-runtime.log` was empty and the result file
reported `actor source occurrence preparation beyond ten nodes and macro scope: OK`.
The installer deployed build `dd66034514dedcd75db71b06f6a001ba2e3c7acf`, retaining
the predecessor. No Notes document or user ATHENA process was touched.

### Paired Package Checkpoints And Legacy Cache Removal

The package entry points now retain `style_environment`, an owner-thread
immutable pair of evaluated variables and a native contract snapshot. Restoring
it selects those evaluated bindings instead of patching values into an unrelated
descriptor scope. It deliberately leaves the host's source/argument/file context
alone. Returned DRD projections are independent mutable drafts over the retained
snapshot and copy the variable map; they do not share a live analyzer.

Editor preamble loading, menu-symbol typesetting, online document preambles and
offline document preambles use this pair. The package cache key includes source
directory and Scheme-module execution policy. Online and offline evaluations
therefore cannot accidentally share module side effects. Offline preamble cache
hits likewise return a fresh projection rather than the cached mutable object.
Package invalidation drops cache ownership, not references held by older views.

Removed the old style disk cache reader/writer, parallel value/DRD cache maps,
and `drd_info_rep::get_locals/set_locals` serialization entry points. Existing
cache files are ignored; no profile cleanup or compatibility reader is added.
The normal package path still performs synchronous `analyze_environment` before
capturing the pair. This work preserves existing evaluated identities; it does
not supply missing default/direct-write bindings, complete authored-contribution
provenance or per-input package admission. The environment-scanning analyzer and
the rest of the positional facade still need replacement.

A temporary production-library probe used the real `generic` package to verify
cold/warm binding identity, value/contract pairing, independent document changes,
restoration without changing host source context, offline preamble binding reuse,
module-policy/directory cache separation, and invalidation with a retained old
snapshot. It passed with exit 0:
`/tmp/athena-style-checkpoint-probe.log`. No permanent test was added.

The `-j16` production build and a scratch BufferActor run also passed. The latter
loaded `generic`, laid out sections, strong/emphasized text and lists, and checked
their accessible arguments and package-defined list group. Its runtime log was
empty; `/tmp/athena-style-checkpoint-runtime-result.txt` reported
`generic style, section and list layout: OK`. The local installer deployed build
`10174035e7544081affa02e114cfc708489f3d59`, retaining its predecessor. Neither Notes
nor a user ATHENA process was touched.

### Definition Inputs At Value Boundaries

Default `MACRO`/`XMACRO` values now receive detached definition payloads during
default-environment initialization. New evaluators and explicit default resets
install that baseline instead of erasing their callable index. Reset copies the
small binding map but shares its immutable bindings/payloads; it does not scan
the entire default value map again.

Direct native writes, monitored writes and patches now synchronize a matching
definition binding. Ready-value synchronization retains an already matching
binding. Source `ASSIGN` and `WITH` still explicitly create a new execution
binding before writing its value, even for equal values. This preserves the
distinction between assignment identity and immutable expression storage.

Explicit value-only whole-environment replacement builds its callable baseline
once at that input boundary and does not inherit callables absent from the new
values. Temporary replacement follows the same rule and restores its caller's
paired state on exit. Unchanged macro payloads can be reused, but replacement
bindings are new. Ordinary scalar keys are neither interned as node tags nor
copied into the macro table merely to bootstrap the environment. Default/full
replacement also updates the syntax environment reference alongside the values.

Local-scope restoration remains distinct from writing a definition: restore its
values, then select its original binding identities. Removing a present `UNINIT`
value now forgets its tracked binding even though the removed value equals the
map's default sentinel. Captured direct-write effects carry their bindings and
replay selects those identities rather than publishing newly generated ones.

The temporary production probe passed default-callable coverage, direct native
write versus equal source-assignment identity, default reset, whole/temporary
replacement, syntax pairing, non-interned scalar input, present-UNINIT removal
and recorded direct-write replay. Package cold/warm and offline isolation also
passed: `/tmp/athena-definition-baseline-probe.log`. No permanent test was added.

This closes missing callable-input paths, not the complete resolver lifecycle.
An absent macro-table entry still cannot distinguish a non-callable value from
an absent variable. That distinction lives in the paired value/presence layer.
Detached resolution receipts, source producer origins, authored-contribution
withdrawal and binding/context-keyed analysis still need integration. The
name-keyed analyzer continues to scan the environment; it has not been moved
onto a worker or declared incremental by this change.

Production was built with `-j16`. The final binary passed the scratch BufferActor
generic-style/section/list check with an empty runtime log and
`generic style, section and list layout: OK` in
`/tmp/athena-definition-baseline-runtime-result.txt`. The installer deployed
`ec46f0c55b657c910a9bb6bf581b7825cb88eae1`, retaining the previous executable.
No Notes source or user ATHENA process was touched.

### Tracked Value Candidates Instead Of Environment Discovery

`evaluation_inputs` records coalesced name/presence changes at the evaluator's
write boundary. It stores no variable values or legacy trees. Defaults capture
their initial names once; explicit whole-value replacement establishes a new
baseline at that boundary. Local restoration records actual changes, including
removal of a present `UNINIT`. Equal source assignments still have execution
identity/effects even when they need no value-analysis update.

Environment checkpoints, package checkpoints, temporary scopes and root
transactions retain the matching input state. Selecting an old checkpoint is a
branch switch: the journal collects changed names from both branches, not a
fabricated sequence of source edits. Replay writes enter through the same value
boundary. The legacy import compiler records names while consuming assignments
and supplies its snapshot explicitly.

`macro_analysis::prepare` now reads those candidates rather than iterating every
environment value and scanning all retained source names for removals. Declaration
changes and restored descriptor projections remain a separate input. Successful
publication acknowledges the selected input snapshot. On an exception, the
adapter discards its partially updated preparation/engine state so the next
attempt starts from a complete input rather than an already-consumed partial
delta. This does not execute source or swallow the original exception.

The journal seals pending changes at checkpoint/preparation boundaries. Its
bounded batch chain shares immutable history; compaction retains final native
name membership. Comparing different baselines visits their native names and
rechecks candidate values. This avoids ordinary full-value scans, but baseline
replacement and compaction are not constant-time. Retained older checkpoints
can also retain older chains; a depth cap is not a global memory budget.

This remains a synchronous, name-keyed adapter, not the planned detached
binding/context analysis service. Complete slot receipts, producer-bound authored
contributions, occurrence preparation and removal of menu-triggered inference
remain outstanding. Inherited inferred projections also need a distinct lifetime
from authored declarations when a fresh analyzer is seeded from another scope;
the value-candidate journal is not that separation.

Verification was limited to the uncertain state boundaries. A temporary native
journal probe passed branching, repeated-write coalescing, removal, restoration
and compaction across retained checkpoints. A production-library probe passed
inferred signature/scalar changes, earlier/later checkpoint selection, absence
versus present `UNINIT`, local exit retaining unrelated assignments, root rollback,
temporary input isolation, whole replacement, declaration-only updates and
withdrawal, and unchanged-input descriptor reuse. Its real-package checks also
passed cold/warm pairing, independent owners, offline preambles and invalidation:
`/tmp/athena-analysis-delta-probe.log`. These are temporary probes, not additions
to the permanent suite, and do not establish the remaining lifecycle goals.

The `-j16` production build completed. A scratch BufferActor loaded `generic`,
laid out a section and list, and checked their field accessibility and package
group. `/tmp/athena-evaluation-inputs-runtime.log` was empty and its result file
reported `generic style, section and list layout: OK`. The local installer
deployed build `d7d92e1f7b95d3e645f74687a3468ba2f622ccf1`, retaining its predecessor.
No Notes source or user ATHENA process was touched.

### Authored Descriptors And Inference Projections

The mixed descriptor table allowed a new analyzer to inherit a result from a
different value environment. A temporary production-library probe demonstrated
the failure before this change: after removing a unary macro from a replacement
environment, a fresh analyzer still reported arity one. The log is
`/tmp/athena-inference-isolation-before.log` (exit 1).

`contract_scope` and `contract_snapshot` now retain separate native-default /
authored entries and materialized inference projections. Ordinary descriptor
queries select the effective projection or declaration; they do not merge fields
or invoke analysis. `declared` selects only source rules. `replace` and `edit`
update that source layer, while `publish_inference` alone stores computed output.
A changed declaration retires its old projection; reapplying an unchanged rule
does not discard a still-selected result.

Declaration decoding starts from the authored layer, and the analysis adapter
detaches that same layer as input. It no longer needs to ignore every
inferred-authority value during declaration comparison: those values can now be
native defaults or signature scaffolding, but never a previous body-analysis
output. Computed parameter names and analysis diagnostics do not become authored
attributes when another property is edited. Explicit field labels also take
precedence over the analyzer's generated formal names.

On first synchronization, a new analyzer rebuilds the inference layer in its
publication draft. Inherited projections from another environment do not survive
merely because the missing name was absent from the new input baseline. Native
and explicit descriptors remain available. Publication still commits one draft;
a failed analysis does not clear the live owner's selected results.

Both layers travel with snapshots. Local target restoration selects the previous
authored and inferred records independently, including masking inherited
projections when necessary, without undoing unrelated inner changes. Package
snapshots retain their evaluated projections until a new analysis is requested;
restoring a snapshot itself does not manufacture a new analysis.

This is not yet producer-indexed authored contribution storage. Source rules are
still merged by target in each scope. Their origins, withdrawal order and binding
attachment require the next lifecycle change; a separate output map must not be
mistaken for that work being finished.

The same temporary production probe now reports absent-macro arity zero and
passes inference/source separation, authored field-label precedence, local
restoration of both original records, and clearing/restoring an inherited
projection. The previous delta and real-package checks also passed:
`/tmp/athena-inference-isolation-after.log` (exit 0). No permanent test was added.

Production was built with `-j16`. The scratch BufferActor generic-style,
section/list layout and field/group checks passed with an empty
`/tmp/athena-inference-isolation-runtime.log` and
`generic style, section and list layout: OK` in its result file. The installer
deployed build `7c44eb605e6c563a45cf5dd1011a013415626539`, retaining the predecessor.
No Notes source or user ATHENA process was touched.

### Declaration History And Withdrawal

Authored declarations now retain ordered per-target chains instead of only their
effective descriptor. Each occurrence keeps its immutable event, execution-time
definition binding, order and any recomposition diagnostic. The chain retains
its native/authored baseline separately from inference. A snapshot retains the
same immutable chain; subsequent writes detach the shared source index before
changing it.

Actual `exec_drd_props` execution creates a new event even for equal source text.
It captures the current file and accessible source path for diagnostics. Admitted
event replay retains that event identity. Applying an already present event is
a no-op, rather than removing/reappending it and changing precedence. Equal but
distinct actual executions remain independent contributors.

Declaration execution also records its definition-binding read. Cache admission
compares that identity at the point of the read, respecting earlier recorded
assignments. An equal-valued new assignment is not the captured binding. Reject
the stale record before changing the live environment, rather than reinterpreting
its field selectors under that assignment. This is a declaration dependency, not
yet complete scoped name-resolution or group-read receipts.

Withdrawing one occurrence recomposes that target from its baseline and remaining
authored sources. Its saved bindings interpret the historical selectors; a later
binding in the live environment does not silently retarget them. If withdrawal
invalidates a later declaration, that whole declaration becomes inactive and
retains a diagnostic. Its fields and group contributions disappear together.
A new invalid declaration still throws before mutating the scope; the final
producer replacement/diagnostic transaction remains to implement.

Ordered group events retain duplicate sources. Removing one source preserves
membership supplied by another and exposes the remaining source's order. Local
target restoration selects its previous history and group effects without
undoing unrelated target declarations. Restoring a checkpoint selects its full
paired history. Native `replace`/`edit` are full-descriptor replacement barriers
and discard that target's declaration history; normal decoded declarations no
longer go through them. Remaining facade writers must be removed, not treated as
a second supported source-authoring API.

The source index is copy-on-write, not copied for every setter while uniquely
owned. First mutation after sharing can still copy the map. Target recomposition
walks its chain, and changing group sources rebuilds the dynamic group projection.
These are explicit transitional costs, not a claim of bounded cursor work.

`/tmp/athena-declaration-history-probe.log` records a temporary probe linked with
the production library. It passed distinct equal executions, replay idempotence,
precedence after withdrawal, duplicate group sources/order, target-local restore,
retained snapshot isolation, invalid-declaration atomicity and saved-signature
recomposition. Real evaluator capture retained source provenance and event
identity, and rejected replay after an equal-valued new binding.

The same probe used the real `generic` package and a 32-paragraph typesetter.
Cold layout and cached replay retained exactly two declaration occurrences.
Deleting one declaration-only bridge exposed the earlier name and removed its
group membership; deleting the last removed its descriptor and history. This
uses the normal incoming-checkpoint restoration/replay path, not a call to the
direct withdrawal helper. The probe exited zero with all three check groups
reported OK. No permanent test was added.

This closes the initial history-storage boundary, not full producer attachment:
chains are still keyed by target name, local-rule applicability across rebinding
is not implemented, and pending declarations, package/local attachment policies
and producer-indexed invalidation remain outstanding. Source paths are diagnostic
locations, not source revision identities. The synchronous name-keyed analyzer,
incomplete occurrence preparation and positional DRD facade also remain.

The next concrete attachment boundary is `edit_env_rep::exec_use_package`: it
loads and filters a package body without establishing a separate package-origin
frame. `cur_file_name` therefore only supplies the current evaluator file, not
reliable package inclusion provenance. Add an explicit inclusion execution frame
there; do not change file-relative evaluation behavior merely to improve a
diagnostic string. Local entry/exit is centralized in `local_environment_scope`,
whose current target restoration follows the names bound by WITH. Complete
declaration scope ownership must integrate with that same owner and its replay
record rather than infer scope from source paths or add an independent stack.

Production was built with `-j16`. The final binary passed the scratch BufferActor
generic-style, section/list layout and field/group query check:
`/tmp/athena-declaration-history-runtime.log` was empty and
`/tmp/athena-declaration-history-runtime-result.txt` reported
`generic style, section and list layout: OK`. The local installer deployed build
`4392a6f0b940e1c3cfd19adfc2da4ba9d577db25`, retaining its predecessor by build ID.
No Notes source or user ATHENA process was touched.

### Explicit Package Inclusion Provenance

`package_inclusion` is an immutable record of one actual package inclusion: its
resolved source and parent inclusion. `exec_use_package` establishes this frame
around the existing package evaluator, after loading the package body. Nested
inclusions link to their caller; repeated real loads create distinct identities.
The frame is separate from `cur_file_name` and `base_file_name`, so provenance
does not change document-relative URL behavior.

Evaluated definition bindings and authored declaration events retain the same
inclusion handle. Their frozen value/rule payloads can still be shared. Event
replay selects the captured handle rather than fabricating another package
execution or changing the caller's ambient frame. Declaration diagnostics use
the executing package source when present, otherwise the current evaluator file.
This identifies execution origin; it is not yet the lexical source map for an
arbitrary helper macro that generates declarations.

Owner checkpoints, root rollback, temporary evaluation and prepared source
occurrences now retain the ambient package frame alongside their other context.
`package_execution_scope` restores its predecessor on normal and exceptional
exit. Frames are allocated for actual package inclusions, not every WITH, glyph
or descriptor query. No live editor, legacy tree or mutable evaluator is stored
in the inclusion record.

The temporary production-library probe used two native `.ats` fixtures in `/tmp`
with nested inclusion, macro assignments and declarations. It verified matching
definition/declaration origins, distinct repeated inclusion identities, cached
identity retention, unchanged host URLs and restoration through exceptions,
checkpoints, temporary evaluation, root rollback and prepared occurrences.
The real `generic` bridge cold/cache/declaration-deletion check also passed.
`/tmp/athena-package-origin-probe.log` reports both groups OK and exited zero.
No permanent test was added.

This supplies the package-origin input for attachment resolution; it does not
yet enforce package-default versus bound-local applicability. Native baseline
and explicit value-only replacement bindings can have no package origin, which
must not be mistaken for complete historical document provenance. Producer
revision receipts, scoped pending rules and owner-incarnation handling still
remain to implement.

Static inspection also found that existing packages use `parameter` and
`macro-parameter` declarations for environment metadata such as lengths, colors
and separators. These must resolve to scoped variable slots, not be rejected or
made pending because they lack callable macro arguments. The design now makes
that target role explicit before introducing binding-local macro applicability.

Another remaining path is `with_package_definitions` in `new_buffer.cpp`, used
by `WITH_PACKAGE` rewrites. It only copies top-level ASSIGN nodes into a WITH;
it does not execute declarations or nested inclusions. This path has not acquired
the semantics of `exec_use_package` from the provenance change. Replace that
syntactic extraction with evaluated scoped package state as part of the package
cutover, rather than claiming all package entry points are complete.

Production was built with `-j16`. The final scratch BufferActor check passed
generic-style section/list layout and field/group queries, with an empty
`/tmp/athena-package-origin-runtime.log` and
`generic style, section and list layout: OK` in
`/tmp/athena-package-origin-runtime-result.txt`. The local installer deployed
`9ed3f890a0cc6b39222b90f43f1ac737b6295bd6` and retained its predecessor.
No Notes source or user ATHENA process was touched.

### Binding Applicability And Scoped Pending Rules

The working implementation now distinguishes macro-field, native-node and
environment-parameter declarations. A local macro-field rule attaches to its
selected callable binding, or remains pending until a callable is assigned in
its owning name scope. An equal-valued new binding does not inherit the rule.
Package defaults are evaluated against the current applicable signature before
local rules; a binding-local rule wins even if a package is loaded later.

Recomposition records pending, dormant, active or invalid applicability on each
occurrence. Invalid declarations contribute neither fields nor groups. A package
default invalidated by a changed signature can become valid again under a later
binding. Binding changes invalidate inferred projections, including inherited
ones, without promoting those projections into authored declarations.

WITH entry masks outer local macro-field rules for its bound names before
installing new bindings. Exit restores the prior target histories. Unrelated
names retain their normal evaluator behavior. Captured prefixes retain entry
targets and explicitly omit scope exit; complete replay performs both. This
prevents an outer pending rule from being consumed by an inner shadow while
preserving its availability after exit. The legacy import contract collector
also records callable assignments for its explicit declaration projection.

The production library built successfully with `-j16`. The temporary linked
probe `/tmp/athena-declaration-attachment-probe` exited zero. Its log reports:

- `native/parameter/macro roles, pending attachment, rebinding and defaults: OK`
- `scoped pending rules, complete replay and open boundary prefixes: OK`
- `package cold/warm pairing, owner isolation, offline preamble and invalidation: OK`

This probe completes verification left running before the design refinement.
No permanent test was added. The application binary was not rebuilt or deployed
for this attachment change; the last recorded deployment remains `9ed3f89`.
The shipped `title-base.ats` declaration for `doc-title-options` also changes
invalid `accessible=no` to the supported `accessible=none` at its source.

This is not full A2 completion. Arbitrary producer-revision replacement,
malformed replacement diagnostics, complete slot/read receipts and publication
through the normal incremental source-edit path remain to implement. In
particular, applying a newly invalid declaration still throws before scope
mutation; the intended diagnostic publication transaction is described in the
design, not implemented by these applicability states. Native field authority,
prepared consumers, detached inference and facade removal retain their separate
completion obligations.

### Source Declaration Outcomes And Replacement

Actual declaration execution now records its frozen source, file/path, package
origin, optional decoded declaration and applicability/diagnostic outcome. The
enclosing bridge execution owns this immutable record. Admitted replay retains
its identity; changed source creates a new one. A malformed declaration whose
target cannot be decoded still has a source execution record, without inventing
a descriptor target or keeping the old declaration as a fallback.

The evaluator retains decoded invalid declarations in their target history.
Whole-declaration materialization excludes all their fields and groups while
preserving lower-priority contributions. Its ordinary `_ERROR` result still
renders through the existing error path. Strict programmatic application used
by static import remains strict; normal evaluator failures are recorded rather
than discarding their authored occurrence. Unexpected execution exceptions are
not converted into source diagnostics.

Replay checks the declaration outcome after applying captured rules to its
private draft. A preceding declaration can change field-layout validity without
changing a macro binding or any variable, so checking the binding alone cannot
admit cached error boxes. A changed status/diagnostic rejects reuse before live
state changes. Invalid results, as well as valid results, survive unchanged
replay. Syntax failures retain the evaluator reads that produced their target.

Normal bridge invalidation already reconstructs from the incoming checkpoint;
it does not need a separate path-keyed withdrawal table. That path now publishes
the new source record alongside its surviving contract state, including when
the replacement has no decoded target. Deletion removes its record from the
new execution graph. Old snapshots retain their own contributions and outcomes.
This is normal-layout evidence, not proof of every unprepared/folded context.

The temporary production-library probe in
`/tmp/athena-declaration-outcomes-probe.cpp` used the real `generic` package and
32-paragraph bridge layout. It exercised valid-to-invalid replacement, lower
rule recovery, group removal, retained-snapshot isolation, cached failure reuse,
declaration-only layout changes in both directions, targetless syntax failure,
correction and deletion. It exited zero and
`/tmp/athena-declaration-outcomes-probe.log` reports:
`real bridge replacement, invalid outcomes, cached suffix and removal: OK`.
No permanent test was added. Complete group/derived-state receipts, occurrence
preparation, detached inference and positional-facade removal remain open.

Production was built with `-j16`. The final scratch BufferActor check passed
generic-style section/list layout and field/group queries;
`/tmp/athena-declaration-outcomes-runtime-result.txt` reports
`generic style, section and list layout: OK`. Its runtime log contains only
the source-fingerprint cache refresh/restart notices, with no runtime error.
The local installer deployed build `87f07c4cef7f36098e551148f7639d3c18245abf`,
retaining `9ed3f890a0cc6b39222b90f43f1ac737b6295bd6` by build ID. This deployment
also includes the previously library-verified binding-attachment changes.
No Notes source or user ATHENA process was modified or stopped.

### Group Receipts And Syntax Resolution

Scoped group reads now record the result actually consumed: boolean membership,
the subgroup selected by `find`, or the ordered member vector. Raw group
projections remain for declaration materialization and admission, which must
not recursively capture their own checks. Scheme-facing native group/variant
queries use the scoped entry points. Capture checks the `contract_scope` owner,
not the address of shared group storage.

Identical reads coalesce until an intervening effect. Admission evaluates them
in order against its private contract draft, including declaration effects and
WITH entry/exit. An unrelated added member leaves a boolean query reusable;
changed order invalidates a member-list query. Changing the supplying subgroup
invalidates `find` even if parent-group membership remains true. Negative
membership and subgroup lookups are dependencies too.

`get_syntax` previously recorded its descriptor read but silently consulted the
legacy environment on fallback. That fallback now enters the existing variable
read journal, including absence and present UNINIT. Explicit syntax does not
acquire an unused environment dependency. VALUE/OR_VALUE use one native
name-resolution helper without creating tag IDs for scalars. Tree-form syntax
expansion also uses its receiver; it no longer switches to `the_drd` halfway
through a query on another instance.

The production-library probe `/tmp/athena-group-receipts-probe.cpp` exited zero.
It covered group membership/order/provider changes, foreign-scope isolation,
local shadow restoration and syntax presence/value/source ownership. It also
used a real Scheme callback in a `generic` document bridge: unchanged callbacks
were reused, while declaration-only deletion and restoration of membership
caused reevaluation. `/tmp/athena-group-receipts-probe.log` reports all three
groups OK. No permanent test was added.

These checks do not establish complete derived-state admission. Thread-local
access policy, cached frame/font computations, reference/attachment providers
and external callbacks have distinct owners. The design records those remaining
boundaries; the group and syntax receipts do not imply that cursor preparation,
detached analysis or facade removal is finished.

### Reference And Attachment Read Receipts

The evaluator now records which local or global reference/attachment provider
supplied a GET_BINDING, HAS_BINDING or GET_ATTACHMENT read, including absent
keys and the selected value. Ordered contract replay compares those reads with
the current provider and value; the same value supplied by a different scope
does not admit stale execution. SET_BINDING writes to reference state and other
layout outputs, so its execution remains non-reusable until that write owner
participates in replay.

The focused `cachedContractReadsTrackReferenceAndAttachmentProviders` check
passed (3 passed, 0 failed including Qt setup/cleanup), and `athena_body`
compiled successfully. Neither result establishes receipts for derived
font/frame state or external callbacks, prepared compound occurrences,
WITH_PACKAGE evaluation, asynchronous inference or removal of the DRD facade.
No production binary was deployed and no Notes source was changed.

### Source Identity Roles Consume One Resolved Contract

Unknown extension nodes still require an explicit role declaration or a
resolved macro contract. For the latter, source identity classification now
reads arity, transparency, field environment and field names from one
`node_contract` rather than routing each through a separate positional DRD
getter. Core v3 nodes continue to use `native_fields` directly. This does not
change persisted source, source identity placement or editing accessibility.
It removes redundant facade reads for this consumer; other editor consumers
and the remaining DRD facade still need their own ownership cutover.

### Evaluated WITH_PACKAGE Scope

`WITH_PACKAGE` now executes its package through `exec_use_package`, including
nested inclusions and native declarations, rather than rewriting the body with
only top-level ASSIGN expressions. One owner-local scope captures executed
variable and contract effects. Its write set excludes names local to an inner
WITH, and leaving the scope restores only package-owned names and contracts;
unrelated body writes remain effective. Ordinary evaluation, incremental
Bridge, Concater and Lazy presentation use this same scoped execution.

The Scheme macro editor still has `tree-load-style` for inspecting source;
that raw syntax cache is not a source of evaluated package contracts.
The separate obsolete `Style/Evaluate` engine cannot publish native contract
scope changes and therefore no longer offers a syntactic WITH_PACKAGE rewrite.
An existing evaluation transaction rolls back incomplete package preparation.
The focused nested-package, declaration and restoration test passed with no
failures, and the production body library built successfully. No broad tests
were run, no Notes files were changed, and no application was deployed.

The remaining work is to finish compound/folded occurrence selection and remove
query-time `drd_update`/`restricted_exec` ownership before detached inference
replaces the synchronous adapter. The tested scope behavior does not establish
those later cutovers.

### Package-Source Receipts In Cached Execution

Each actual `exec_use_package` now records its requested name, resolved file,
file modification time/size and style-cache generation in the ordered contract
execution log. Nested `use-package` executions produce their own receipts.
Bridge replay uses the same resolver to compare these inputs before reusing
evaluated effects, so changes to a nested package trigger ordinary source
evaluation instead of replaying its previous declarations and variable values.
No watcher or duplicate package interpreter was introduced. Direct on-disk
changes are observed at the next cache admission; ordinary style-cache clearing
still explicitly requests view initialization.

`athena_body` built successfully. The existing focused WITH_PACKAGE test now
also verifies a warm receipt and rejection after rewriting its nested child
package; that test passed. Package source admission is therefore covered, but
compound/folded occurrence preparation, derived font/frame inputs, and the
remaining query-time DRD calls are separate migration work. No Notes content
was changed and no installed executable was replaced.
