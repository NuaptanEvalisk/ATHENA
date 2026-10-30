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
update that source layer, while `replace_inference` publishes the worker's
immutable computed-output projection.
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

### Compound Occurrences And Recorded Contexts

Bridge, Lazy and Concater now retain source entry/exit boundaries for compound
nodes as well as atomic text. A nested handoff of the same source through these
layers is one occurrence; separate displays of an argument remain separate.
Lookup filters by the actual source value and can retain an explicitly selected
boundary. Without a selected boundary, multiple displays remain ambiguous.
Atomic sources share their entry/exit token because they have no evaluated
effects. Compound borders select the corresponding entry or exit state.

A successful typesetter pass now publishes its incoming environment checkpoint
alongside its execution record. Cursor context preparation reconstructs from
that exact pair rather than re-admitting a prefix against the current mutable
environment. The cache key includes the incoming checkpoint, not only the trace:
reusing a trace under different incoming values must not reuse an old cursor
context. Context reconstruction does not evaluate source, inspect package files,
or reexecute reference writes. A layout-output barrier still refuses cache
admission, but permits reconstructing recorded variable/contract state. An
unrecorded whole-environment replacement still refuses reconstruction.

Heading folding retains the original source beneath its transient placeholder.
The document bridge executes folded children through their hidden bridge while
omitting their boxes, including documents containing only hidden children.
Previously the document bridge skipped those children before the hidden bridge
could run. Hidden work now participates in its existing progressive budget.
Visible positions in a folded screen tree can use their own captured contexts.

The temporary production-linked probe
`/tmp/athena-compound-occurrences-probe.cpp` passed after the parent document
dispatch was corrected. Its final log is
`/tmp/athena-native-occurrences-final-probe.log`: compound/empty fields and local
entry/exit, actual duplicate displays, cached reuse, recorded contexts behind
reference writes, opaque-reset rejection, and folded definitions/declarations
in cold/warm layout all passed. No permanent regression suite was added.

This is not the final prepared-query cutover. Physical repeated-display
selection must carry the hit box's occurrence (including Unicode source spans),
not guess from a source path. Source/metadata preparation, progressive pending
positions and preparation cost still need completion before removing
`restricted_exec` and the three `drd_update` callers. Macro inference remains
synchronous. No application binary has been deployed for this slice.

### Native Bootstrap Without Legacy Descriptor Storage

`tag_info.cpp` and `tag_info.hpp` are removed, including parent/child descriptor
storage, tuple codecs and both conversion directions. Their only remaining
production consumer was standard-table initialization. That table now builds
`node_contract` directly through a transient local builder; it does not retain a
second mutable representation. Native JSON field prototypes populate the same
contract without a legacy intermediate. Existing defaults and policies are
unchanged. One existing source-identity test fixture was translated to native
contract data; no new assertions or tests were added for that mechanical change.

The production body library built with `-j16`, including CMake's removal of the
deleted translation unit. The final occurrence probe above starts the real
standard registry and generic style on this build. The remaining `drd_info`
consumer facade and synchronous analysis owner have not yet been removed.

### Physical Display Selection And Descriptor Writer Removal

Source occurrence scopes now associate produced boxes with the existing
execution-boundary token. This is an identity, not a copy of the environment or
an actor-owned source tree. Unicode paragraph source spans retain the token
through merging, line assembly and glyph expansion; plain text expansion also
preserves it. Concater resolves its effective source address before opening the
occurrence scope, matching the address used for the actual boxes.

Actual clicks and physical cursor movement retain the hit box path and layout
root separately from the logical source path. Exploratory hit queries do not
change this selection. Cursor geometry and preparation use that physical
selection within the current frame, so two displays of the same source no
longer collapse merely because their source paths are equal. Navigation also
distinguishes their tokens when comparing positions. Preparation precedes the
cursor-moved callback, rather than waiting for later menu publication.

The production body library built with `-j16`. The temporary production-linked
probe `/tmp/athena-compound-occurrences-probe.cpp` now checks actual coordinate
hits on two displays of one argument under distinct WITH environments, after
Unicode merging, admitted reuse and a subsequent source-suffix change. It
passed; the log is `/tmp/athena-occurrence-hit-probe.log`. Its first endpoint-only
check hit the merged line's structural markers rather than its argument text;
the diagnostic box/source paths established this before the probe was changed
to interior coordinate hits. No implementation fallback was added for that
test-selection error, and temporary diagnostic output was removed.

Old descriptor mutation methods (`set_type`, `set_arity`, `set_accessible`,
`set_env`, the remaining field/tag setters and unused block/tuple declarations)
are removed from `drd_info`. Production had no remaining callers after native
bootstrap conversion. The sole remaining test fixture now constructs its
native contract directly; its assertions were not changed or run as a suite.
The actual evaluator declaration and analysis boundaries remain; this does not
claim removal of the read facade or the synchronous analyzer.

This slice does not complete owner preparation. A changed layout root currently
invalidates the retained physical path; retaining the chosen display across
that change still needs an admitted source/display mapping, not a guessed box
index. Cursor accessibility correction still uses the ambient DRD policy, and
source-mode, metadata and progressive pending contexts remain unfinished. The
`cur[path]` fallback and all three `drd_update` callers still exist. No installed
application binary was replaced, and no Notes documents were accessed.

### Native Cursor Queries And Hit Context Isolation

`editing_query` reads one explicit contract scope with a value-type access and
input-writability policy. Native v3 child access, borders and field environment
come directly from `native_fields`; macro fields read the selected evaluated
scope and record their contract dependencies. Field-environment projection has
one implementation here; the remaining DRD getters delegate to it.

`cursor_navigation` owns recursive correction, accessibility and elementary
movement. Activation, inactive/source access and nested input writability no
longer mutate thread-local modes during recursion. The obsolete global
writability mode and its DRD getters are deleted. Source traversal no longer
round-trips the old integer access mode through a boolean.

Physical hit correction obtains the captured occurrence's checkpoint before
checking accessibility. `cursor_context_at` reconstructs recorded values but
does not restore the live evaluator. Its small occurrence-keyed cache is separate
from `cur[path]`, so probing another display cannot overwrite the current cursor
environment. Only actual cursor preparation publishes that entry. The still
synchronous analysis boundary updates matching cache entries when it publishes.

This is not removal of the DRD facade. `Data/Drd/cursor_adapter.cpp` temporarily
serves callers that have not supplied their own context, and the old access-mode
API remains for search, selection and other consumers. Unprepared hit positions
also still use the owner's available contracts. Source-mode preparation,
metadata editors, progressive positions, changed-frame display continuity and
removal of query-time execution remain open. No installed executable was
replaced by this slice.

The production body library built with `-j16`. The temporary production-linked
`/tmp/athena-native-navigation-probe.cpp` passed nested activation/writability,
policy isolation, and cold/warm physical-display checks with different local
contracts for the same source argument. The selected query does not change the
live owner's contract state. Evidence is in
`/tmp/athena-native-navigation-probe.log`; this is not an interactive editor
acceptance test. No permanent tests or Notes changes were made.

### Recorded Source Fields

Inactive recursive rendering and preamble rendering already produce inner
source-field occurrences under the actual `mode=src` WITH scope. The temporary
`/tmp/athena-source-context-probe.cpp` checked those fields after cold and warm
layout, including that neither rendering nor checkpoint reconstruction executes
the displayed assignments. It passed against the production body library;
evidence is `/tmp/athena-source-context-probe.log`.

Preparation no longer rejects these captured fields merely for having source
mode. The preamble outer-boundary rejection remains: the inner displayed field
and the outer node entry are different contexts. Cursor access treats an
explicit preamble context as source access. `in_source()` reads a captured
context when present; its legacy getter fallback still serves unprepared
positions. The previous process-static environment lock/cache is replaced by
an editor-owned scoped source-mode override for the two mutations that need to
retain their entry mode. Nested exits and exceptions restore that editor's
previous override without touching another view.

This does not remove `define_style_macros` or `restricted_exec` for missing
contexts. Their remaining callers still need prepared/pending behavior before
deletion; source display is not permission to execute the text being displayed.

The source-field preparation and per-editor override changes subsequently built
with `-j16` (`/tmp/athena-source-context-build.log`). The source probe establishes
the recorded-context invariant; no additional test was added for removing the
mode rejection or replacing the two static mutation locks.

### Explicit Search And Selection Policies

The remaining global access modes and `drd_mode.cpp/.hpp` are deleted, together
with their Scheme bindings. Search owns its policy, limits and matching options
per request. Selection correction receives the same native navigation context;
source highlights and hover probes explicitly request source access. Vault
search owns that choice in its common implementation, not in each Qt caller.
The generated `tree-search-source` binding exposes the explicit source query.

The production library built with `-j16`. The temporary production-linked
`/tmp/athena-explicit-access-probe.cpp` checked normal/source search isolation,
source selection without changing subsequent navigation, native math descriptor
exclusion, removal of the old Scheme bindings and actual invocation of the new
binding. It passed (`/tmp/athena-explicit-access-probe.log`). Existing C++ and
Scheme fixtures were migrated to the explicit APIs without adding assertions or
running their suites.

### Native Field Read Cutover

The DRD child type/name/environment APIs and environment helper aliases are
removed. Their consumers in editing, inactive rendering, spelling, tree
correction and conversion now read `editing_query`. Core v3 field types and
names come directly from `native_fields`; scoped macro declarations retain their
own projection. Descendant environment traversal uses the same implementation.
Unused positional descriptor getters are deleted; cardinality limits belong to
`field_layout`. This batch built with `-j16`
(`/tmp/athena-native-field-query-build.log`). No new test was needed for the
direct getter/call-site migration.

Owner acquisition is not yet fully replaced: compatibility entry points still
obtain the current owner before constructing a query. Remaining syntax queries,
synchronous inference, missing-position execution and the `drd_info` owner must
still be removed. These changes do not establish completion of nativization or
deployment of the final application.

### Progressive Prepared Prefixes

Cursor context lookup no longer requires the whole root bridge to be PROCESSED.
A successful partial pass publishes the matching execution base and captured
occurrences for its completed prefix. A progressively pending bridge retains
its entry boundary but does not publish an exit boundary. An unrendered source
has no prepared occurrence; lookup does not execute that source to obtain one.

Typesetter source notifications invalidate the execution base immediately.
Environment invalidation uses the existing editor/typesetter ownership boundary
and does the same; cache-size eviction does not invalidate a valid execution.
This separates an unfinished current layout from a stale layout after an edit.
The legacy missing-position fallback is still present and must be removed in
the query cutover; a prepared miss is not yet a complete pending-command policy.

The library built with `-j16` (`/tmp/athena-progressive-context-build.log`). The
production-linked temporary probe checked a 100-paragraph progressive document:
prefix assignments are visible without executing the suffix, advancing layout
publishes the suffix's own context, only the final pass provides the root exit,
and source assignment clears the old base. Search/selection and generated glue
checks also passed on this build. Evidence is
`/tmp/athena-progressive-context-probe.log`. No Notes files or installed
application executable were changed.

### Native Read API Completion

The remaining DRD read getters are deleted: descriptor/type/name, arity and
insertion, accessibility/border, with-like and syntax reads. Callers use
`editing_query` and `field_layout` directly. `syntax_query` accepts an explicit
contract query and the matching value map; it substitutes syntax without
executing definitions. Ordinary scalar values still belong to that value map,
not to the callable binding registry. Contract and variable reads retain their
dependency receipts.

The production library built with `-j16`
(`/tmp/athena-native-read-cutover-build-2.log`). The first build identified a
missing explicit include in deletion code, corrected before the successful
build. No broad test suite was run. `drd_info` still owns the transitional paired
environment and synchronous analyzer; deleting its getters does not remove
those remaining ownership and scheduling paths.

### Admitted Display Continuity

When layout replaces the root box, the editor first checks whether its selected
source occurrence remains in the current execution's indexed source log. Only
an admitted token is searched in the new box tree; an invalidated token does not
cause a whole-tree search on each keystroke. Lookup requires both the exact
logical source position and the occurrence token. Modifier remapping is checked
at each enclosing box.

Merged Unicode lines require their own lookup: several spans can refer to the
same source field but have distinct occurrence tokens. Their native span map now
filters by the token before converting a source offset into a caret position.
This preserves the selected display through admitted reuse, not just its source
path. Reevaluated occurrences whose old tokens were not admitted still need the
explicit unresolved/pending policy described in the design; this change does
not claim to identify them by geometry or their previous box indices.

The production library built with `-j16` (`/tmp/athena-display-span-build.log`).
The temporary production-linked `/tmp/athena-display-continuity-probe.cpp`
passed (`/tmp/athena-display-continuity-probe-2.log`): a macro displays its one
argument twice under different local environments; the second display is found
by its token in a merged line, after cached layout, and after a sibling source
edit changes the root. The first probe exposed the missing merged-span lookup;
that failure was fixed in the line box's source map, not worked around in editor
geometry. No Notes files or installed executable were changed.

### Detached Analysis And Owner Publication

`macro_analysis` no longer retains a DRD/evaluator reference. Owner-side capture
consumes value-journal differences, declaration changes and binding-identity
changes into immutable resolution records. Equal-valued reassignments remain
different bindings. Changed expressions are frozen once; unchanged resolutions
are shared. The worker receives neither mutable source trees nor an editor.

`analysis_service` uses the existing bounded worker implementation (two workers,
32 queued jobs). Each environment has at most one admitted job and one coalesced
latest demand. Capacity exhaustion retains one weak waiting entry per owner;
completion admits waiting owners instead of dropping demand or executing it
inline. Closing an environment cancels its service without joining a worker.
Editor wake-ups transport actor/view IDs, not editor pointers.

The detached engine updates its dependency closure incrementally. Boost.Graph
computes recursive components. A proposal retains its complete resolution
context and component read sets; owner admission checks binding identity,
presence, values and authored declarations using the current captured records.
Rejecting one component also rejects its dependent callers, not unrelated
components. Cold synchronous preparation and later interactive requests share
retained immutable results; synchronous preparation never borrows a busy worker.

Before polling a new result, preparation also checks the previous publication
and retires stale component projections. Therefore a delayed result is not the
only invalidation boundary: editing a dependency cannot keep its old dependent
facts published through the waiting period. Unchanged prepared inputs reuse the
admission result instead of repeatedly traversing expressions.

Field analysis now carries typed unresolved/resolved/ambiguous/dynamic evidence
for type, accessibility and environment. Type conflicts propagate through
callers as ambiguity, with `TYPE_UNKNOWN` as the unresolved value, rather than
manufacturing the persisted/schema type `TYPE_ERROR`. Conditional visibility and
environment conflicts remain native evidence, not ad hoc tree attributes.
Consumers still need the complete explicit pending policy specified above.

The service belongs to `edit_env`; `drd_info` no longer owns an analyzer or
manual declaration-invalidation methods. Native scope/input differences are
the invalidation source. The legacy importer directly uses the declaration
decoder and explicit detached synchronous preparation. The old `drd_update`
method is removed. Source identity adoption instead prepares an independent
style/init/preamble environment, without using the current cursor, cached
cursor context or approximate document-prefix execution.

Menu refresh no longer requests preparation. Normal layout/cursor lifecycle
prepares interactive contexts and publishes ready worker results. The remaining
general variable-query prefix fallback is isolated in a temporary evaluator so
it cannot replace the selected prepared contract owner; its eventual removal,
metadata-field transfer and missing/invalidated occurrence policy remain open.
Temporary evaluation restores its source filename and secure flag as well as
values, bindings and argument maps.

Production-library builds used `-j16`:
`/tmp/athena-native-analysis-owner-build.log`,
`/tmp/athena-source-contract-owner-build.log`,
`/tmp/athena-analysis-retirement-build.log`, and
`/tmp/athena-query-owner-isolation-build.log`.
The temporary production-linked `/tmp/athena-detached-analysis-probe.cpp`
passed (`/tmp/athena-analysis-retirement-probe.log`): worker/owner separation,
same-valued binding replacement, atomic recursive rejection, dependent rejection,
independent publication, retirement while no new result is available, restored
context, unchanged publication reuse, and typed ambiguity through a caller.
No permanent suite, Notes writes, deployment or commit was performed.

This is not full DRD removal. Large changed-expression capture and a large
publication batch are not yet time-sliced; cold package/identity preparation is
still explicitly synchronous; missing/source/metadata contexts and the old
current-owner adapters remain. The `drd_info` scope/value handle has lost its
read/analysis authority but still needs its remaining consumers migrated before
deletion. Final application linking and deployment are still outstanding.

### Metadata Field Contexts

`evaluation_environment_checkpoint::detach` now transfers immutable values,
their matching contract snapshot, macro argument values, package origin and
the structured source URL. It reuses existing frozen binding expressions and
does not transfer source observers, source paths or an evaluator. The receiving
actor thaws its own variable/argument maps; source locations become decoration
paths rather than addresses into the original document.

Inline enunciation slots capture their source-node entry context. The existing
UI endpoint carries the latest generation-bound snapshot, and buffer bootstrap
installs it before content initialization or the first layout. Live slot
refresh compares changed variable candidates, binding/declaration state and
argument context. Actual changes update the existing input actor without
replacing its text/cursor; unchanged contexts do not refreeze the environment.
Losing the source occurrence closes the slot through its existing flush path.
Node properties also pass the selected node's immutable context into their
structured-text editors. Their modal draft retains the opening context, and
commit continues to use the existing source lease/content validation.

The first focused probe exposed an important distinction: a metadata field is
not another source document. Assigning document UUIDs to its local macro content
required an inferred child role before the field could even open. Field buffer
bootstrap now retains v3 editing/serialization but does not create a document
identity index. It cannot opt into source identity adoption. Source commit owns
field validation and persistence; ordinary document identity preparation is
unchanged. This is not a permissive fallback for unknown document macros.

Production-library `-j16` builds succeeded in
`/tmp/athena-metadata-context-build-4.log` and
`/tmp/athena-metadata-field-owner-build.log`; the final slot-close adjustment
also built successfully in `/tmp/athena-metadata-context-final-build.log`.
The temporary production-linked `/tmp/athena-metadata-context-probe.cpp` passed
at 20:43 on 2026-10-26 (`/tmp/athena-metadata-field-owner-probe.log`). It checks
cross-thread local macro execution, retained binding identity, argument-value
transfer without original source paths, scalar/binding/declaration invalidation,
snapshot isolation and real v3 BufferActor bootstrap without field UUIDs.
No Notes files, installed executable or permanent test suite were changed.

This completes this metadata transfer boundary, not the full native cutover.
General missing/repeated-display preparation and query-prefix removal, the
remaining old owner adapters, inference pending-policy consumers and budgeted
large-input capture/publication remain. Application linking/deployment are not
yet performed.

### Recorded General Queries And Explicit Conversion

General `get_env_value` queries now select recorded source occurrences before
considering the remaining cold-position fallback. Previously only cursor
preparation populated these entries: querying a different displayed source path
could ignore a valid record and run the old ten-child heuristic instead. Cached
recorded contexts are distinguished by execution base, trace and physical
boundary, not merely source path. Root entry and exit use their corresponding
boundaries.

Conversion uses an explicit matching checkpoint and restores it under a
temporary evaluation scope. Values, binding identities, arguments, package and
source context travel together; overlays no longer replace the whole variable
map and borrow the live editor's argument stack. Whole-document expansion
explicitly selects style/init preparation before body execution. Fragment
conversion requires an existing source context, and clipboard converters pass
the selection's source position (including mathematical HTML fragments).

`/tmp/athena-prepared-query-before.log` demonstrates the old error: paragraph 12
assigned `late`, the paragraph 14 recorded context contained `late`, but the
query returned `early`; the root exit returned `uninit` despite its recorded
`late`. After the change, the production-linked temporary
`/tmp/athena-prepared-query-probe.cpp` passed at 21:02 on 2026-10-26
(`/tmp/athena-prepared-query-after.log`). It also checks local/outer contexts and
that fragment execution restores the editor's original contract/value state.
The `-j16` production library build is recorded in
`/tmp/athena-prepared-query-build.log`.

The same probe deliberately inspects an important remaining gap: a newly
created script field has no layout occurrence until the next pass. Those
queries still use the isolated legacy fallback. Deleting that fallback without
providing edit-owner preparation would change typing semantics. Native script
context includes display/condensed style, nesting level and vertical position,
not merely math mode; JSON field shape alone does not specify it.

### Pending Access At Displayed Macro Arguments

`cursor_navigation::is_mapped_cursor` checks one already-admitted physical
source path. It is not another schema-wide access mode. Normal tree traversal
still uses ordinary explicit/inferred permissions; only the exact displayed
route may use its actual presence as evidence when inference is unresolved,
ambiguous or conditional. Native fields and authored accessibility/writability
restrictions remain authoritative. Inferred child environments do not grant
source-mode permissions along that route.

Physical hit testing first verifies the box boundary against the current source
occurrence, then uses mapped accessibility. Current-cursor correction and the
post-physical-movement check use the same rule. Missing or stale boundaries
cannot authorize a route, and no inferred permission is published as a side
effect. This prevents pending analysis from moving a click out of an actually
displayed macro parameter while keeping unknown, undisplayed arguments closed.

The production library built with `-j16`
(`/tmp/athena-mapped-cursor-build.log`). A temporary production-linked probe
(`/tmp/athena-mapped-cursor-probe.cpp`) passed at 21:22 on 2026-10-26
(`/tmp/athena-mapped-cursor-probe.log`). It exercises an actual editor layout
whose locally assigned macro has no inferred access facts, hits its blue
argument at source path `[1,0,3]`, retains that exact cursor through correction,
and rejects its old occurrence after source replacement. It separately checks
declared denial, declared read-only fields, ambiguous analysis, native delimiter
descriptors, builtin restrictions and no permission leakage to an unused
argument. No permanent tests, Notes changes or installed binary changes.

The unused `get_style_drd` entry point is removed. Import arity correction now
takes an explicit native `contract_scope` rather than a `drd_info` owner; its
LaTeX caller selects the thread-local standard scope, and the existing import
telemetry describes arity correction. This is a consumer removal, not deletion
of the remaining current-owner adapters. The production library rebuilt with
`-j16` (`/tmp/athena-contract-import-build.log`); this straightforward API
cutover did not add or run tests.

### Immutable Builtins And Explicit Identity Inputs

`builtin_contracts` owns the process-wide immutable native baseline. Its
one-time initialization registers tree labels before constructing native
contracts; consumers fork a snapshot rather than inheriting a mutable standard
DRD owner. The parser code table is materialized privately on each thread from
the immutable native label catalog. Parser-local changes cannot change the
identity of a builtin label.

`std_drd`, `STD_CODE`, `init_std_drd`, `standard_drd_for_thread`,
`standard_codes_for_thread` and `std_contains` are removed. Source identity
classification, planning and initialization receive `const contract_scope&`;
editor preparation returns a native scope, not an evaluator owner. The existing
test sources were adjusted to these API changes without adding tests.

The production library built with `-j16`
(`/tmp/athena-native-builtins-build.log`). The temporary production-linked
mapped-cursor probe passed at 21:49 on 2026-10-26
(`/tmp/athena-native-builtins-probe.log`), including concurrent cold
initialization on four threads and isolation of private scopes/parser tables.

### One Actor-Local Evaluation State

`evaluation_state` replaces the refcounted `drd_info` handle. It owns a native
contract scope and the actor-local variable map. `edit_env` borrows that map
directly: there is no second DRD variable view and no `set_environment`
synchronization. A state copy forks contracts and copies the mutable variable
map. Worker inputs remain the separate detached native snapshots.

Temporary evaluation no longer replaces an owner handle. It saves and restores
values and contracts in place, so the editor, evaluator and active execution
scope retain the same owner address. Root evaluation transactions likewise
commit or roll back that single paired state. Source/argument stacks, input
revisions and reference outputs keep their existing evaluator ownership.

Scheme execution scopes borrow the native state on their checked owner thread.
Standalone import/correction scopes own a value. `prepare_document_context`
returns prepared native state; offline identity migration now requests exact
source-entry scopes instead of the retired document-wide preparation cache.
Legacy import compilation uses a contract scope directly. The obsolete `set-drd`
binding and its two print-wrapper calls are removed; actor context selection
already belongs to `SchemeExecutionScope`, not a URL-based Scheme setter.

`drd_info.cpp/.hpp` and `drd_std.cpp/.hpp` are deleted. The cursor utility entry
points now live in `Data/Tree/cursor_execution.cpp` and borrow native execution
state. Tree-label registration belongs to `Kernel/Types`, native builtin tag
declarations to `Data/NodeContracts`, and environment names to `Typeset/Env`.
The obsolete `Data/Drd` directory is removed. Persisted `drd-props` syntax still
feeds native declarations; its spelling is not an old runtime authority.

The production library built with `-j16`
(`/tmp/athena-native-evaluation-state-build.log`). The temporary
production-linked probe passed at 21:58 on 2026-10-26
(`/tmp/athena-native-evaluation-state-probe.log`): direct variable-map ownership,
private copied state, nested temporary restoration, exceptional root rollback,
successful root commit and the actual mapped-cursor case. It also verifies that
the generated runtime no longer registers `set-drd`. No permanent test was
added and no suite was run.

This does not complete the cutover: fresh-edit/cold-position preparation,
query-prefix removal, remaining pending consumers and budgeted large-input
capture/admission are still open. The installed executable has not been
changed. Final application deployment must also refresh compiled Scheme
resources, including the changed print wrapper.

### Persistent Snapshot Storage Selection

The retained-snapshot write path in `contract_scope` cloned whole `std::map`
tables for definitions, declared contracts and inferred projections. Declaration
source state cloned its complete target map too. A transaction reduces the
frequency but not the size of that copy, so it cannot meet the owner-work
requirement for a small edit in a large scope.

The selected implementation is [Immer](https://github.com/arximboldi/immer),
pinned to upstream v0.9.1 commit
`160f0d7025321e5dc1fc3f0b7742ca7621d43981`. Its native persistent HAMT maps share
unchanged branches and provide structural-sharing-aware `diff`. Default
reference counting is thread-safe; mutable legacy trees remain excluded from
these maps. The library is header-only and Boost Software License 1.0, matching
ATHENA's C++ toolchain without another system runtime dependency. It is fetched
through the existing CMake dependency mechanism, with upstream tests/examples/
docs/extras disabled. No system installation is performed.

Alternatives considered: `std::map` and Qt implicit-sharing maps still copy a
whole retained table; Boost.Container's standard-style maps do not provide
persistent snapshots; a custom HAMT or unbounded overlay chain would duplicate
a mature implementation. Ordered declaration group application remains ordered
data, not an unordered-map iteration side effect. This storage change is not
by itself completion of resumable expression capture or budgeted publication.

Contract entries, inferred projections, bindings, declaration targets and
detached analysis inputs now use these persistent maps. Snapshot writes copy
only the affected branches; scope and worker input differences skip shared
branches. Ordered group contributors remain a shared ordered map, copied only
when group declarations change. The dependency include boundary shields Immer
from the legacy `make` preprocessor macro without modifying upstream headers.

Variable-input snapshots use persistent name-to-assignment receipts as well.
Each batch replaces receipts for assigned names and erases removed names, so
same-name reassignment and restored branches remain distinguishable. The old
64-batch history compaction and its ensuing all-name invalidation are removed.
Analysis capture reuses the evaluator's already-frozen binding payload rather
than comparing and copying it a second time. Changed unbound expressions still
require separate bounded-capture work.

Production library builds passed with `-j16`
(`/tmp/athena-persistent-contracts-build.log` and
`/tmp/athena-persistent-inputs-build.log`). The temporary production-linked
probe passed at 22:34 on 2026-10-26
(`/tmp/athena-persistent-contracts-probe-2.log`). It checks retained snapshots,
changed-key discovery, inherited deletion/inference masking, restored input
branches, same-name reassignment and declaration-group withdrawal/restoration.
Its initial group fixture had no callable binding, so its declarations correctly
remained pending; the corrected fixture checks that state and then binds the
macros before checking ordering. No production change was needed for that
fixture failure. Retaining a snapshot, modifying one key and finding its diff
256 times took 0.574 ms for 8,192 entries and 0.734 ms for 32,768 entries on this
run; these observations are not timing thresholds. The existing temporary
single-owner and physical mapped-cursor checks also passed. No permanent suite,
Notes changes or executable deployment occurred.

The next integration step still needs the real consumers: `texmacs-exec*`
(`var_texmacs_exec`) is used by graphical `box-info`, `frame-direct` and
`frame-inverse` operations, not merely file conversion. Its missing-context
behavior must be fixed with interactive edit preparation rather than blindly
replacing it with a conversion error. `contract_scope(snapshot)` now shares its
immutable group-extension projection when forking a child scope. Group
publication replaces that projection wholesale rather than modifying it.

### Worker-Side Symbol Discovery

Owner capture no longer recursively scans definitions for tag spellings and
referenced variable names. The input baseline already contains all visible
variables; subsequent assignment/presence receipts and contract/binding changes
cover additions, removals and shadowing. Unknown references therefore remain
absent receipts, not a reason to traverse the expression on the owner.

The detached engine discovers symbols on its private thawed tree before
building dependencies. This iterative walk checks cancellation and reads the
process tree-label catalog only for label spelling; the catalog is protected by
its shared mutex, and production labels are registered before a source tree can
reference them. No binding, authored declaration or variable value is taken
from another owner. Numeric tags and literal `compound`/`map-args` names are
both resolved. Inference and dependency receipts still come exclusively from
the captured native inputs, including absent targets that may be defined later.

The production library built with `-j16`
(`/tmp/athena-worker-symbol-build.log`). A focused temporary probe passed at
22:50 on 2026-10-26 (`/tmp/athena-worker-symbol-probe.log`): an initially missing
target is referenced by both a numeric tree tag and `compound`; defining it
updates the caller's access and mathematical environment, and deleting it
removes that access again. Notifications prove the analysis ran off-owner.
No permanent tests, Notes changes or executable deployment occurred. Large
unbound-value freezing, proposal admission/publication and fresh-edit context
preparation remain separate unfinished work.

### Prepared Inference Projections

The detached engine now materializes complete native inference projections,
not just the analyzer's field results. Captured resolutions retain the full
immutable authored contract. Preparing the analyzer's reduced input and merging
inferred fields into that authored baseline both happen on the worker; border,
writability and other non-inferred attributes are preserved. Full declaration
receipts prevent an old projection from overwriting a newer authored attribute.

Facts and projected contracts use persistent maps. An unchanged input snapshot
allows owner publication to replace the inference map and retain the facts
without enumerating components or assembling field vectors. Repeated publication
of that snapshot is a no-op. This is a snapshot match, not a document revision
shortcut: stale proposals still validate their dependency components and only
share their admitted projections. The owner no longer reconstructs those
projections. Capture uses `changed_declarations` rather than effective-contract
changes, so inference output is not rediscovered as an analysis input.

Production-library builds passed with `-j16`
(`/tmp/athena-projected-analysis-build.log` and
`/tmp/athena-projected-analysis-inputs-build.log`). The temporary production-linked
probe passed at 23:10 on 2026-10-26
(`/tmp/athena-projected-analysis-probe.log`). It covers matching snapshots,
same-valued rebinding of a recursive component before publication, acceptance
of independent facts, non-inferred attribute preservation and changes made
after worker completion, unchanged reuse, and late/deleted dependencies.
No permanent suite, Notes modification or executable deployment occurred.

Stale proposal admission and retirement still enumerate the component graph;
this change does not claim those paths are budgeted. The worker's component
read sets can supply a reverse receipt index so sparse changed inputs visit
only affected components. Large changed-input capture, large affected closures,
fresh-edit context preparation and the old variable-query prefix fallback also
remain open.

### Sparse Stale-Result Admission And Retirement

Worker proposals now include a reverse receipt index and reverse component
edges. Admission uses structural diffs of the persistent input maps to find
changed receipts, then visits only their readers and dependent components.
Absent entries and explicit empty resolutions have the same negative-receipt
meaning. Rebinding still invalidates a receipt even if its expression is equal.

The owner adopts the worker's persistent projection map and erases only rejected
members. Retirement without a new proposal uses the same dependency closure.
Neither path builds a complete accepted-name set or walks all facts; the former
duplicate `applied` facts table and per-target inference publication API are
removed. Unaffected projection objects retain their identity. This does not
bound the cost of a genuinely large affected closure or large new input capture.

The production library built with `-j16`
(`/tmp/athena-sparse-analysis-build.log`). The focused temporary probe passed at
23:29 on 2026-10-26 (`/tmp/athena-sparse-analysis-probe.log`), covering stale
recursive components, callers outside those components, independent projection
identity, fresh-result recovery, and retirement before requesting new work.
No permanent suite, Notes modification or executable deployment occurred.

The remaining editor lifecycle work is unchanged: source-prefix heuristics in
`typeset_exec_until`, unprepared edited fields, initial-value preparation and
the interactive graphics consumer of `var_texmacs_exec` must move to explicit
owner preparation before the old query-time fallback can be removed. The goal
is not complete and the intermediate binary has not been deployed.

### Prepared Initial-Environment Queries

`defined_at_init` and `get_init_value` no longer prepare a missing style context
or execute a background pattern. An editor starts with its native default
checkpoint. Loading data prepares the complete style/init checkpoint before
publishing command state; explicit style changes, initial-override removal and
field-environment replacement publish their new baseline at those operations.
Plain initial overrides remain immediately readable. `set_data` batches its
inputs once instead of applying the same init overlay twice, and actor reloads
no longer call the formerly empty `init_update` a second time.

Background expression preparation is owned by those input changes. It restores
the initial checkpoint plus current overrides in a temporary environment, then
retains the computed result for reads. File/pattern resolution and expression
side effects therefore cannot be triggered by an initial-value getter or leak
into the live cursor's values/contracts. Normal layout and explicit conversion
preparation still have their own evaluator responsibilities.

The production library built with `-j16`
(`/tmp/athena-initial-query-build.log`). A focused temporary probe passed at
23:41 on 2026-10-26 (`/tmp/athena-initial-query-probe.log`), covering construction,
load, initial override/reset, field enter/exit, style publication and isolation
of a background expression containing an assignment. Its first launch failed
before these checks because the probe subclass omitted virtual-base
initialization; correcting the probe constructor required no production change.
No permanent tests, Notes changes or binary deployment occurred.

Initial getters are now read-only, but `typeset_exec_until` still contains the
source-prefix heuristic for unprepared cursor/path queries. Removing that path,
preparing fresh edits, and confining interactive graphics evaluation to its
selected context remain required before completing the native cutover.

### Native Script Field Contexts

The ordinary evaluator's generic child traversal did not enter the environments
used by native script layout. Replacing the editor's approximate prefix with
that traversal alone would therefore still give wrong environments for freshly
inserted script fields. `native_math_context_scope` now owns the shared math,
script-style and vertical-position scopes. Layout retains one script scope
around the whole slot group, with separate position scopes for each slot; it
does not repeat font-level setup for every sibling or change their order.

`MATH_SCRIPTS`, `MATH_ANNOTATION` and `MATH_LONG_ARROW` use those scopes in full
evaluation and both partial-evaluation paths, including macro-argument lookup.
Complete evaluation restores the incoming environment and preserves metadata;
partial evaluation retains the selected inner state. This is an existing
evaluator preparation primitive, not permission to execute it in getters or to
select the first display of a repeated argument.

The production library built with `-j16`
(`/tmp/athena-native-script-context-build.log`). The focused temporary probe
passed at 00:00 on 2026-10-27
(`/tmp/athena-native-script-context-probe.log`), comparing prepared field values
against actual layout occurrence records for nested subscripts/superscripts,
bases, annotations and long-arrow labels, plus complete expansion and a macro
argument inside a script. No permanent tests, Notes changes or deployment.
The editor's prefix fallback has not yet been switched: fresh-edit preparation,
other native field semantics and occurrence selection still need integration.

### Direct Prepared Boundary Contexts

Owner-local checkpoints now share a variable baseline and persistent frozen
overrides. Reading one value no longer copies the whole variable table; full
materialization is explicit for evaluator restore, transfer or a full-env
request. Interactive graphics evaluation uses a temporary selected environment
and restores the live values, contracts and derived font state afterward.

The typesetter now publishes a persistent boundary-to-context index alongside
its execution base. Real execution captures the current native contract scope,
input receipts and shared variable overrides at source entry/exit and WITH-body
boundaries. Variable-write capture shares its existing frozen payload with the
context recorder. Prepared field queries select this index instead of searching,
copying and resolving a prefix of the event trace.

Admitted replay prepares boundary contexts during its existing read/effect
traversal, not a second source evaluation. Its variable effects are persistent;
structural diffs update only changes since the preceding boundary, including
local-scope restoration. A private draft is published only after admission
succeeds. An unread incoming variable is therefore rebased correctly even when
the same bridge and boundary tokens are reused. Environment replacement rebases
the recorder; transaction rollback restores its previous index and values;
temporary evaluation suspends it. Root failure discards the published index.

Production-library builds used `-j16`; the last completed build is recorded in
`/tmp/athena-boundary-context-final-build.log`. The focused temporary probe passed
at 00:39 on 2026-10-27
(`/tmp/athena-boundary-context-final-probe.log`). It compares indexed values and
contracts with execution-prefix reconstruction, covers native script contexts,
admitted reuse with changed unread inputs, rejected replay, transaction rollback,
local-scope effects, temporary isolation, checkpoint transfer and interactive
graphics isolation. No permanent tests, Notes changes or executable deployment.

This removes prefix reconstruction from already prepared queries, not the
separate `restricted_exec` fallback for missing occurrences. Fresh-edit/cold
preparation and pending-state integration remain the main cutover work; large
input capture/admission budgeting and final application deployment remain open.

### Native Content Contexts And Prepared UI Reads

`DELIMITED`, `MATH_ACCENT` and all eleven `MATH_VARIANT` kinds now share their
mode/font scopes between layout, full evaluation and both partial-evaluation
routes. Only the schema's expression field is evaluated; descriptors remain
data. The variant renderer no longer maintains a second font dispatch table.

Command and focus snapshots are unavailable until the cursor has an admitted
occurrence. Mode and paragraph-spacing publication read that context directly;
spelling suggestions also require a prepared source context. Native action
dispatch applies pending tree/environment layout before checking its command
snapshot, as keyboard dispatch already does. The right footer keeps its previous
field description while statistics can still update; missing contexts do not
create an idle retry loop. Page dimensions retain the last successful layout's
geometry (or initial prepared geometry before any layout), independent of cursor
environment selection. Slot-width measurement now isolates evaluator state.

Interactive `typeset_preamble` now requests asynchronous analysis and publishes
ready results instead of synchronously running macro inference on environment
refresh. Explicit cold style/source/offline preparation is unchanged.

The production library built with `-j16`
(`/tmp/athena-native-prepared-ui-build.log`). A focused temporary probe passed at
01:22 on 2026-10-27 (`/tmp/athena-native-prepared-ui-probe.log`), checking native
field environments against actual layout, full/macro-argument execution,
unprepared/prepared command-state transitions, slot measurement isolation and
page-geometry independence. The first expanded probe had incorrectly called
`node::edit` on a tree without metadata; the core identified that fixture error,
which was corrected with `node::set`, without a production fallback.

This is not completion: `restricted_exec`, `table_descend` and
`define_style_macros` still service unprepared variable queries. Fresh-edit
preparation, remaining native block/grid context semantics, command pending
integration and large-input capture/admission budgeting remain. No Notes changes,
permanent tests or executable deployment were performed for this group.

The complete `ATHENA.bin` target subsequently built with `-j16`
(`/tmp/athena-native-application-build.log`). A headless launch with an empty
temporary profile verified the generated runtime interfaces by writing a closed
result file, `/tmp/athena-native-binding-check-VKafVP/bindings.scm`:
`tree-search-source` is defined; `set-drd`, `set-access-mode` and `get-access-mode`
are not. The initial stdout-only check was inconclusive because the requested
Scheme quit takes the normal `_exit` path; its missing stdout marker was not a
crash or evidence of an absent binding. No executable was deployed.

### Native Grid Row Contexts

Native table layout and AVD counter projection now share grid-numbering
validation and the row context implementation in
`Typeset/Env/native_math_environment.*`. Counter advancement, quoted rich labels,
whole-block label inheritance, pending-tag isolation and row UUID binding have
one implementation. Both paths share the display-math environment scope as
well; semantic projection preserves MathBlock/MathGrid metadata.

The table renderer records source occurrences for its direct grid, row and cell
entry points. Previously those wrappers bypassed the generic concater, leaving
only their contents recorded. Boundary queries can now select the actual
rendered row/cell context instead of reconstructing it from format declarations.
Incomplete/failed layout still has no completed exit boundary.

The complete application and targeted row-numbering executable built with
`-j16` (`/tmp/athena-native-grid-final-build.log`). At 02:03 on 2026-10-27,
the existing checks for counters/bindings/destinations, empty/native-math tags,
nested/tag grids, single execution of nested MathBlocks, and whole-block kinds
and rejected overrides all passed
(`/tmp/athena-native-grid-numbering-check.log`). The temporary production-library
probe passed at 02:07 (`/tmp/athena-native-grid-context-probe.log`): grid/row/cell
entry and exit contexts retain display mode, row/column indices and quoted
labels; the direct index still agrees with the execution trace; successive
binding edits before layout do not reuse stale query checkpoints. A native
subscript/input/nested-superscript/input sequence made zero unprepared variable
queries. This does not remove the editor's unprepared-query fallback or complete
fresh-edit/pending-state integration. No permanent tests or deployment.

Source modification notifications now clear both occurrence and path-query
checkpoints before cursor correction. The bridge trace is not a source revision:
it remains unchanged between successive mutations until layout runs, so it
cannot alone validate a cached unprepared query. Cursor-moved Scheme hooks also
require an admitted current occurrence; an old valid cursor box after mutation
does not authorize mode-dependent callbacks. `apply_changes` delivers the hook
after the new layout through its existing `go_to_here` path.

The next query consumers to handle explicitly are source-position search and
manual spelling (`edit_search.cpp::test`, `edit_spell.cpp`), table format queries
at arbitrary paths (`edit_table.cpp`), selection environment reads and dynamic
macro insertion/completion. Search can visit unrendered source, so replacing its
missing context with a default mode or silently skipping that match is not a
valid cutover. The native editing probe above establishes only its exercised
sequence, not that all callers of `get_env_value` are ready for fallback removal.

### Prepared Presentation And Completion Consumers

Live-spelling menu queries and dictionary insertion now select prepared source
contexts. They do not reconstruct an environment for a spelling range; absent
context produces no suggestions and prevents a dictionary write. The preferred
display occurrence is retained even when the word starts before the cursor in
the same source leaf. Selection image export likewise requires the selected
fragment's prepared math/display context before it creates output.

Caret, context-frame and match-highlight drawing no longer use general variable
getters. The caret waits for its current occurrence; presentation colors may use
the initial document color until context is ready. Link-hover color selects the
actual hit box's source occurrence, not whichever display a path-only lookup
would choose.

Hybrid-name completion reads captured native definition signatures instead of
materializing and filtering the entire environment. A request immediately after
an edit is retained by cursor path and prefix and resumed after layout; moving
away or changing the requested text discards it. This is presentation work only:
it does not apply a macro, wait on inference or cause another layout pass.

Still pending: hybrid activation/argument preparation after a completion changes
the name, selection and table queries inside edits, and the unrendered-source
search/manual-spelling workflows. The latter have no current shipped menu or
keybinding entry but retain exposed Scheme interfaces and keyboard branches;
their retirement has been asked separately, not assumed. Source identity
planning also must distinguish pending inferred field roles from invalid source.
In particular, a new analysis owner currently clears inherited inference before
its own proposal arrives; retaining cold-prepared facts requires their matching
resolution evidence, not blindly trusting an inherited projection.

The next macro-edit slice has three concrete post-mutation callers:
`complete_choose` replaces the HYBRID name before invoking preparation or
activation; `activate_hybrid` prepares arguments and recursively activates the
new node; and its no-argument branch clears the inserter before calling
`make_compound`, which reads the macro definition again. The existing
`source_mode_during_edit` retains only the source-mode decision. These operations
need to capture the invocation's prepared macro context before replacing the
inserter and pass it through that bounded action. Do not retain an arbitrary
pre-edit environment for later independent commands or insert a layout pass
between primitive mutations.

The full application built with `-j16`
(`/tmp/athena-native-query-consumers-final-build.log`). The temporary production
library probe passed at 02:37 on 2026-10-27
(`/tmp/athena-native-completion-context-final-probe.log`), verifying captured
macro parameter labels, post-edit completion deferral, resumption after layout
and zero unprepared variable queries in that sequence. Its initial assertion
incorrectly inspected the candidate list after a missing GUI endpoint had
closed the completion session; the diagnostic confirmed that the prepared
definition and signature were present. The fixture was corrected to inspect
the generated labels, without adding any production fallback. No broad suite,
permanent test, Notes access or executable deployment was performed.

### Bounded Macro Invocation And Inherited Inference Evidence

Hybrid completion selection captures its invocation checkpoint before changing
the name, including keyboard-command lookup before mutation. Argument preparation,
recursive activation and compound creation retain that context only for the
bounded operation. Invocation queries select the enclosing source occurrence,
outside the name field's display-only text style. Ancestor selection follows the
remembered physical box path, so repeated macro arguments can retain different
local definitions. Direct compound callers without a prepared invocation and
command actions querying newly inserted fields are still pending migration.

Inference now carries immutable input/dependency evidence through snapshots and
local target restoration. The evidence does not own output projections, avoiding
an ownership cycle. Effective inference uses complete persistent maps rather than
parent fallthrough/tombstones; inheritance shares those maps. Value and provenance
maps are separate, preserving structural value diffs when a worker renews evidence.
Receipt counts track the live publications, including mixed publications after
restoring individual local targets. New owners validate these receipts rather
than deleting all inherited facts while waiting for their own worker. Rejection
still follows recursive components and their caller closure, and only removes
an entry when it carries the rejected evidence.

The complete application built with `-j16`
(`/tmp/athena-native-inference-evidence-final-build.log`). At 03:23 on 2026-10-27,
the temporary production-library probe passed
(`/tmp/athena-native-inference-invocation-probe.log`): nullary activation,
parameter preparation, recursive selection activation, math invocation and
different definitions at repeated displays made zero unprepared variable queries;
matching cold inference survived a new owner, changed dependencies invalidated
callers without discarding unrelated facts, and mixed local restoration preserved
the matching publication. The detached editor fixture holds an owning editor
handle and disables plugin keyboard lazy loading because it has no BufferActor.
Earlier fixture failures were diagnosed from their actual stacks, not covered by
production null checks. No permanent test, Notes access or deployment.

This is not the final cutover. Selection replacement and table editing still have
post-mutation variable queries; legacy manual search/spelling retirement has not
been authorized. `restricted_exec`, `table_descend` and `define_style_macros`
remain. Genuinely pending source-identity roles, bounded large-value capture and
large dependency-closure admission also remain, followed by final deployment.

### Table Entry Contexts And Insertion Preparation

Existing table commands retain their table-entry checkpoint across their own
mutations. Reading a format no longer inserts TFORMAT into a bare TABLE;
format writes own that transformation. Cell defaults come from the table entry,
not whichever cell contains the cursor. Rectangle cuts use the table command's
bounded context, and decoration payload composition does not query a displayed
environment for its stored data. Table layout records nested TFORMAT entries
and subtable inheritance; repeated displays retain their own source contexts.

The follow-up call-site checkpoint plumbing was insufficient for construction:
a table macro can establish WITH and TFORMAT values inside its argument. The
2026-10-28 change prepares that prospective argument using the existing native
partial evaluator before make_compound performs its own mutations. Preparation
has separate evaluator state and reference/auxiliary outputs. The evaluator now
reports whether it actually reached the requested argument; no source-prefix
scan is involved. The resulting context survives block-table relocation.
Subtable insertion constructs its empty cell bodies using the parent cell's
effective block/hyphen properties before replacing the cell content.

The complete production target built with `-j16`
(`/tmp/athena-native-table-insertion-build.log`). A temporary production-library
probe passed at 15:23 on 2026-10-28
(`/tmp/athena-native-table-insertion-probe.log`), checking macro-local formatting,
minimum dimensions, block relocation, parent-cell inheritance and zero
unprepared environment queries during those operations. No permanent test,
Notes access or executable deployment was performed.

Remaining work includes missing-context command continuations (including hybrid
activation before a later compound operation can decline), dynamic arguments
that do not render, composite selection/paste preparation, unrendered source
queries, pending identity roles and bounded analysis capture/admission. The
general `restricted_exec` fallback and table's unprepared-query path still exist.
The 2026-10-27 follow-up commits also migrated physical-ancestor selection across
navigation, focus and editing consumers and centralized BACKUP interpretation
in `effective_value`; they did not remove those remaining fallbacks.

### Prepared Clipboard Replacement

Native clipboard preparation captures a validated detached envelope, target
mode/language, source-move token and any admitted table-entry checkpoint. The
opaque plan is command-local on the destination owner, not a reusable request
for arbitrary future revisions. AVD's existing multi-actor barrier holds each
owner between preparation and application; replacement now applies this plan
after its range cut, instead of installing a temporary clipboard and invoking a
second validation/environment lookup against the modified source.

Ordinary cross-mode paste composes the mode wrapper and payload before insertion,
keeping the cursor inside the payload. Identity duplication and source-move
claims retain their existing policy. Table writes accept the captured entry
checkpoint when the original table path remains the destination. Balloon,
marginal-note and geometry wrapping directly transfers the selected source into
the new wrapper; those local moves no longer run clipboard mode inference in a
freshly created field. Slideshow balloon wrapping retains its separate Scheme
clipboard contract.

This step does not remove unprepared getters. Native preparation still uses the
old query path if its target has no admitted context; general `var_insert_tree`
and table relocation after removal of an enclosing source require their own
preparation. Source-mode/range cuts that change the insertion owner's semantics
must be handled explicitly, not by treating every pre-cut leaf environment as
the environment of the retained parent. Pending command/identity handling and
bounded analysis capture/admission also remain open.

The production target built with `-j16`
(`/tmp/athena-native-prepared-paste-build.log`). The temporary production-library
probe passed at 15:47 on 2026-10-28
(`/tmp/athena-native-prepared-paste-probe.log`): range replacement retained the
target mode, cross-mode insertion kept the caret in its formula, invalid text
paste was rejected before mutation, whole-formula replacement retained the
formula wrapper, and replacement/expansion inside a table used its captured
context. The audited replacement and table operations made zero unprepared
variable queries. No permanent test, Notes access or executable deployment.

### Prospective Wrapper Fields

`var_insert_tree` no longer cuts to the primary clipboard and queries the new
field after inserting an empty wrapper. It prepares that prospective field from
the selected physical insertion context, builds and validates its paste plan,
then performs the local transfer. The primary clipboard is not changed, and
source identities are retained for this same-document operation without a
source-move ticket. Templates introducing tables prepare their table entry
separately from the destination cell. A missing source context or an unrendered
template argument leaves the selection untouched; missing-context continuation
is still separate work.

The existing native partial evaluator now returns actual destination reachability
through its direct-source traversal as well as argument expansion. This also
propagates a failed nested destination instead of treating any matching ARG as
success. Preparation uses isolated evaluator outputs, not the live document's
reference or auxiliary maps. Table traversal applies row/column scopes and
extracts CWITH using the same format selection as layout. Layout and preparation
share `table_cell_environment` and `local_environment_scope`, replacing the
cell renderer's separate string-keyed save/restore bookkeeping.

The focused wrapper probe exposed a separate occurrence-selection gap. A regular
table cell builds both its rigid display and a lazy representation for automatic
wrapping. Both recorded the same formula ancestor with correct cell mode and
language, while only the actual displayed leaf was selected. Transparent macros
need not contribute a box carrying their own boundary, so walking box ancestors
alone could not choose the matching formula occurrence. The source occurrence
now retains its actual enclosing occurrence. Ancestor queries follow that chain
from the selected leaf and accept a parent only if its boundary is still admitted
at the requested current source address. This retains wrapping support and does
not merge distinct contexts or guess an occurrence by source-path uniqueness.

The lineage contains boundary handles and parent links only. Retained child
logs must not keep old parent source trees and macro argument environments alive.
Links are allocated lazily when an occurrence actually records a child.

The complete production target built with `-j16`
(`/tmp/athena-native-wrapping-final-build.log`). The temporary production-library
probe passed at 18:16 on 2026-10-28
(`/tmp/athena-native-wrapping-probe.log`): WITH and inactive-template wrapping,
mode rejection before mutation, preservation of an unrendered argument's
selection, formatted table-cell preparation and comparison with actual layout.
The ambiguous formula ancestor now returns text/french while its selected leaf
returns math/french, matching the actual scopes. Audited wrapping made zero
unprepared environment queries. No permanent tests or Notes changes.

### Compound Insertion Acceptance

Compound insertion requires an admitted invocation context instead of using the
legacy getter when it is missing. Its private replacement form prepares arity,
native enunciation conversion and any table argument before consuming a hybrid
input. It returns acceptance to hybrid activation, which leaves the draft and
selection intact on refusal and does not finish the hybrid session. This closes
the earlier destructive ordering, not the missing-context continuation itself.
Public Scheme/editor binding signatures remain unchanged.

The general wrapper preparation also replaces the intermediate direct-transfer
implementations of balloon, marginal-note and geometry wrapping described above.
Those commands now retain the selection until `var_insert_tree` has prepared and
validated its destination, preserving mode conversion as well as source identity.
The slideshow-specific clipboard contract remains separate. The unused temporary
`exec_macro_argument` evaluator entry was removed after argument preparation
became a specialization of `prepare_tree_position`. Block-macro inspection also
receives its invocation checkpoint explicitly and no longer calls a legacy getter.

The combined preparation/compound-consumer changes built successfully with
`cmake --build build_qt6 --target ATHENA.bin -j16`
(`/tmp/athena-native-preparation-build.log`, 2026-10-28). The final forwarding and
acceptance-order edits were checked from their control flow, without another
test run. The earlier focused wrapper probe covers the evaluator/occurrence
transition, not a claim that all remaining consumers are migrated. No executable
deployment or commit was performed; the overall cutover goal remains active.

### Table And Paste Query Cutover

Native paste plans now retain the actual table source objects and their entry
contexts, including enclosing tables. Applying the plan identifies a retained
destination by object identity, not its old path. Table/TFORMAT source survives
in-place row, column and cell edits; a tree pointer observer was not used because
its detach behavior deliberately redirects it to a neighboring node. Prospective
wrapper tables receive their own separately prepared context after insertion.
No live observer is retained by the opaque plan. This handles path relocation,
not arbitrary changes to an insertion owner's environment.

Non-graphics native and external paste require a prepared target mode before
modifying source. Table environment reads likewise no longer enter the legacy
getter. A table command's context scope checks readiness before changing its
owner state or source. Missing contexts are explicit unavailable operations;
the automatic pending-command continuation is not implemented by this change.

Compound table commands share that existing owner-local scope through
`with_table_context`: width/height mode coupling, padding/border batches, cell
spans and multi-property formatting of a selection. Each scope remains specific
to one table, not a generic pre-edit environment used for unrelated new fields.
Native toolbar span actions use the same high-level span command. There are no
new Scheme bindings; the callback interface is C++-only.

The first compound-border probe exposed an additional policy read: restoring
the cell selection called `in_source()` after the format mutation. Its table
entry was still available, but the selected cursor's navigation policy was not.
`with_table_context` now also retains the invocation cursor's selection context.
Selection normalization and correction use its mode and contracts until the
bounded command ends, including exceptional exits. This is separate from the
table entry: an individual cell can have a different mode. Clipboard payload
mode/language and new-field evaluation do not borrow this navigation context.

The production target built with `-j16`
(`/tmp/athena-native-table-selection-build.log`). The focused production-library
probe passed at 19:01 on 2026-10-28
(`/tmp/athena-native-table-cutover-probe.log`): prepared paste and table relocation,
compound dimensions/padding/borders/spans, rectangular cuts, nested table entry
inheritance and repeated argument displays. The audited operations made zero
unprepared environment queries. No permanent tests, Notes changes, executable
deployment or commit were performed. General unrendered-source preparation,
pending-command continuation, source-identity inference policy and bounded
analysis capture/admission remain open; this is not completion of the cutover.

### Analysis Demand Ownership

The scheduler distinguishes unfinished current demand from an obsolete batch
still in flight. Returning to a retained context satisfies current demand
immediately. An older completion is cached for reuse, but cannot overwrite that
ready result or wake its already satisfied continuation. A still-pending newer
request can continue receiving dependency-validated components from an older
result; the existing owner-side receipt checks remain in force.

A temporary concurrency probe controlled both workers with gates, queued A,
selected cached B, then released A before polling. It verified that B remained
ready, no obsolete wake was delivered and A remained reusable afterward. The
probe includes the production service implementation, without adding a runtime
test hook or changing the executor. It does not verify bounded capture/admission
or editor pending-command continuation.

### Clipboard Occurrence Ownership

Copy no longer moves the caret to the selection's endpoints and back. That
source-only repositioning could lose the selected display when a macro shows
one argument multiple times under different mode/language bindings. Selection
extraction and clipboard metadata retain the original display selection.
Clipboard publication obtains mode and language from one prepared selection
context, and only clears selection gestures after publication succeeds. The
selection environment getter no longer uses the old positional fallback;
missing preparation remains an explicit unavailable operation. Wrapper transfer
uses its already captured selection context instead of looking it up again.

The focused copy probe then exposed a separate pending-inference consumer:
normal selection correction expanded the displayed substring `onte` into its
whole `copy-twice(content)` call. Its mode/language were not the first incorrect
transition. The editor now admits original selection endpoints against the
current physical occurrence and passes that evidence into structural selection
correction. At those exact endpoints, `is_mapped_cursor` applies the existing
native/authored restrictions without requiring optional inferred accessibility.
Structural adjustment does not grant mapped access to a new endpoint. Source
ranges without matching occurrence evidence retain normal navigation policy.

The combined service/clipboard/endpoint changes built successfully with `-j16`
(`/tmp/athena-native-selection-endpoints-build.log`). The production-library copy
probe passed at 19:30 on 2026-10-28
(`/tmp/athena-copy-occurrence-final-probe.log`): both English and French displays
of the same source argument copied only `onte`, retained their own language and
left the caret and physical occurrence unchanged, with zero legacy environment
queries. The probe selects recorded source occurrences, including those carried
by merged text spans rather than top-level box markers. The earlier failing
log (`/tmp/athena-copy-occurrence-probe.log`) records the first incorrect range
promotion. No production diagnostics or permanent tests were added; Notes and
the deployed executable were untouched.

### Retained Analysis Inputs And Pending Hybrid Activation

Analysis capture accepts an owner-local lookup for already frozen variable
values. Cursor preparation supplies the checkpoint it has just restored;
`style_init_env` and `update` refresh derived evaluator state without changing
those bindings. Changed non-definition values can therefore share an existing
checkpoint override instead of freezing the same tree again. The lookup is
used synchronously and never retained by the service or sent to its workers.
Definitions still share their binding payload as before. Cold baseline values,
initial definition capture and dependency admission are not made bounded by
this change.

Hybrid activation now retains its pre-mutation intent when there is no prepared
invocation context. The editor owns the command, source epoch, cursor/selection
and request token. Layout only checks readiness and queues an ID-only native
continuation; source mutation runs in a separate BufferActor editing transaction.
Repeated readiness checks queue it once. A newer request supersedes the old
token, further keyboard input cancels it, and both readiness and execution
reject a changed source epoch, caret or selection. Closing the view drops the
owner and the queued callback finds no editor. No source tree or editor pointer
is captured in the shared continuation registry.

This is deliberately not a retry wrapper around partially executed commands.
It handles a missing invocation checkpoint at hybrid activation entry. Compound
table preparation failure, optional inferred field policy and other command
families still require their own lifecycle integration. The ordinary compound
insertion refusal remains pre-mutation and has not become a generic replay.

The production target built with `-j16`
(`/tmp/athena-native-pending-activation-build.log`). A temporary probe using a
real buffer actor and editor view passed at 19:56 on 2026-10-28
(`/tmp/athena-pending-hybrid-probe.log`): readiness does not mutate source,
activation occurs once in the next actor transaction, and later source edits,
caret moves and selection changes cancel queued activation. The probe invokes
the same post-layout readiness hook directly to control queue ordering. The
frozen-input sharing was checked from its capture/restore data flow, not with a
separate regression suite. No permanent tests, Notes changes, executable
deployment or commit were performed. The full DRD cutover goal remains open.

### Strict Queries And Native Shortcut Inputs

The worktree has removed `typeset_exec_until`, `restricted_exec`, its unscoped
`define_style_macros` scan, and the boundaryless `cur` path cache. Environment
getters and interactive evaluation require an admitted checkpoint; they report
missing preparation rather than evaluating an approximate source prefix. This
exposes remaining caller obligations, not malformed documents. Hidden-source
search/manual spelling, source-authoring discovery, IME rollback, graphics and
other compound consumers still need their own explicit preparation lifecycles.
The strict intermediate binary is not deployed.

Native LaTeX invocation captures auxiliary-prefix values before removing the
hybrid draft. Native compound insertion receives the invocation checkpoint
explicitly. Shortcut chains retain their original insertion checkpoint before
`mark_cancel` removes the previous visible output, rather than treating the
generated delimiter's interior as the insertion site. The retained checkpoint
is cleared when the shortcut ends; native command preparation happens before
cancellation. It is used for compound insertion, not as a generic environment
fallback for every action or a replacement for prospective field evaluation.

The production build and temporary production-library probe passed
(`/tmp/athena-native-shortcut-input-build.log`,
`/tmp/athena-native-shortcut-probe.log`). The latter verified macro invocation,
a ToC prefix assigned beyond ten preceding paragraphs, and cancellation of an
opening delimiter followed by matrix construction. All completed without an
unprepared environment query. No permanent tests or Notes changes were made.

Source identity classification now selects the immutable native baseline for
remaining built-in primitives that do not yet have named native fields. An
editor's mutable contract scope cannot reclassify their physical source fields.
Named native fields and explicit source-role declarations retain precedence.
Custom macro fields still depend on resolved contracts; their pending and
lexical-source ownership problem is not solved by this primitive-only change.

Horizontal cursor movement captures the starting source mode with its starting
contract before moving. The skipped-child and old-parent border checks no
longer read the destination mode to classify the node being exited. Destination
accessibility continues to use the destination's own context. These two narrow
ownership changes were checked from their source flow without adding tests.
They built successfully with `-j16`
(`/tmp/athena-native-source-role-build.log`). The executable remains undeployed
until the remaining strict-query consumers have explicit readiness handling.

### IME Rollback Preparation

IME replacement and committed text share `cancel_pre_edit`. Cancelling the
pre-edit undo marker restores source but invalidates its execution base. The
input owner now calls normal `apply_changes` after that restoration, before
native keyboard mode selection or Scheme `kbd-insert` dispatch. No getter
restores the old draft context, performs evaluation or schedules this work.
The helper does nothing when there is no pre-edit marker.

A temporary real-BufferActor probe reproduces the old failure with normal
pre-dispatch preparation: `pre-edit:a` succeeds; `pre-edit:ab` throws while
reading `mode` after rollback (`/tmp/athena-ime-before.log`). The initial fixture
omitted pre-dispatch `apply_changes`; it was corrected before using that result
to validate the production change.

The production target built with `-j16`
(`/tmp/athena-native-ime-context-build.log`). The real-actor probe then passed
at 22:24 on 2026-10-28 (`/tmp/athena-ime-verified.log`), covering replacement of
an existing draft, text-input commit and ordinary-key commit in both text and
math modes. An intermediate probe stalled because its unconsumed UI-effect
queue filled; an attached stack identified `actor_command_transport::acquire`,
not evaluator recursion. The temporary fixture now drains effects between
commands; no production transport changes were made. This checks source and
context transitions, not Qt composition-event timing or GUI rendering latency.
No permanent tests were added, Notes were untouched, and the binary remains
undeployed while the broader cutover is incomplete.

### Source-Authoring Signatures

Source-mode macro discovery no longer writes unevaluated document assignments
into the runtime environment. The editor prepares a separate signature index
during cursor-context publication, only when mode is `src` and preamble is not
`true`. It descends through top-level DOCUMENT/CONCAT containers, records literal
ASSIGN names, and uses `signature_of` for kind and formal parameters. Macro
bodies, local bindings and conditional expressions are not executed or scanned
for additional assignments. Value assignments also participate in shadowing.

The owner maintains per-paragraph signature maps and ordered definition origins.
Existing source mutation notifications dirty affected paragraphs; structural
changes to the root invalidate the index. Removing a later definition restores
the earlier definition of the same name. Getters read the prepared catalogue;
they never scan source or run inference. Commands use it with their admitted
source-mode invocation context. Its update is independent of runtime contracts.

Completion, hybrid argument preparation/activation and compound insertion use
these source signatures in authoring mode. Constructing a source macro creates
syntax fields, not a guessed executed table/macro environment. Fixed and
variadic signatures control argument validation; ordinary text/math editing
continues to consume actual evaluated bindings. This is discovery support, not
a replacement source-identity schema or an inference result.

The hybrid name field forces text mode for display. Both publication and
completion select the outer invocation occurrence, as macro insertion does;
using the name-field checkpoint would silently omit the source catalogue.

The production target built with `-j16`
(`/tmp/athena-source-signature-build.log`). The temporary native editor probe
passed at 22:51 on 2026-10-28 (`/tmp/athena-source-signature-probe.log`): definitions
after the invocation are discoverable without appearing in its effective
environment; nested macro-body assignments are excluded; removing a shadowing
definition restores the earlier signature; completion, fixed/variadic argument
activation and text-mode isolation work. The initial fixture placed definitions
before the invocation and incorrectly expected them to be unexecuted; moving
them after it corrected that assertion. The corrected probe exposed the actual
name-field mode issue before the outer-context change. This probe uses a
detached editor, not a GUI or worker-scheduling stress test. No permanent tests,
Notes changes, deployment or commit were performed.

At this checkpoint, cutover work still included custom source-identity role ownership/pending
policy, bounded analysis input capture and admission, and supported consumers
without an admitted occurrence. Old interactive search and manual spell entry
points were unchanged; their explicit preparation is implemented in the later
"Explicit Hidden-Source Search Preparation" section below;
the shipped native search bar and live spell checking are separate consumers.

### Exact-Demand Result Admission

The analysis service now publishes and notifies only when a finished task
matches its currently requested immutable name/symbol context. Superseded
results remain in the bounded cache and the detached engine can reuse their
work for the next demand, but they no longer wake the editor to prune a stale
candidate. A new request also withdraws an unconsumed previous completion before
checking the cache. Returning to a cached context is still immediately ready.

The owner rechecks both context identities after capture and before admission,
covering a binding change between worker completion and owner publication.
Exact results replace the persistent projection/provenance/receipt roots without
walking the candidate dependency graph on the owner. A superseded candidate,
including its error, is not applied to the current context. Until the matching
result arrives, existing published facts still use dependency-specific
retirement: unrelated existing facts are not cleared wholesale. This deliberately
stops partial publication of a never-admitted stale candidate, not incremental
reuse by the worker or retention of already-valid facts.

Large-input capture and retirement of already-published dependent closures are
still owner-side and not yet time-sliced. Removing candidate-graph traversal
from admission does not establish that those remaining paths are bounded.

Production built with `-j16` (`/tmp/athena-native-exact-admission-build.log`).
Temporary probes passed at 23:09 on 2026-10-28:
`/tmp/athena-analysis-exact-demand-probe.log` gates the two real analysis workers
to verify that both cached and still-pending newer demands reject superseded
publication/wakeups, then checks exact completion and earlier-context cache
reuse. `/tmp/athena-exact-admission-probe.log` changes a binding after completion
but before publication, checks atomic cycle retirement and independent existing
facts, then exact/unchanged admission and mixed inherited inference. These are
concurrency/admission probes, not a claim of bounded input capture or a GUI
latency measurement. No permanent tests, Notes changes, deployment or commit.

Cache eviction also transfers the retired proposal out of the global scheduler
critical section before releasing its detached graph. Worker completion wakes
the owner before its local retired pointer is destroyed; explicit offline
preparation likewise releases after unlocking. This addresses eviction lifetime,
not every possible large-object destruction path in analysis preparation.
Request replacement similarly moves the previous context, callback and unread
completion into local retirement holders declared before the mutex guard. Early
cache-hit returns therefore unlock before destroying any of those captures.
These lifetime-only changes were checked from RAII destruction order without
adding more tests.

The final production build also passed with `-j16`
(`/tmp/athena-analysis-request-retirement-build.log`, after the intermediate
`/tmp/athena-analysis-cache-retirement-build.log`). The deterministic probes
above precede the lifetime-only changes; they were not rerun ceremonially.
The full cutover remains active and the executable is not deployed.

### Retained Analysis-Ready Delivery

The completion callback previously created a native continuation and discarded
it when `try_submit_to` failed. A busy producer gate or full command ring could
therefore leave a quiet editor pending despite its completed analysis result.

Workers now signal `buffer_actor::notify_contracts_ready(actor, view)`. The
actor retains a coalesced set of view IDs under its state lock, independently of
command slots, and wakes the existing command wait condition. The transport
distinguishes command, wakeup and close; a command also consumes a pending wake,
so the actor drains readiness after either kind of event. Commands retain FIFO
ordering and readiness does not consume a mailbox ticket or advance command
completion counters. Each synthetic dispatch enters the usual owner Scheme
execution context, prepares the current cursor and invalidates focus/cursor
presentation. The analysis owner still validates the exact input context.

Registry leases protect actor lifetime during notification. Shutdown rejects
new signals and discards retained readiness; a removed view is looked up on the
owner and ignored. Dispatch now also reacquires the editor after a command,
because `destroy_view` can invalidate the pre-dispatch editor pointer before
coalesced-command cleanup. There are no editor pointers or continuation tokens
in the worker callback.

This is completion delivery, not a generic command replay system. The separate
pending hybrid activation must not become an out-of-band source edit that
overtakes already queued user input; its retained FIFO policy is described below.

Production built with `-j16` after both changes
(`/tmp/athena-contract-ready-final-build.log`). The temporary probe
`/tmp/athena-contract-ready-probe.log` passed at 23:50 on 2026-10-28:
idle transport wake, command priority with a full ring, wake coalescing and
close; then a real BufferActor with 127 queued commands and 1000 notifications
delivered one readiness dispatch before the next queued command, with idle
delivery and closed-view/closed-owner handling also checked. Its view was kept
unprepared to isolate delivery from independent inference completions. It does
not replace the earlier exact-context admission checks or measure UI latency.
No permanent tests, Notes changes, deployment or commit.

### Retained FIFO Hybrid Activation

Layout readiness no longer stores a temporary callback and abandons its
submission when the mailbox is full. `queue_hybrid_activation` retains the
latest numeric request token per view in the actor-owned implementation. After
the current command returns its transport slot, the actor attempts to enqueue
`resume_hybrid_activation` through the ordinary nonblocking producer gate.
Successful submission removes the retained intent; otherwise it remains for the
next consumed command. A full ring necessarily has work to consume, and a
contending producer either publishes work or closes the actor. No timer, spin
loop or additional layout pass is required to retry mailbox admission.

The resume command remains FIFO with already queued user work. The existing
editor request checks its token, source epoch, caret and selection before
starting a source transaction. Closing a view erases its retained request;
already queued tokens resolve the view again and do nothing if it is gone.
Readiness preparation itself still cannot mutate source. This change covers
delivery after readiness, not unresolved inferred child policies or prospective
field preparation.

Production built with `-j16` (`/tmp/athena-hybrid-fifo-build.log`). The temporary
real-actor probe `/tmp/athena-hybrid-fifo-probe.log` passed at 00:04 on
2026-10-29: with 127 commands already queued, repeated readiness preparation
caused one activation after all earlier commands; a queued caret movement
cancelled activation in the second case. The probe used synthetic input only.
No permanent tests, Notes changes, deployment or commit.

### Native Graphics Grid Command Plans

The live Ctrl+2 through Ctrl+0 bindings call `graphics-set-grid-aspect`.
Previously that Scheme command changed visual properties, then synchronized
the logical grid through helpers that queried the now-invalidated environment.
Other grid setters shared the same mutable Scheme globals, including a link
flag that was independent of the current document's authored setting.

`native_graphics_grid.cpp` now captures the admitted graphics-entry context,
authored properties and initial aspect defaults before planning changes. The
planner computes linked visual/logical updates entirely from that captured
state. Applying the plan does not query an environment; property writes share
an editing transaction. Reset preserves the existing upward property-removal
semantics. Toggle state retains saved grid parameters, and hidden subdivisions
remain stored separately from the visible aspect.

The current canvas's `gr-as-visual-grid` is the link authority. Notebook grid
setup computes its unit, subdivisions and colors in the same plan instead of
requiring delayed callbacks. The Scheme module retains public command names,
prompts and checkmark metadata, but no current-grid globals or mutation-time
environment-fetch helpers. Both native entry points are declared in
`src/Scheme/Glue/native.xml`; CMake generates their wrappers and registration.
Grid rendering and snapping implementations are unchanged.

Production built with `-j16` after correcting the native point label spelling
(`/tmp/athena-native-grid-plan-build-final.log`). The temporary real-actor
probe `/tmp/athena-native-grid-plan-probe.log` passed at 00:36 on 2026-10-29.
It loaded the actual Scheme module and generated bindings, checked linked and
unlinked grid/aspect changes, hidden subdivision retention during color edits,
toggle restoration, synchronous notebook setup and reset. A second native
command within an invalidated context was rejected before any source change.
These checks used a synthetic document, not Notes; they do not claim GUI
gesture or rendering coverage.

No permanent tests, deployment or commit. The other strict-context consumer
and identity-ownership gaps listed in the design entry point remain open.

### Cooperative Cancellation Within Detached Analysis

The analysis service already cancels an owner when it closes, but the core
previously inspected that token mostly between definitions/worklist items.
One large macro's recursive argument/with-like/dependency traversal, XMACRO
minimum-arity walk or dependency fan-out could continue after closure. The
service's input diff, result projection and complete dependency-index pass
also had unchecked loops.

Those traversals now consume the existing cancellation token. It is passed
explicitly through recursive analysis helpers, not stored as an ambient or
cross-owner flag. Cancelled work continues to discard its partial detached
engine on the worker and cannot publish a partial result.

Dependency indexing still uses Boost Graph's `strong_components`. Its
component property map now checks cancellation on access. Boost's Tarjan
implementation uses that map during vertex discovery, edge inspection and
component draining, so cancellation is observed inside the algorithm rather
than only before/after the call. The graph-building and receipt-indexing loops
also check the token. No SCC algorithm or scheduling subsystem was added.

Production built with `-j16`
(`/tmp/athena-analysis-cancellation-build.log`). A temporary probe compiled
the actual dependency-index implementation and verified recursive grouping,
downstream component relationships and external reads. It then set the token
on the seventeenth underlying component-map access and verified that the next
access stopped active Boost traversal, without sleep-based thread timing
(`/tmp/athena-analysis-cancellation-probe.log`, 2026-10-29).
The recursive token forwarding was checked from its call graph; no permanent
test suite was added or run.

This is cooperative owner-close cancellation, not per-request preemption or
bounded time slices. Input tree thaw/copy, large container allocation/destruction,
owner-side input capture and stale-fact admission/retirement still need their
separate budget treatment. Superseded contexts retain their existing cache and
exact-demand publication policy. No Notes changes, deployment or commit.

### Checkpoint Transfer Without Duplicate Capture

`evaluation_environment_checkpoint::restore` now gives the recorder the restored
checkpoint directly. Its persistent override map and baseline are retained;
previously recorded boundaries survive, and subsequent writes branch the
recorder's current overlay. The evaluator still materializes its own variable
table. This avoids the second full table capture previously done by `rebase()`
without introducing in-place mutation of a possibly shared legacy hashmap.

Field-environment detachment now visits baseline entries not shadowed by an
override, then retains the present frozen overrides themselves. Deleted entries
are omitted; present UNINIT stays distinct from absence. It no longer creates a
full materialized table or thaws and refreezes each override. The receiver still
thaws into its own mutable trees. Baseline hashmap copies share owner-local tree
handles, not immutable cross-owner payloads; their entry copying must not be
described as a deep copy of every tree.

Production built with `-j16` (`/tmp/athena-checkpoint-transfer-build.log`).
The temporary runtime probe `/tmp/athena-checkpoint-transfer-probe.log` passed
at 01:11 on 2026-10-29: retained boundaries, writes after restoring an overlay,
transaction rollback, overridden/deleted/present-UNINIT values, and mutation
isolation after detached transfer. No permanent tests or Notes modifications.
The strict-context cutover remains undeployed; full baseline capture, unprepared
consumers and source-identity policy are not resolved by this optimization.

### Resumable Detached Analysis Batches

The monolithic core `apply` path is replaced by `begin`/`resume`. Its existing
input installation, dependency closure, signature initialization, reset,
fixed-point evaluation and result construction retain their progress between
worker admissions. Their order and inference rules are unchanged. The detached
engine similarly retains fact updates, projections and provenance until a
complete proposal is available; its committed context changes only then.

The existing two-worker executor now runs soft four-millisecond slices. An
unfinished owner rejoins the FIFO behind waiting work, with one active batch
and one coalesced latest request. A failed queue admission does not mark a new
batch running. Superseded active batches still complete into the retained cache;
only the currently requested complete context may publish or notify. Closing
an owner continues to cancel its private engine. Offline preparation drives the
same stages without a scheduling deadline, not a second analysis algorithm.

The budget is checked between resumable work units, not inside every operation.
Input diff, one recursive definition/large dependency fan-out, tree thaw/freeze,
Boost SCC/index construction and bulk allocation/destruction can exceed a
slice. This removes whole-batch worker monopolization, not every remaining
large-input latency source or owner-side admission/capture cost.

Production built with `-j16` after the queue-admission adjustment
(`/tmp/athena-analysis-slicing-build-final.log`). A temporary runtime probe
compiled the actual service implementation and forced a pause after every
resumable step, comparing complete contracts, read dependencies and recursive
groups with continuous execution across initial, replaced, removed and explicitly
declared definitions. It also checked cancellation before commit, queued
supersession, a competing owner, closure before execution and reuse of the
superseded complete result. The final run passed at 01:28 on 2026-10-29
(`/tmp/athena-analysis-slicing-probe-final.log`). This is not a measured latency
bound or a GUI interaction check. No permanent tests, Notes edits, deployment
or commit; the other strict-context cutover gaps remain open.

### Graphics Style Transfer Preparation

The Shift+Return style-sampling command previously wrote canvas properties one
at a time while still resolving later defaults. With no existing WITH wrapper,
the first write changed the source at the retained canvas path; subsequent
iterations used that stale path. A missing cursor checkpoint could also fall
through to an environment getter after some properties were already written.

Sampling now computes the complete property list from the target object and
prepared canvas entry, then uses the existing batch setter for one assignment.
Return style application and canvas-property queries also use the canvas entry,
not the cursor environment inside a styled graphical text object. The nullable
prepared-value helper is removed, with no implicit environment-query fallback.
Missing preparation rejects before mutation. The existing UI/source refresh
paths are unchanged; this does not add automatic replay of failed commands.

The temporary real-BufferActor probe exercised canvas defaults distinct from
label-local values, bare-canvas wrapping, sampling/application and rejection
without partial mutation when the canvas context is absent. Its initial
configuration setup bypassed the normal environment invalidation path; adding
that actual preparation step corrected the fixture without a production
fallback. The style checks passed at 01:44 on 2026-10-29
(`/tmp/athena-graphics-style-plan-probe.log`).

That run also exposed a separate post-command lifecycle error: replacing source
could update UI wheel capture by reading PREAMBLE before any layout context
existed. The notification now requires an admitted cursor context and otherwise
retains the prior UI state until the layout/apply command publishes again. It
does not substitute a false/default mode for editing operations. Hybrid input
creation likewise captures source mode before cutting the selected content,
instead of querying the invalidated source after the cut.

Production built with `-j16`
(`/tmp/athena-graphics-style-lifecycle-build.log`). The same temporary probe,
relinked against the completed build with its diagnostics removed, passed at
01:49 on 2026-10-29 without the actor PREAMBLE preparation error
(`/tmp/athena-graphics-style-lifecycle-probe.log`). No separate test was added
for the straightforward hybrid read-before-cut ordering. No permanent tests,
Notes edits, deployment or commit. Hidden-source search/manual spelling,
per-source identity contracts and large-input capture/admission remain open;
this is not completion of the strict-context cutover.

### Detached Analysis Reclamation

Cancellation and release are separate lifecycle operations. An analysis owner
is cancelled on its owning thread before its capture maps, cached scopes,
proposals and private engine are transferred for destruction. Superseding a
queued request or replacing an unconsumed completion likewise no longer makes
the request caller release its last potentially large graph reference.

The existing two-worker scheduler retains these worker-safe objects in a release
queue, with at most one reclamation task admitted at a time. That task releases
objects outside the scheduler lock and checks a four-millisecond soft deadline
between objects. Queue-full admission leaves the retained objects queued; normal
task completion pumps them again. Nested analysis-service destruction can enqueue
more state without reentering a held scheduler mutex. No new thread pool exists.
Waiting work has one scheduler-owned reference. Cancelling/skipping it transfers
that reference to the release queue instead of temporarily locking a weak
reference whose last release could race with owner closure under the mutex.
The executor marks process shutdown before draining its workers; later owners
release synchronously and cannot post to an already destroyed executor.

This is not a bound on one destructor, a replacement for stale-fact validation,
or completion of bounded input capture. Live editors/evaluators and mutable
source trees must never be submitted to this release queue.

Production built with `-j16`
(`/tmp/athena-analysis-release-build-final.log`). A temporary probe compiled the
actual service implementation, occupied both workers and filled their admission
queue before superseding a request, cancelling an owner and submitting release
work. It verified destruction off the caller thread, retained queue-full
releases, current-result delivery and no stale/cancelled notification. A release
deleter reentered `pending()` to check that destruction did not hold the scheduler
mutex. Idle cached-result destruction was checked separately. The run passed at
02:10 on 2026-10-29 (`/tmp/athena-analysis-release-probe.log`). No timing bound or
GUI responsiveness claim follows from this check. No permanent tests, Notes
edits, deployment or commit.

Owner input capture still completes before `publish_ready` can validate old
inference receipts. Yielding inside that capture without a corresponding
admission/pending transition would let validation see only part of the changed
inputs. This work deliberately does not introduce that invalid intermediate
state; capture and stale-fact retirement remain a joint preparation obligation.

### Explicit Hidden-Source Search Preparation (2026-10-29)

Legacy interactive search and manual spelling no longer ask strict environment
getters to reconstruct unrendered candidate positions. A synchronous scan retains
an explicit preparation session: it first uses an admitted candidate or source
ancestor entry, otherwise starts from the document's prepared initial context.
`prepare_tree_position` evaluates the selected source prefix with isolated
reference, auxiliary and attachment outputs. It does not publish a display
occurrence or change the source. An unreachable field is an explicit preparation
failure, not an invented mode or a silently discarded match. Only an unchanged
atomic leaf within the same scan reuses its candidate context. Spelling compares
the candidate's actual mode/language before consulting the dictionary.

This exposed an evaluator omission rather than a search-specific mode rule:
partial evaluation did not enter native MathBlock/MathGrid scopes. Complete
semantic evaluation and both partial-evaluation routes now share block, row and
cell traversal. Row counters, row-label/tag scope, display mode and physical
column are retained when a target is reached, and restored on ordinary exit.
Macro arguments expanded through a native block/grid use those same scopes.
Prospective/source preparation enables native counter semantics in its isolated
environment rather than projecting counters into the live document.

The first production build passed with `-j16`
(`/tmp/athena-source-search-build.log`). A temporary real-actor probe passed at
02:32 on 2026-10-29 (`/tmp/athena-source-search-probe.log`): an unrendered prefix
with fourteen intervening siblings, hidden native block/grid row and column
scopes, native-block macro arguments, isolation of source/reference/auxiliary
outputs, strict getter rejection after inspection, and language-filtered search.
No permanent tests or Notes writes were involved. The check does not establish a
bound on prefix preparation, arbitrary external-effect isolation, inference
readiness or source-wide traversal policy.

The legacy search/spelling stepper and previous/next-compound lookup now also
read each visited source node's entry contract, rather than the caret's ambient
contract. A scan retains the last parent-entry checkpoint for sibling checks;
the session never survives a source edit. Source access retains its explicit
all-fields policy. Normal/hidden access uses the selected local binding and its
authored declarations. This change does not invent accessibility while optional
inference is pending.

The follow-up production build passed with `-j16`
(`/tmp/athena-source-traversal-build.log`). The expanded temporary actor probe
passed at 02:40 (`/tmp/athena-source-traversal-probe.log`), checking two equal
macro definitions under distinct local bindings with opposite declared
accessible fields, both before layout and through actual forward search after
layout. The first inaccessible occurrence was skipped and the second selected.
The earlier mode/scope/isolation checks passed in that same run. No deployment
or commit was performed.

At this checkpoint the native document search bar was a different consumer: `document_search` called
the shared matcher in `tree_search.cpp`, whose request inherits one
`cursor_navigation`/contract scope. Its recursive cascaded matcher currently
does not carry source paths, and its range endpoints call recursive
`start`/`end` cursor correction using that same scope. Merely replacing the
entrypoint's ambient query with the document preamble would not fix local
bindings. Its next migration must preserve source addresses through matching
and endpoint selection, separately from pattern trees and explicit source-only
search. Tree-pointer lookup is not sufficient: one shared source object can
occur under multiple bindings. Hidden-prefix preparation budgeting and custom
source-identity policy also remained open. The structured-search context
selection is addressed in the next section.

### Source-Scoped Structured Search And Borders (2026-10-29)

The native document search bar now passes explicit source-entry preparation to
the shared structured matcher. Cascaded and injective matching, native grid
cell patterns and select-region traversal retain the physical source address,
separately from the pattern tree. Field accessibility is read from that source
entry's immutable contract, not from the caret or a request-wide preamble.

Matching alone was not sufficient: whole-node matches, wildcard spans and
multi-paragraph matches compute endpoints through cursor correction. The
existing cursor algorithms now accept an explicitly supplied source-preparation
operation and retain source addresses when descending, checking borders and
adjusting to neighbouring fields. This reuses the existing rules rather than
implementing a second approximate endpoint algorithm. The original constructor
still consumes just its supplied contract; it does not consult an editor,
schedule work or fall back to environment evaluation. Explicit source access
still exposes physical fields subject to the matcher's native descriptor
exclusions.

The preparation callable is synchronous and owned by the command. A navigation
object using it must not outlive it. Returned aliases retain their checkpoint's
contract without relying on a mutable cursor environment. Atomic children need
no additional node-contract preparation. This does not add a persistent path
cache or identify a source occurrence by its tree pointer. Unprepared search
entries still use the command's explicit isolated source preparation; a hard
bound on that preparation and optional-inference pending policy are not claimed.

Production built with `-j16`
(`/tmp/athena-structured-search-context-build.log`). The temporary probe passed
at 02:58 on 2026-10-29 (`/tmp/athena-structured-search-probe.log`). It used the
same source object twice under different contracts, checked free-text results,
structured-result endpoints, recursive document start/end and explicit source
access. The real BufferActor portion additionally called `document_search` on
two local definitions with opposite field permissions and checked its result
count, selected range and caret position. Earlier hidden-source and native-math
preparation assertions passed in the same run. No permanent tests, Notes edits,
deployment or commit were made.

The next concrete consumer at that point was live spelling: `tree_spell.cpp` used
`current_evaluation_state().contracts` both for accessibility and inferred
mode/language transitions. `edit_live_spell.cpp` supplies document-init values,
not each scanned source position's prepared environment. Its resumable jobs
and four-millisecond soft slice must remain resumable; inserting unrestricted
prefix evaluation into that loop would not complete the migration. Missing
admitted contexts and environment changes need an explicit waiting/invalidation
path without discarding committed spelling marks between slices. Custom source
identity roles, large-input capture/admission and other pending consumers remain
separate cutover obligations.

### Admitted Live Spelling Contexts (2026-10-29)

Live spelling now receives a read-only source-entry provider over the current
typesetter's occurrence and prepared-context indexes. Each scanned leaf reads
its actual mode and language. Parent accessibility uses that parent's prepared
contract; inferred permissions can only be used after the child has an admitted
display context, and authored/native field restrictions remain authoritative.
Neither document-init mode/language nor ambient contract queries are used. The
unused synchronous recursive `spell` entrypoints and their three XML Scheme
exports have been removed; interactive manual spelling already uses explicit
source preparation.

Missing or ambiguous occurrences park that branch without executing a source
prefix. Other branches can finish, and publication retains committed marks
under the unprepared prefixes. A later committed layout retries pending work.
The cache retains source-block receipts: an admitted entry boundary, contract
state, mode and language. New layouts review these in bounded round-robin slices
with viewport priority, so an earlier definition can invalidate an untouched
later block without rescanning unchanged admitted blocks. Ordinary source edits
still rebase committed marks and restart only affected block scans.

The existing four-millisecond slice remains a soft budget shared by receipt
review and scanning, with review limited to half the slice and 64 blocks. ICU
segmentation and dictionary calls are not preemptible; this is not a hard
real-time guarantee. No hidden-prefix execution, new worker or background
mutation of editor state has been introduced.

Production built with `-j16`
(`/tmp/athena-live-spelling-context-build.log`). A temporary real-actor probe
passed at 03:28 on 2026-10-29 (`/tmp/athena-live-spelling-probe.log`). Its native
MathBlock and MathGrid fixtures include the required v3 metadata. Actual layout
contexts excluded mathematical content while checking embedded text, selected
a macro argument's local French dictionary, and respected an explicit authored
denial. Changing an earlier mode binding removed marks from an untouched later
paragraph. A controlled pending-context check also covered sibling progress,
retaining marks below a missing child, retry on a new layout and avoiding
unchanged compound rescans. The runtime confirmed that the retired Scheme
exports are absent.

The existing dictionary/ICU slice tests were adapted to an explicit fixed-context
fixture; the obsolete descriptor-driven math traversal assertion was removed.
That suite was not run, and no permanent tests were added. No Notes writes,
deployment or commit were made. This slice does not complete source-identity
policy, owner-side large-input capture/admission or the remaining compound-edit
preparation lifecycles. The separate ambient queries in source correction,
bracket processing and mathematical statistics still need caller-by-caller
ownership review; they must not be declared migrated by renaming their APIs.

### Invocation-Scoped Argument Editing (2026-10-29)

Parameter insertion, parameter removal, empty-parameter deletion and argument
navigation now select the target invocation's entry context. `find_dynamic`
also reads each ancestor at that ancestor's entry. These operations previously
used the caret field's checkpoint, falling back to the ambient contract scope.
A field may locally shadow the enclosing macro name with different arity and
access rules; the caret checkpoint can therefore be valid and still be the
wrong authority. The command retains its selected invocation context and
source-editing policy across its mutations instead of querying the newly
created or removed field. General macro deletion uses the same entry policy.

Inactive activation selects its outer invocation context and uses the selected
inner argument index, not the inactive wrapper's index zero, when deciding
whether the caret may remain in place. Macro-input argument-count shortcuts do
not consume the draft or borrow ambient descriptors when their invocation is
not prepared. `edit_dynamic.cpp` no longer reads `contract_state.contracts` as
a fallback for a missing checkpoint.

The source preparation used by hidden-source search has been extracted to the
owner's explicit `prepare_source_position` operation and is reused here. It
does not publish a display occurrence, install a getter fallback or create a
persistent path cache. As before, it can execute a cold prefix synchronously;
hard budgeting and arbitrary external-call isolation are not claimed.

Production built with `-j16` (`/tmp/athena-argument-context-build.log`). The
temporary real BufferActor probe passed at 03:43 on 2026-10-29
(`/tmp/athena-argument-context-probe.log`). It first verified that equal-valued
local rebindings actually produced opposing caret/invocation arity contracts,
then checked both repeated-outer/fixed-inner and fixed-outer/repeated-inner
editing. Insertion, removal and empty-argument removal ran consecutively without
layout; the probe checked that the intermediate caret had no admitted context
and that the original argument list was restored. Inactive activation with an
explicitly denied first argument and allowed second argument retained the
second argument's caret offset rather than jumping to its end. No permanent
tests, Notes writes, deployment or commit were made.

At this stage upward structure-deletion recursion still passed one caret scope
through ancestor removal; the following slice replaces that consumer. Implicit
cursor-navigation helpers used during other mutations and source-identity
policy remain separate obligations.

### Ancestor-Scoped Structural Deletion (2026-10-29)

`remove_structure_upwards` now reads custom border policy at the removed
invocation's entry before mutation. Its recursive call prepares the next
ancestor anew instead of retaining the original caret's contract through
multiple removals. Native table, row, cell and format removal does not request
optional inferred border policy. The redundant scoped overload is removed.

Production built with `-j16`
(`/tmp/athena-structural-delete-context-build.log`). A temporary real BufferActor
probe passed at 03:52 on 2026-10-29
(`/tmp/athena-structural-delete-context-probe.log`). It verified opposing entry
and caret border declarations in both directions, retained content after
removal, and deletion through two ancestors without intervening layout. The
second ancestor's entry permits recursive deletion while the original caret
scope forbids it, distinguishing actual preparation from scope reuse.

No permanent tests, Notes writes, deployment or commit were made. Selection
enlargement and formula-root queries were identified as the next consumers of
one caret scope across different ancestors; their migration is not established
by this deletion check.

### Target-Scoped Selection Commands (2026-10-29)

Selection enlargement now obtains border policy at the selected custom node's
entry, including each recursive enlargement. Native nodes do not consult
optional custom border inference. Text enlargement reads mode at the selected
text, rather than assuming the caret is in the same environment. Formula-root
discovery queries each inspected wrapper's entry; the one-child DOCUMENT skip
retains the corresponding wrapper path instead of querying it with a different
node's context. These are explicit commands using existing source preparation,
not new work performed by environment getters.

Production built with `-j16` (`/tmp/athena-selection-context-build.log`). The
temporary structural-deletion probe was extended to inspect raw selection
endpoints under opposing entry/caret border rules. It passed at 03:55 on
2026-10-29 (`/tmp/athena-selection-context-probe.log`), along with the preceding
deletion checks. Formula-root and text-mode changes were inspected statically;
no separate runtime cases or permanent tests were added. No Notes writes,
deployment or commit were made.

The next selection boundary is distinct: `selection_request` in
`tree_select.cpp` recurses into subtrees without advancing its navigation
context. `selection_correct` and `compute_selection(path,path)` in the editor
also still fall back to the ambient contract scope. Their callers include
command-side extraction and repaint-side focus/highlight geometry. A correct
migration must propagate per-node context while keeping repaint read-only and
providing pending behavior for missing occurrences; putting
`prepare_source_position` into every geometry query would execute source during
repaint. The source-only range-set overload also uses a live ambient scope for
its border correction, despite its source-only access policy. This is not
resolved by selection enlargement. Source identity planning, completion
discovery, remaining cursor consumers and bounded owner capture/admission also
remain open.

### Selection Traversal And Source Highlights (2026-10-29)

`selection_request` now advances its navigation context when descending into a
subtree, querying that child's custom border, or resolving a script-adjacent
endpoint in a sibling. A provider supplied to cursor navigation consequently
selects the actual node entry rather than being lost during recursive selection
adjustment. Mapped endpoint permission remains attached to the original root
endpoints; child adjustment does not invent additional admitted hits.

Source-only correction no longer applies custom inner-border promotion.
Range-set geometry, used by selection/search/diff/spelling highlights, now uses
the immutable native baseline instead of the current evaluator scope. It
neither evaluates source nor needs a live caret checkpoint. The unused
`compute_selection(path,path)` overload with an ambient fallback was removed
from both editor interfaces and implementation; callers already use the
range-set form or the explicit-navigation implementation.

Production built with `-j16` (`/tmp/athena-selection-traversal-build.log`). The
temporary real-actor probe passed at 04:09 on 2026-10-29
(`/tmp/athena-selection-traversal-probe.log`): correction visited the selected
macro's prepared entry and respected opposing local border rules; source-only
correction did not escape the selected macro, and the native-baseline highlight
path retained its exact source endpoints. An initial probe assertion incorrectly
required exact macro border endpoints even with an explicit custom inner-border
navigation scope. The printed paths showed normalization to content endpoints
inside that same macro, not promotion to its parent. Only that probe expectation
was corrected; no additional production patch was made for the fixture failure.
No permanent tests, Notes writes, deployment or commit were made.

At that stage the normal editor `selection_correct(path,path,...)` still supplied
a single caret scope with an ambient fallback. Its replacement must distinguish pending
read-only use from explicit compound command preparation. In particular,
`cursor_visible`, focus rectangle calculation, and
`editor_command_state_snapshot` indirectly request corrected selections; they
must not trigger source-prefix execution. The existing `with_selection_context`
scope currently retains only one checkpoint for table commands and is not a
per-source provider. A second unused ambient entry remains in the tree layer:
`selection_correct(tree,...)` has only one test caller in
`utf8_editor_test.cpp`; remove it and make that existing assertion use explicit
native navigation with the next selection batch. Do not run the broad suite
merely to adapt that call.

### Read-Only Selection Admission (2026-10-29)

Normal selection correction now supplies each visited node's admitted entry
through the navigation provider. Missing context returns pending, leaves output
endpoints untouched and never executes source. Atomic leaves do not request
unneeded contract contexts. The retained table-command checkpoint determines
source-editing policy only; it is not substituted for missing entries after an
edit. Source-mode correction and whole MathGrid boundary comparison use explicit
native navigation.

`selection_try_get` exposes readiness to menu snapshots and cursor scrolling;
focus rectangle selection correction also skips an unprepared result. Definite
selection consumers keep the existing void API and receive an explicit
awaiting-layout error instead of invented bounds. Table-range bounds no longer
enumerate spans that the caller discarded. The unused ambient tree-selection
overload was deleted, and its existing test call was adapted to explicit
navigation without running or expanding that suite.

Production built with `-j16` (`/tmp/athena-selection-pending-build.log`). The
temporary real-actor probe passed at 04:24 on 2026-10-29
(`/tmp/athena-selection-pending-probe.log`). It covered actual editor selection
reads under opposing entry/caret rules, an admitted caret with a non-displayed
selected branch, invalid menu publication for that pending selection, and source
mutation inside a retained command scope. Missing entries did not borrow the
checkpoint, publish partial bounds or silently satisfy a definite-boundary
request. After layout, the subsequent ancestor deletion still succeeded.

No permanent tests, Notes writes, deployment or commit were made. This does not
add automatic command replay. Focus rectangle policy and cursor hit/navigation
still have independent ambient fallbacks. Source copying also retains implicit
cursor-boundary helpers; removal of a named ambient query is not proof that
those helpers have the correct source authority. Completion discovery, source
identity contracts and bounded analysis capture/admission remain open.

### Read-Only Focus Admission (2026-10-29)

`focus_try_get` now reports missing selection/entry contexts to command and
toolbar snapshots. The definite `focus_get` API reports awaiting layout rather
than borrowing the ambient scope. Toolbar hidden-field visibility reads the
focused invocation's entry; declarations inside its argument no longer describe
the enclosing invocation.

Selection correction and focus outlines share `with_admitted_navigation`, a
read-only provider rooted at the current buffer. Focus border/access checks
select each visited entry, and geometry traversal uses buffer-relative paths
instead of applying that provider to the entire editor tree. The focus path is
resolved once before recursive geometry. Rectangles are published together only
after successful preparation; pending traversal does not leave partial frames.
Table/cell geometry is retained. Source editing uses the immutable native
baseline and does not request inference or evaluate a prefix.

Production built with `-j16` (`/tmp/athena-focus-context-build.log`). The temporary
real-actor probe passed at 04:40 on 2026-10-29
(`/tmp/athena-focus-context-probe.log`). Its new case gives a macro accessible
fields at its entry and the opposite declaration inside its argument. Focus
lookup selected that macro and toolbar visibility followed the entry. A
non-displayed selected branch with an otherwise admitted caret left both focus
lookup and toolbar publication pending. This is context/actor evidence, not a
claim of visual GUI verification. No permanent tests, Notes writes, deployment
or commit were made.

The repaint call `selection_get(selection&)` was also inspected. Unlike the
path-endpoint overload, it calls native-baseline source-range geometry and does
not throw an awaiting-layout error. It needed no speculative pending wrapper.

Physical hits and keyboard navigation still retain separate ambient fallbacks
in `edit_cursor.cpp`. They must not be switched blindly to the focus provider:
`edit_typeset_rep::cursor_context_at(position, selected)` currently resolves
ancestor tokens from `tp`, while a physical hit can belong to another displayed
occurrence. `find_source_occurrence` selects an exact token or a unique source
candidate, but does not filter descendants by an ancestor token. The next
cursor migration needs explicit hit-anchored occurrence selection and pending
propagation through movement, not a false/inaccessible result that triggers a
fallback jump. Completion discovery, source identity contracts and bounded
analysis capture/admission remain unfinished.

### Physical Hit Admission (2026-10-29)

`display_context_at` selects an entry from the actual hit's box ancestry and
source occurrence. `occurrence_context_at` takes the anchor source explicitly;
the ordinary two-argument cursor query retains its current-cursor meaning.
Physical hits no longer use `tp` as the anchor for a different displayed
instance. Current-cursor queries reuse the display resolver when a retained
display path is available.

`with_admitted_navigation` can carry that display anchor into each visited
source node. The physical `tree_path` path now uses this provider rooted at
`rp`, not a single leaf contract or `contract_state.contracts`. Its cursor
correction uses the same buffer-relative root. This matters when an invocation
allows its argument but a declaration inside the argument changes that same
macro's field policy.

A nil physical path means no admitted hit. Mouse movement reports readiness to
selection and paste, so pending cannot paste at the previous cursor or install
nil selection bounds. A physical direction-search step distinguishes an actual
edge from pending, and restores its ghost coordinates if a later sample is
unprepared. The caller does not invoke logical edge compensation for a pending
step. The AVD selection adapter reports awaiting layout for such a hit instead
of interpreting it as an out-of-source coordinate.

Source-only region picking uses the immutable native baseline without querying
an execution context. Diagram region dispatch and locus detection therefore do
not depend on field admission. An initial broad pending return in mouse event
dispatch was removed during review: it would also have suppressed scrolling and
active image/table resize completion. The final readiness checks belong to
cursor-consuming operations, not the whole event dispatcher.

Production built with `-j16`
(`/tmp/athena-physical-context-final-build.log`). The final temporary real-actor
probe passed at 05:01 on 2026-10-29
(`/tmp/athena-physical-context-final-probe.log`). It rendered one compound source
twice with different local values, kept the old cursor elsewhere, and checked
each hit's ancestor entry and subsequent retained cursor occurrence. An inner
declaration opposing the invocation's field policy did not reject the hit.
After source invalidation, editable hits returned pending and left the cursor
unchanged, while source-only region picking remained available. The initial
mouse-helper build exposed a missing declaration on the shared editor base;
that declaration was added before the successful final build and run.

No permanent tests, Notes writes, deployment or commit were made. This probe
does not establish all keyboard logical movement or real GUI event behavior.

### Logical Cursor Admission And Native Reveal (2026-10-29)

Horizontal logical compensation now reads each visited invocation's entry,
anchored to the original displayed occurrence. Pending correction restores the
original cursor, affinity, display and ghost position without publishing an
intermediate selection. Resume/accessibility correction and `go_to_here` no
longer fall back to `contract_state.contracts`; the definite accessibility API
reports awaiting layout instead of calling pending content inaccessible.

Explicit hidden-source jumps prepare their own entries and apply native node
group policies for hidden, folded, alternative, unroll and expanded content.
They preserve the exact target position. The unused Scheme `cursor-show-hidden`
and `tree-show-hidden` dispatches are removed. Ordinary read-only admission does
not unfold content.

Mutation-observer correction must run before layout, after every primitive edit.
It uses the immutable native field baseline, not source-prefix execution or
unrestricted source-field access. Instance-specific correction follows after
layout. A temporary real-actor probe passed repeated-occurrence horizontal
movement and all five reveal policies at 05:17
(`/tmp/athena-logical-cursor-probe.log`). Final verification also checks formatted
table boundary correction and explicit source-field positions. The final
production build passed with `-j16`
(`/tmp/athena-logical-cursor-final-build.log`), and the combined temporary
real-actor probe passed at 05:22
(`/tmp/athena-logical-cursor-final-probe.log`). This is actor/context evidence,
not a claim of interactive GUI verification.

Remaining indirect cursor consumers include `go_to_start/end` and paragraph,
document and enclosing-node boundary commands. They call implicit navigation
helpers from compound edits and need command-owned preparation, not cold
evaluation on every mutation. Source identity contracts and bounded analysis
capture/admission remain open. No permanent tests, Notes writes, deployment or
commit were made in this slice.

### Admitted Incremental Text Completion (2026-10-29)

The ambient `find_completions(contract_state.contracts, ...)` traversal is gone.
Completion discovery runs in resumable BufferActor slices over each node's
prepared layout entry. Scans have a cooperative time/node budget; candidate
lookup itself performs no source evaluation or document tokenization. Native
field permissions and authored declarations remain authoritative. A mapped
display entry can supply field evidence while optional inference is unavailable;
absent or ambiguous entries stay pending rather than borrowing the caret scope.

Each paragraph retains its word contributions, occurrence and contract receipt.
Source edits invalidate its partial scan. Layout generations incrementally
review receipts, so earlier binding/declaration changes can retire words from
untouched later paragraphs. Candidate ranking excludes unreviewed contributions
and preserves frequency sorting. Missing branches do not prevent known sibling
words from being indexed, and are retried on a later layout.

Realtime completion reads the actual cursor mode, not the document's initial
mode. Pending requests retain the exact source position, text and prefix; input,
cancellation or cursor changes discard them. Idle/layout passes can finish a
request after its index becomes available. A manual request awaiting preparation
does not report a definitive completion failure. Label/reference completion and
hybrid macro completion retain their separate catalogs.

Production built with `-j16` (`/tmp/athena-completion-context-final-build.log`).
The temporary real-actor probe passed at 05:40 on 2026-10-29
(`/tmp/athena-completion-context-final-probe.log`): opposing local/caret field
policies, declarations inside arguments, forward invalidation and recovery,
unprepared branches, a displayed inferred macro, a deferred manual request,
cursor-change cancellation and local math mode. Two initial fixture assumptions
were corrected from code evidence: WITH restores its bound names only, and text
completion starts at a word end, not inside an existing word. Neither prompted
a compensating production fallback. No permanent tests, Notes writes,
deployment or commit were made. This does not establish interactive popup
latency or a hard per-slice execution-time bound.

### Resumed Baseline And Cooperative Capture (2026-10-30)

The intervening committed work through `5ddc3edeb` completed the lazy
BODY-relative source-role provider for live identity transactions, saved files,
detached documents and Vault operations, including authored preamble state.
The eager `prepare_source_contracts` entry point and obsolete offline
document-wide snapshot cache were removed. Native label bootstrap now lives in
Kernel/Types, contract data in Data/NodeContracts, and environment names in
Typeset/Env; neither production nor tests retains a Data/Drd directory.

Document/paragraph/enclosing-node boundary commands now resolve against
admitted per-node contexts before changing the cursor. Missing context is an
explicit pending-layout failure, not ambient navigation or query-time execution.
Dynamic argument deletion, inactive unwrapping and macro-name exposure retain
the corresponding invocation or parent entry before mutation. Their new target
boundaries use command-local evaluation of that bounded subtree, including
local binding and source-mode transitions. Generic mutation-oriented
`go_to_start/end` and detached insertion endpoints are not globally redirected.

`macro_analysis::request` now materializes at most 128 names or approximately
four milliseconds per owner turn. Pending names and dirty value receipts survive
between turns, with newer assignments/removals merged before continuation.
No evaluator, callback into an evaluator, or raw value table is retained by the
batch. Existing coalesced actor notifications request subsequent slices.

Only a completed resolution map is submitted to the detached service. While
capture is incomplete, publication rejects old results, and inferred components
reading unresolved names (including their dependent closure) are retired.
Unrelated evidence remains usable. Completed maps keep the existing exact-demand
publication check. Explicit offline `synchronize` still finishes its capture
synchronously. Initial name-diff enumeration, one recursive tree freeze and
stale-evidence retirement remain unbounded; this is cooperative materialization,
not completion of the large-input admission design.

Production built with `-j16`
(`/tmp/athena-contract-capture-boundary-build.log`). The temporary capture probe
passed at 06:17 on 2026-10-30 (`/tmp/athena-capture-slices-probe.log`): 1,025
changed inputs, owner continuation, rejection of incomplete-demand publication,
retirement of unresolved dependencies, preservation of unrelated evidence,
and changes/removal/addition during capture. It completed in 16 owner turns.
No permanent test was added for constants or forwarding code.

An initial command-boundary fixture incorrectly combined `unaccessible all`
with `accessible 1` in one declaration. The native declaration decoder rejects
overlapping, conflicting selectors, so its missing field contract was not a
navigation regression. The temporary fixture was corrected to declare the two
fields separately; no production fallback or declaration-rule relaxation was
added for it.

The corrected fixture then exposed a real command-boundary cache bug:
`path_less` compares cursor positions, not subtree addresses. Both comparisons
between the empty root and a child returned false, so `std::map::find` selected
the root checkpoint for every nested node. Direct preparation returned the
correct inner field rule while cached navigation selected the outer field.
The cache now uses strict lexicographic subtree ordering, following the existing
incremental spelling cache, so distinct nested entries are actually prepared.

The final production build passed with `-j16`
(`/tmp/athena-contract-capture-boundary-final-build.log`). The corrected temporary
real-BufferActor probe passed at 06:28 on 2026-10-30
(`/tmp/athena-boundary-context-ordered-probe.log`), covering admitted local node
start/end, opposing invocation/caret rules, consecutive no-layout argument
edits, activation, nested command-field preparation and a descendant source-mode
TFORMAT. Temporary diagnostics were removed. No Notes changes, permanent tests,
deployment or commit were made.

Next concrete consumers include hybrid draft argument navigation, cancellation
and activation endpoints in `edit_dynamic.cpp`, followed by the remaining
generic mutation callers of implicit tree navigation. Cooperatively collecting
input names and retiring large evidence sets remain separate from the input
materialization slices implemented here. The overall cutover goal remains open;
the binary is built in `build_qt6`, not installed over the user's runtime.

### Hybrid And Prospective Template Boundaries (2026-10-30)

Hybrid argument preparation, Tab navigation, selection placement, cancellation
without an undo marker, and activation now compute endpoints from the retained
invocation context. Nested content selects its own local contracts. The name
field and freshly inserted empty arguments use their exact atomic positions;
they do not need document-wide navigation. Cancellation through an undo marker
continues to restore its recorded cursor/selection positions. Non-undo
cancellation retains invocation mode through selection restoration as well as
boundary calculation. Missing invocation context leaves the draft intact.

The command-local boundary implementation now also accepts a detached prospective
tree. WITH/style/active selection wrappers and MathBlock/MathGrid insertion use
that same evaluator-backed preparation before cutting their selection. Native
math blocks validate their selected paragraph and prepare the destination before
consuming a hybrid draft. No second evaluator or separate descriptor authority
was introduced. Paragraph return helpers retain their source-entry context
across insertion instead of reading the mutated editor's ambient contract.

The temporary real-actor check passed at 06:50 on 2026-10-30
(`/tmp/athena-hybrid-boundaries-v3.log`): draft argument navigation, explicit
nested local-field boundaries, activation, cancellation, retained selection
placement and prospective MathBlock/MathGrid insertion. Two fixture assumptions
were corrected without changing production policy: WITH has its own cursor
border, and an unversioned in-memory fixture is not a v3 document. No permanent
test was added.

### Compound Destinations And Selected Ranges (2026-10-30)

The three detached `end(selected)` calls in `make_compound` are removed.
Source-authoring insertion selects the immutable native source policy. Runtime
enunciation/macro/table destinations prepare the actual inserted field, after
any table relocation and cell-local bindings, before finding the selected
content's endpoint. This explicit command preparation can execute a source
prefix; it is not a getter fallback or a promise of bounded insertion latency.

Explicit-path tree insertion now uses the existing structural `go_to_correct`
after mutation instead of running a second correction against ambient contracts.
The caller owns the prospective endpoint; admitted layout subsequently owns
invocation-specific visual correction. Atomic insertion compares its literal
string endpoint without consulting a contract.

Whole-node selection and cut prepare the selected node, not the caret scope.
Selection's retained mode context can be supplied by that command and is restored
afterwards, including on failure. Selection-environment queries use a read-only
admitted boundary; they do not execute source. Structural search and manual
spelling also prepare their source boundaries explicitly. Recursive cuts and
concatenation-pattern search preserve each child's own boundary rather than
canonicalizing it in the parent CONCAT: parent-level left correction can move
the next child's start to its previous sibling and repeat the same cut range.
Atomic cut boundaries use their literal string offsets.

Production built with `-j16`
(`/tmp/athena-selection-boundaries-final-build.log`). The temporary real-actor
check passed at 07:04 on 2026-10-30
(`/tmp/athena-selection-boundaries-final-probe.log`): hybrid navigation,
activation/cancellation, selection placement, MathBlock/MathGrid insertion,
an inserted macro's local field contract opposed to the caller's contract,
and a cross-child cut leaving exactly the two retained text fragments.
The wrapper check follows the actual inserted macro rather than assuming that
cutting a range ending inside another macro removes that entire outer node.
No permanent tests, Notes changes, deployment or commit were made. These checks
do not establish the remaining consumer cutover or interactive latency.

Remaining consumers include generic mutation-oriented `go_to_start/end`, the
implicit endpoint overload of `insert_tree`, native math/format/table commands,
clipboard extraction/identity completeness, and passive heading/ProofTrack/image
geometry. `cursor_execution.cpp` still exposes the thread-context utility facade.
Those callers must be classified by command preparation versus read-only admitted
display versus native structural correction; a global replacement with cold
source evaluation is not acceptable. Large-input name enumeration, individual
freezes and stale-evidence retirement remain separate lifecycle obligations.

The next clipboard boundary is concrete: `selection_compute` still compares
metadata-bearing subtrees to ambient `::start/end` during recursive extraction.
Its source-root stripping and `complete_identified_selection_path` callers do
the same before issuing move tickets. They need one explicit context provider
whose source address advances with recursion, not a descriptor chosen from the
clipboard result. Keep the raw selected tree, identity-completeness decision and
rendered endpoint correction separate when changing that path.

### Clipboard Identity And Command Boundaries (2026-10-30)

`selection_compute` now takes an explicit navigation provider and advances its
source address during recursive extraction. Identical tree handles at different
source paths do not imply identical local contracts. Complete-node metadata and
source-root stripping use the same boundary predicate. Partial format ranges do
not query an unrelated, possibly unlaid-out document tail. Spelling menu readers
now extract literal text from their single atomic source range instead of passing
through structured clipboard extraction.

A temporary real-actor probe exposed a pre-existing identity inconsistency at
07:21: extraction promoted a selected accessible field to an identified
`clipboard-pick(left, right)`, while cut left `clipboard-pick(left, "")` in its
fixed owner slot. Complete-object cut now checks ancestors in extraction order,
excluding the document root, and removes the same identified object. Fixed
content slots remain present but do not retain the moved object's identity.

Generic mutation `go_to_start/end`, adjacent deletion boundaries, table-cut
positioning and drawing-text entry now prepare the target source explicitly.
Default tree insertion prepares its detached prospective endpoint in the
insertion context. Atomic targets use exact offsets. These are command paths,
not permission for a getter to execute source. ProofTrack bounds/paragraph
anchors and selection visibility use an optional admitted-only boundary query.
Image bounds use IMAGE's fixed outer borders, and resize retains its source
entry before mutation invalidates the selection's mode context.

Production built successfully with `-j16`
(`/tmp/athena-clipboard-command-boundaries-build.log`). The temporary real-actor
check passed at 07:31 (`/tmp/athena-clipboard-context-final-probe.log`):
source-path-specific extraction with shared tree handles, partial identity,
an unadmitted document tail, source-root UUID stripping, complete-object cut,
generic start/end against opposing local contracts, default insertion in a
locally rebound field, and a subsequent boundary command without intervening
layout. The fixed owner slot was empty after cut, with no retained moved UUID.
No permanent tests, Notes changes, deployment or commit were made. Existing
selection tests only received the new explicit-provider argument.

Remaining direct ambient navigation includes `edit_math.cpp` endpoints and
detached selection content, heading-range geometry, constructor/observer
structural correction, source-policy Vault search and tree utility facades.
Large-input enumeration, single-value freezing and stale-evidence retirement
also remain open. The overall cutover is not complete.

### Math Boundaries, Screen Projections And Inherited Analysis (2026-10-30)

Native math selection wrappers prepare their prospective body endpoint before
cutting. Script bases, prime continuation and delimiter transitions use explicit
source-entry preparation; empty/atomic slots use literal endpoints. Heading
section ranges now describe complete source nodes and obtain geometry from
current boxes, without navigating an unrelated or unlaid-out document tail.
Constructor/observer correction is native structural navigation, and Vault
source search selects the immutable native source policy explicitly.

The real-actor check exposed two distinct failures:

- A reflow ToC rebuilds the screen document root. Its recorded displayed value
  differed from the actual source, so the root context was unavailable even
  though the caret and selected formula contexts were present. The fold
  projection now retains exact source/display pairs for rebuilt containers and
  ToCs. Lookup verifies both sides through the ordinary occurrence index; it
  does not accept a mismatching tree by path or bypass source admission.
- Initial cooperative analysis capture treated every inherited style input as
  unresolved and retired its existing inferred facts. The displayed `math`
  macro then had zero fields and selection correction expanded a selected
  argument into its whole formula. Analysis contexts now retain their immutable
  input receipts. New owners reuse inherited resolutions and compare both
  current and previously known labels for binding/declaration changes, including
  removal. Changed inputs still retire affected dependency components while
  materialization is pending. No math-specific accessibility exception was added.

Generic focus field names, variable-name detection, hidden-field editing,
parameter names/types and structural applicability now read explicit prepared
contexts. Attached nodes select their own entry; detached proposals use the
caret's prepared context. Missing preparation yields unavailable UI data rather
than borrowing the thread's final environment. Field-name and hidden-field
implementations are shared with the existing explicit-query path. Structured
argument movement uses the parent's prepared policy and command-owned target
boundaries, replacing the remaining native-editor `next/previous_argument` use.

Production built with `-j16`
(`/tmp/athena-native-inherited-inference-final-build.log`). The temporary probe
passed at 12:03 (`/tmp/athena-native-inheritance-and-math-probe.log`): inherited
analysis retention, selective retirement across 1,025 changed inputs, folded
root/ToC admission, selection extraction under opposing local macro contracts,
accent/bracket wrapping, nested scripts and repeated primes without intervening
layout, and heading/ToC geometry after fold and unfold. The pre-fix log
`/tmp/athena-math-heading-access.log` records `math` with zero fields; the final
log records its retained one-field inference and the exact selected inner node.
These are state/ownership checks, not evidence of bounded interactive latency.
No permanent tests, Notes changes, deployment or commit were made.

Remaining utility consumers are the Scheme-facing Data/Tree property/navigation
facades and the TeX export environment lookup. Large-input name enumeration,
single-value freezing, stale-evidence retirement and cold command preparation
remain unbounded. The goal is still active; the successful production build is
not authorization to deploy this incomplete strict-consumer cutover.

### Scheme Source Traversal (2026-10-30)

The Scheme valid-position, word, node, tag and argument movement interfaces now
belong to the active editor. Their XML declarations moved from `basic.xml` to
`editor.xml`; the generated bindings call editor-owned source navigation. A
global root query traverses only the current buffer and returns absolute paths.
An attached subtree uses its exact source entry and returns relative paths.
Detached, foreign, stale or unlaid-out sources do not acquire an arbitrary
environment: the query returns its original position. These passive operations
do not evaluate source or schedule analysis.

Word/node/tag/argument algorithms now live on `cursor_navigation`. Tag boundary,
ancestry and accessibility decisions use each visited node's prepared context,
not a single ambient scope. Their unused global wrappers are removed. The
multiparagraph deletion exit also uses the editor-owned valid-position query.
Start/end helpers and tree-property Scheme interfaces are not migrated by this
change and remain part of the next consumer pass.

The runtime check exposed another incorrect transition: `path_accessible`
interpreted the terminal cursor component as a child index. Text offsets and
outer borders therefore failed a field accessibility check, switching valid
movement to source policy and allowing hidden fields. The terminal component
now ends that check; actual ancestor field restrictions still apply. The
pre-fix log `/tmp/athena-source-navigation-probe.log` records entry into the
wrong field. No macro-name exception or relaxed source admission was added.

Production built with `-j16`
(`/tmp/athena-native-source-traversal-final-build.log`). The focused temporary
real-actor check passed at 12:33
(`/tmp/athena-source-navigation-probe-final.log`). It invoked all twelve migrated
Scheme bindings with the caret in the opposite macro scope, verified forward
and backward movement across locally hidden fields, checked attached-subtree
relative coordinates, and confirmed an edited/unlaid-out source stays pending
without moving the caret. No permanent tests, Notes changes, deployment or
commit were made. Property/boundary facades, the TeX environment lookup and the
previously listed large-input preparation budgets remain open.

### Prepared Properties And Deletion (2026-10-30)

Scheme macro/parameter classification, arity, accessible fields, field names
and types now use `generic_prepared_tree_context`. Attached source selects its
own entry; detached proposals and label-only requests select the caret entry.
The old global implementations are removed, and XML binds the public names to
the native prepared consumers. Pending bool/string/list queries return
false/empty results; arity and insertion positions return `-1`, not an invented
descriptor. `graphics-complete?` now excludes an unavailable maximum.

Post-deletion editable-content discovery takes an explicit source provider.
Native-schema nodes use immutable native field rules without preparing an
environment; non-native nodes request their own command-owned entry. It no
longer recurses under an unrelated ambient contract scope.

The real-actor probe exposed an independent deletion lifecycle error. On the
second Backspace without layout, `remove_text_sub` called `in_source()` while
scanning ordinary text ancestors, before any mathematical shape matched. The
stack in `/tmp/athena-property-delete-gdb.log` identifies that exact eager mode
read. Mode is now queried only after matching the relevant inactive symbol or
empty MathGrid cell, through explicit source preparation. The passive getter's
missing-context error remains intact.

Production built with `-j16`
(`/tmp/athena-native-property-consumers-final-build.log`). The temporary
real-actor probe passed at 12:57
(`/tmp/athena-source-properties-probe-final.log`): runtime property bindings,
opposite local/caret field policies, detached insertion proposals, pending
properties after source replacement, and consecutive deletion. No permanent
tests, Notes writes, deployment or commit were made. Start/end utilities,
field-environment utilities/converter ownership and the previously recorded
large-input preparation budgets still prevent declaring the cutover complete.

### Source And Command Boundaries (2026-10-30)

`path-start/end` now belong to the editor and return a source-addressed,
admitted boundary or `#f`. They preserve absolute root-tree and relative
attached-subtree coordinates; detached, stale and foreign sources are not
evaluated. The separate XML-generated `command-path-start/end` bindings may
prepare a source position or a prospective tree under the insertion checkpoint.
Scheme errors retain command-preparation failures rather than inventing paths.

`path-in` now retains the full template while descending through field indices.
The old recursion discarded outer macro scopes before calculating a child's
boundary. Selection wrappers, animation/locus/format commands, native big
operators and table wrappers prepare the full destination before cutting.
Paste plans retain the insertion checkpoint for a subsequent mode wrapper.
`tree-go-to` explicitly requests command preparation, while tree predicates and
math inspection retain pending behavior. Label jumps and label previews now
have command and passive boundary policies respectively; hover-link geometry
only consumes admitted entries.

AUDMAP paragraph-start context returns null when unavailable. AVD whole-source
range defaults are prepared by the owning source editor. Undo cursor hints use
immutable native structural navigation after source replacement, not the old
layout or another editor's scope. Structural differences cover whole-node
borders including hidden fields. Shifted-caret queries use source-addressed
admitted navigation and retain the caret while pending.

Production builds succeeded with `-j16`; the final log is
`/tmp/athena-native-boundaries-final-build.log`. The temporary real-actor probe
passed at 13:58 (`/tmp/athena-source-boundaries-probe.log`): four runtime boundary
bindings, opposing local/caret contracts, attached relative coordinates,
full-template `path-in`, and unavailable edited source versus explicit command
preparation. This also exercised the earlier navigation/property/deletion
fixture. The peripheral AUDMAP/AVD/history/diff edits were compiled, not given
an unrelated test suite. No permanent tests, Notes writes, deployment or commit.

Next consumers are concrete: unused Scheme field-environment and structural
search bindings in `basic.xml`, ambient search wrappers in `tree_search.cpp`,
the remaining cursor utility facade and its retained test callers, TeX export
mode statistics, AI compression and verbatim conversion. Conversion ownership
must not become a lookup of the current editor. Large-input enumeration,
single-value freezing, stale-evidence retirement and cold command preparation
budgets remain open; this batch does not complete the strict cutover.

### Implicit Utility Removal (2026-10-30)

Removed the global cursor start/end, correction/accessibility and valid or
accessible movement wrappers, the ambient generic search overloads, and six
unused Scheme bindings (four field-environment queries and two generic tree
search queries). Retained editor checks now use explicit editor command
boundaries; standalone Unicode/navigation/search checks select their native
policy directly. No new tests were added or test targets built.

The existing structured-math Scheme check does exercise independent source
search. That capability is retained as `tree-search-source`, now backed only
by immutable native source policy. It does not inherit the active editor's
macro declarations. Generic search with document-local semantics instead
requires an explicit prepared provider/query.

Removing the declarations exposed additional real consumers: graphical-text
contiguity, Scheme tree geometry, Qt AVD selection and UUID navigation.
Contiguity is now a navigation method and resolves subtree-local endpoints
through `at`, without ancestor CONCAT correction. Geometry reads admitted
boundaries and leaves its empty rectangle while unavailable; AVD and UUID
navigation explicitly prepare their owning editor's source boundaries.

Production built with `-j16`
(`/tmp/athena-native-utility-callers-build.log`). The temporary real-actor
fixture passed at 14:34 (`/tmp/athena-native-utility-probe.log`), confirming
retired bindings are absent and native source search sees a field hidden by
the attached macro's local rule. The prior source/property/boundary checks in
that fixture also passed. The retained test suites were not run. No Notes
writes, deployment or commit.

At that checkpoint, `current_cursor_navigation` had one caller: `get_env_child` for
LaTeX mode statistics. The converter's `file_output_native` has body, style and
initial inputs; use those to establish source-owned context, not a renamed
ambient query. AI compression and verbatim conversion also remain to migrate.
Large-input enumeration, freezing, stale-inference retirement and cold command
preparation budgets are still open. The goal remains active.

### Conversion-Owned LaTeX Statistics (2026-10-30)

LaTeX's mode-statistics pass no longer borrows an editor/evaluator scope. Its
file entry owns the original body, normalized style list, initial collection
and source URL. It traverses that body in source order, excludes definition and
preamble bodies from use counts, and selects each custom node's own prepared
source entry. Native nodes use immutable builtin contracts. Editor field
accessibility is deliberately not an export filter: a read-only argument can
still contain an exported macro use.

Filtering the body before this pass would change source paths and hide the
definitions needed to prepare later entries, so filtered output trees are no
longer its input. Detached source preparation now patches the document's
initial values after its style and before its authored preamble. The public
helper also accepts an explicit source URL. No active editor is consulted.

Removed the final `get_env_child` / `current_cursor_navigation` chain and its
`cursor_execution.cpp` implementation. The unused standalone Scheme mode-stats
binding is removed from XML rather than retaining an API without document
ownership. No handwritten glue or compatibility facade was introduced.

Production built with `-j16`. A temporary `/tmp` probe passed at 15:07
(`/tmp/athena-conversion-context-probe.log`): two detached documents define
opposing field modes in `initial`; export under conflicting ambient rules
still emits a direct parameter for the math definition and `ensuremath` for
the text definition. Re-export after switching the ambient context is identical,
the caller's context is unchanged, and the retired Scheme binding is absent.
The final normalized-default-input adjustment is statically checked and built,
not a reason to repeat this check or run a broad suite. Build log:
`/tmp/athena-native-conversion-context-final-build.log`.

Verbatim and AI compression still need explicit ownership. Verbatim's search
worker callers prohibit simply adding synchronous source/style execution to
its default entrypoint. Large-input analysis capture/admission and cold command
preparation also remain open. No deployment, commit or Notes writes.

### Source-Owned Compound Clipboard Text (2026-10-30)

The verbatim caller audit found a cross-owner interpretation bug in AVD copy:
source actors supplied only XML fragments, and Qt reconstructed the merged tree
and read its fields using that thread's ambient contracts. A merged tree cannot
recover distinct local definitions from its physical source documents.

`copy_ranges` now returns a `clipboard_copy` containing the unchanged XML
pieces and plain text. Each participant expands its selected content using
`exec_verbatim` at its own selection cursor before producing text. After all
participants succeed, the completion combines the strings in source order.
Qt only merges/validates the XML and publishes the already prepared text. A
failed batch returns no partially prepared clipboard payload. Existing test
callbacks were adjusted to the result type; their suite was not run or expanded.

Production built with `-j16` (`/tmp/athena-native-compound-text-build.log`). A
temporary two-real-actor check passed at 15:31
(`/tmp/athena-compound-text-probe.log`). The same `clip-local(token)` source
macro in two documents, defined differently in their initial collections,
produced `S:ONEtoken:E` and `S:TWOtoken:E`. Native clipboard XML retained both
macro invocations. Interpreting the merged source on the non-owning thread did
not produce the owner-generated text, confirming the original broken boundary.
No Notes writes, deployment or commit.

Remaining verbatim callers are not equivalent:

- Ordinary selection copy and `buffer_actor` verbatim export already expand
  through the explicit source editor before calling the formatter.
- Typesetting's `concat_active` passes `env->exec` output; HTML's `force_text`
  also operates within the converted/expanded tree pipeline.
- `search_metadata` receives a raw buffer body from `get_metadata`; its current
  tag-search helpers discard source paths. A replacement must keep those paths
  and use admitted source contexts, not run a cold evaluator inside a getter.
- `QTMVaultLinkModel::source_excerpt` receives detached fragments, including
  structural enunciation names, from UI and background workers. Its current
  prepass projects named symbols and images only. Do not make this entry load
  or execute styles on a search worker.
- The generic conversion executor also accepts direct raw document/snippet
  requests; those must be distinguished from already expanded editor exports.

The formatter itself still contains an ambient accessibility query, so this
consumer fix does not finish its cutover. AI compression and bounded analysis
capture/admission remain outstanding as recorded above.

### Explicit Verbatim Consumers (2026-10-30)

The formatter no longer reads `current_evaluation_state`. Its consumers now
select one of three distinct policies:

- `tree_to_verbatim` formats already expanded/native content using immutable
  native rules. Ordinary clipboard copy and source-actor exports already expand
  their content before entering this formatter.
- `source_to_verbatim` accepts an explicit source-position provider. Custom
  nodes use the requested entry for both access and field environment. Missing
  entries make the entire result unavailable. Table wrappers, cells, fractions
  and root degrees preserve their real source addresses instead of formatting
  synthetic trees under unrelated positions. Full-document addresses are
  relative to BODY.
- `source_text_excerpt` intentionally projects detached source text, not a
  macro's rendered expansion. It hides native data fields and includes custom
  macro arguments. Vault-link search workers and HTML literal-text extraction
  use this policy without cold evaluation or mutable ambient state.

Metadata search retains the paths of doc-data/title/author fields. The editor
supplies admitted entries only, so this getter cannot synchronously prepare
source or return a partially formatted title. Existing filename/author fallback
remains at the editor boundary. The generic converter prepares direct documents
with their own style/initial/preamble/source URL; editor-backed snippets expand
through that explicit editor instead.

Production built with `-j16` (`/tmp/athena-native-verbatim-build.log`). The
temporary runtime probe passed at 20:42
(`/tmp/athena-verbatim-context-probe.log`): opposing local/global macro fields,
table/fraction source paths, generic direct export, exact metadata field paths,
pending metadata, detached excerpts and expanded-native text. No broad test
suite was run. A subsequent argument-formatting adjustment avoids wrapping a
subargument before its containing paragraph; that statically straightforward
change is included in the next production build rather than repeating this
runtime probe. No Notes writes, deployment or commit.

At this checkpoint AI compression, implicit tree-correction utilities and the
previously recorded large-input analysis/preparation work remained open.

### Editor-Owned AI Compression (2026-10-30)

`compress-tree`, `decompress-tree`, `compress-html` and `decompress-html` now
bind native editor methods from `editor.xml`, with generated Scheme exception
translation. No free ambient conversion facade remains. Attached trees must
belong to that editor and select their own source entries. Detached proposals
retain the complete tree and prepare each custom node under the explicit
selection/caret insertion context, so a local wrapper's rules are not replaced
by the caret's outer rules. Native nodes use immutable native contracts.

The dictionary lives with the editor instead of in process-global mutable
hashmaps. A process-wide atomic serial only allocates distinct placeholder
names; it contains no source content or context. Foreign/expired placeholders
fail explicitly. Hidden template fields, exposed atomic content and returned
codes are detached from mutable source/dictionary storage. Metadata-bearing
formatting nodes use placeholders too, because raw HTML paragraph/concatenation
output cannot preserve their native metadata.

The production target built with `-j16`; the final log is
`/tmp/athena-native-compression-snapshot-build.log`. The focused temporary
real-actor check passed at 20:59 (`/tmp/athena-compression-owner-probe.log`):
all four runtime bindings, opposing local/outer rules in attached source,
detached proposed wrappers, tree/HTML round trips and foreign dictionary
rejection. A source snapshot check mutates both exposed and hidden original
text after compression and verifies the retained content and metadata through
both tree and HTML round trips. No permanent test or broad suite was added.
No Notes writes, deployment or commit.

`Data/Convert` no longer directly reads `current_evaluation_state`. Shared tree
helpers are still a separate unfinished boundary:

- `tree_modify::correct_node` uses ambient arity and can replace a node with
  empty text. Its consumers include format-wrapper removal and three generated
  Scheme bindings (`cpp-tree-correct-node/downwards/upwards`). The arity decision
  needs source-owned preparation; native concat flattening is structural.
- Formatting's `is_with_like` and the with-correction traversal still borrow
  ambient descriptors. Wrapper comparison must not imply that both operands
  share a source context.
- `prepare_document_context(tree)` in `new_style.cpp` selects an ambient snippet
  context by checking for a `theorem` definition. Detached correction needs an
  explicit context boundary instead. Symbol classification, correctable fields
  and math-statistics traversal must retain that distinction as they migrate.

These consumers and the previously documented unbounded input enumeration,
single-tree freezing, inference retirement and cold command preparation remain
part of the active goal. The successful conversion checks do not establish a
finished or deployable strict cutover.

### Source-Owned Live Correction And Wrappers (2026-10-30)

The preceding `tree_modify`/live-formatting items are now migrated. The three
`cpp-tree-correct-*` names bind editor methods through XML, with generated
exception translation. These are live mutation commands: their input must be
attached to the calling buffer. The obsolete global downward/upward traversal
has been removed. Upward traversal stops at that buffer's root; downward
traversal prepares each current custom-node entry after correcting its children.
Native nodes use the immutable native baseline without cold evaluation.

The lower `correct_node` primitive now receives its query explicitly. Only an
authored/native arity mismatch can erase unannotated markup. Optional inference
does not authorize source deletion, and identified/attributed subtrees are
preserved for owner validation, matching import correction's existing source
integrity rule. Concat flattening remains observer-preserving and structural.
The primitive no longer reads `current_evaluation_state` or a global document.

`with-like?`, `with-same-type?` and `with-similar-type?` select each operand's
admitted source context (or the caret for a detached proposal). Missing custom
entries return false without evaluation. Native wrappers use native rules.
Formatting's insert/detect/toggle operations instead prepare their source or
proposal entry explicitly. Their failures cross generated Scheme exception
boundaries, including the standard bold/italic/small-caps/underline commands.

Same-name custom wrapper comparison checks both contexts and the definitions'
contents. Binding receipt identity is not definition-content equality:
independent cold preparations create distinct binding objects. This semantic
comparison does not relax binding identity checks for declaration attachment
or analysis publication. Structural wrapper-prefix comparison no longer
performs hidden ambient queries as an assertion side effect. The offline
`with_correct` traversal still has its own implicit-context migration pending.

Production built with `-j16`
(`/tmp/athena-native-correction-definition-build.log`). The temporary real-actor
check passed at 21:25 (`/tmp/athena-source-correction-probe.log`): all six
runtime interfaces, conflicting outer/local arities, node/downward/upward
mutation, retention of an identified invalid node, and rejection of detached
mutation. Wrapper checks distinguish source from caret proposals and different
local definitions. After a source edit, passive lookup stays pending while
explicit preparation restores the correct local rule; separately prepared
copies of the same wrapper compare equal. No broad suite or permanent tests,
no deployment, no commit and no Notes writes.

The remaining concrete tree consumers are the offline with/math correction
pipeline, symbol classification/correctable-field utilities, and mathematical
statistics. The latter is called by `math/math-stats.scm` from input heuristics:
its `buffer`/`cursor` caches currently live in a global native table, and source
mode traversal still reads ambient contracts. It needs owner-local caches and
explicit source entries, not synchronous document preparation inside a getter.
The previously recorded large-input analysis/preparation work remains open.

### Source-Owned Mathematical Statistics (2026-10-30)

The mathematical-statistics ownership item above is now migrated. The backend
is an owner-held `math_statistics` object, not a process-global table. It accepts
an explicit source-path context provider and uses an explicit-query overload
of `is_correctable_child`; it no longer reads `current_evaluation_state`.
Native nodes use immutable native rules, custom nodes use their actual source
entries, and macro definition bodies are not counted as formula uses. An
unavailable entry leaves the result pending with no visible partial or prior
counts. Count-map keys own their source content instead of retaining mutable
tree leaves that would invalidate hash keys after subsequent editing.

The `math-stats-compile` free-function binding is retired. XML-generated
`math-stats-update`, `math-stats-occurrences` and `math-stats-number-in-role`
dispatch to the calling editor. The Scheme module no longer owns global URL,
paragraph-count or cursor-source state. Buffer and cursor-paragraph caches
belong to `edit_typeset_rep`, stamped with the successful execution baseline
and contract trace. Source edits and environment invalidation clear them before
the next layout; optional-analysis publication clears them too. A changed
cursor paragraph recomputes only that cache. Getters never cold-prepare source
contexts or run macro evaluation to fill a missing entry.

Production built with `-j16`
(`/tmp/athena-native-math-statistics-build.log`). The temporary runtime check
passed at 21:44 (`/tmp/athena-math-statistics-probe.log`). It exercises source
paths through local declarations, opposing field modes, unchanged scoring,
atomic pending results, and map-key independence from subsequent source edits.
Two real actors verify all three generated interfaces, removal of the global
compile binding, document isolation, paragraph switches, pre-layout pending
counts and post-layout refresh after a same-paragraph edit. No permanent test
or broad suite was added; no Notes writes, deployment or commit.

This is not a completion claim for input latency: first statistics generation
still walks the source synchronously, and now invalidates correctly on each
source/layout change rather than hiding edits behind the old paragraph-count
heuristic. This traversal needs incremental/scheduled work before final
deployment. Offline with/math correction, bracket/symbol classification and
the separately documented bounded-analysis input/retirement work also remain
part of the full active goal.

### Source-Addressed Wrapper Normalization (2026-10-30)

`with_correct` and `superfluous_with_correct` now accept explicit source-contract
providers. The parameterless `is_with_like` and the ambient `with_decompose`
helper are removed. The wrapper normalizer retains each custom output node's
source context while flattening/recomposing the tree. This provenance belongs
to the operation and is keyed by retained node identity, not structural equality
or a transformed path. Wrapper comparisons use both original contexts, including
definition content. Thus distinct scoped definitions cannot become one wrapper
merely because their names and explicit arguments match.

Macro definitions and quotation templates are left intact rather than treating
their content as executed instances. Metadata-bearing nodes retain the existing
identity-preservation rules. A requested but unavailable source entry raises an
explicit error; correction builds returned values and does not mutate its input.

Detached correction owns a style/initial/preamble-backed BODY provider. It
preserves the document envelope and does not borrow an active editor's state.
An independent snippet is prepared under explicit generic style; a DOCUMENT
snippet retains its top-level paragraphs instead of becoming one giant replay
unit. This change removes the ambient-theorem heuristic from wrapper passes,
not yet from the later mathematical passes.

The generated `with-correct`, `with-correct-superfluous` and `manual-correct`
interfaces now belong to the editor with generated Scheme exception conversion.
An attached argument prepares its real source entry; a detached argument is a
proposal at the insertion context. The manual command captures source math mode
before constructing its temporary math wrapper, replacing the Scheme helper
that first detached the argument. Each wrapper stage prepares the transformed
tree anew in the retained entry context. Subsequent legacy math passes still
need explicit contexts and are not covered by this interface change.

Production built with `-j16`
(`/tmp/athena-native-wrapper-correction-final-build.log`). The focused temporary
runtime check passed (`/tmp/athena-wrapper-normalization-probe.log`): same/different
definitions, retained context after nested recomposition, source paths, metadata,
quotation opacity, missing contexts, detached document initial/local rules,
all three runtime bindings, source versus caret proposal rules, source math mode,
non-mutating results and preparation after an edit invalidates layout.
No permanent test or broad suite was added; no deployment, commit or Notes writes.

The next correctness boundary is **values paired with contracts**. Redundant
wrapper removal still propagates a structural field-environment tree and starts
detached/manual passes in text mode. This does not represent a different initial
document mode or earlier executed assignments. For example, an initial math-mode
document containing an explicit text-mode WITH must not lose that WITH merely
because the corrector's hardcoded baseline says text. The correction provider
needs the actual prepared source values as well as its scope. This is also
required for remaining `syntax_query` consumers, homoglyph/invisible correction,
bracket classification and formula-error counting. Existing unbounded statistics
traversal and analysis snapshot/diff/freezing/retirement work remain open. The
full goal is still active and undeployed.

### Source Values Paired With Contracts (2026-10-30)

The previous redundant-wrapper limitation is resolved. A new owner-local
`source_environment_preparation` returns the actual evaluation checkpoint at
each source entry. `prepare_document_source_environments` shares the existing
style/initial/preamble and advancing-paragraph implementation, while the
contract-only API retains its cheaper scope-only capture. Empty BODY-relative
paths select the baseline, not the most recently advanced paragraph.

Redundancy is checked against that checkpoint's values, not a propagated WITH
tree or an assumed text baseline. Thus initial math mode, preceding assignments
and macro-local mode all participate. ASSIGN expressions and WITH parameters
are not traversed as display content. Each manual correction stage still prepares
its rewritten input afresh. The temporary math-mode wrapper belongs after the
wrapper passes, immediately before the remaining legacy math stages; otherwise
correctly removing it would inadvertently reset those stages to text mode.

Production built with `-j16`
(`/tmp/athena-native-source-values-final-build.log`). The temporary actor probe
passed (`/tmp/athena-source-values-probe.log`): initial/sibling/local modes,
root/backward/inline source requests, contract-only projection, unavailable
entry errors, detached versus attached commands, non-mutating correction, and
fresh values after an earlier assignment changes without another layout. It
also exercises actual bracket conversion after wrapper correction, not merely
identity output at a math caret. No permanent tests, deployment, commit or Notes
writes. The later math/syntax consumers and bounded analysis/statistics work
remain in the active goal.

The later attempted source-aware bracket migration is not included: its custom
delimiter-macro case failed before the containing token sequence was rewritten.
That experimental implementation and its generated-interface changes were
withdrawn, rather than declaring the partial probe successful. The accepted
source-values probe above remains the evidence for this delivery. The unused
`count_math_errors` implementation and declaration are removed after confirming
there are no callers. See the design document's Bounded Delivery section for
the three remaining changes and their concrete completion conditions.

The final retained delivery built successfully with `-j16`
(`/tmp/athena-native-context-delivery-build.log`). Existing caller/test edits
adapt retired interfaces; no additional permanent regression suite was added.

### Source-Owned Homoglyph And Invisible Correction (2026-10-30)

The homoglyph pass, superfluous invisible removal, and missing invisible
insertion now consume explicit source-environment providers. They no longer
borrow `current_evaluation_state` or `prepare_document_context(tree)`.
`math_correction_source` pairs each stage with its original tree and retains
output-token occurrence addresses across recomposition. Shared source handles
in different scopes produce separate output handles; contexts are requested
from the original addresses, never guessed from transformed paths.

Modes come from actual source checkpoints, including document initial values
and preceding assignments. The existing math-font literal exclusion remains
explicit, as do field eligibility and metadata preservation. Assignment and
quotation templates and WITH binding parameters are not display-content
correction targets. Missing requested entries report an error rather than
borrowing the caret or another document.

Syntax substituted from a macro definition retains that expansion's prepared
environment, rather than being looked up as though it were another attached
source occurrence. This matters for recursive aliases and for invisible
insertion's operator/body heuristics. The mathematical correction algorithms
themselves remain the existing implementations.

Detached calls preserve the document envelope and prepare style, initial and
preamble state for each transformed body. Editor calls capture the actual
source entry or proposal insertion context. Existing
`invisible-correct-superfluous` and `invisible-correct-missing` bindings move to
generated editor interfaces with exception conversion. There are no additional
Scheme names or Scheme implementation. Manual correction supplies explicit
contexts to these passes; only its still-legacy bracket stage retains the
temporary mode adapter. Bracket upgrading and its ambient symbol facade remain
a separate pending change, not implicitly covered by this batch.

Production built with `-j16` (`/tmp/athena-native-invisible-build.log`). The
temporary runtime check passed at 23:16 (`/tmp/athena-source-invisible-probe.log`):
shared macro handles with different local meanings, recursive alias expansion,
initial and preceding-assignment modes, literal-font and template preservation,
explicit missing-entry failure, both existing generated bindings, attached
versus proposed source, manual homoglyph correction, and fresh values following
an edit without another layout. No permanent test suite was added. The batch
stops here; bracket migration and scheduled analysis/statistics remain open.

### Source-Owned Bracket Correction (2026-10-30)

Bracket upgrading and big-operator grouping now use `math_correction_source`.
Their native editor commands capture the attached source entry or proposal
insertion environment; detached calls prepare the document envelope. Every
rewritten stage gets its own source provider. The manual-correction mode
adapter is gone, along with ambient symbol classification, child eligibility
and the free `prepare_document_context(tree)` fallback. The editor's explicit
document-preparation member is a different API and remains in use.

Local syntax classification does not authorize lossy delimiter serialization.
The existing matcher can encode atomic or native LEFT/MID/RIGHT/BIG delimiters,
not arbitrary macro invocations. A sequence containing a custom delimiter is
therefore preserved rather than replacing its source with `<nobracket>`.
Custom operator syntax still participates in grouping: a trailing macro that
locally means an infix operator stays outside a big operator's body, while the
same shared macro under a basic-symbol definition stays inside it.

Legacy bracket movement carries a parent-source factory through recursion.
After moving a delimiter, it prepares the reconstructed parent and then the
changed math child, retaining preceding assignments and surrounding bindings.
Definition/quotation templates and WITH binding values are not rewritten as
display content. Existing bracket/big Scheme names now use generated editor
bindings with exception conversion; no Scheme behavior or new API was added.

Production built with `-j16` (`/tmp/athena-native-brackets-build.log`). The
temporary source/actor check passed at 23:34
(`/tmp/athena-source-brackets-probe.log`): source/initial/forced modes,
custom-delimiter source preservation, shared macro grouping, template/envelope
preservation, missing-entry errors, parent contexts after bracket movement,
both runtime bindings, and attached versus proposed inputs. Existing native
test callers were adapted to the explicit symbol-query signature, without
running or expanding that suite. This completes the bounded math-correction
batch; scheduled statistics, analysis responsiveness and final deployment remain.

### Scheduled Mathematical Statistics (2026-10-30)

Input queries now request work and read completed owner-held results; they do
not traverse source or prepare cold environments. `math_statistics` has a
resumable source traversal with explicit pending/ready/unavailable states.
Its synchronous `compile` entry remains for explicit offline callers. Partial
counts are never observable. Document containers are no longer copied into the
token table when the initial environment is mathematical.

The BufferActor retains one requested continuation per view, including across a
full mailbox. A continuation processes a cursor slice and a buffer slice, then
returns to the FIFO queue. Each traversal uses a node-action budget and a soft
four-millisecond deadline; individual formula tokenization is still the existing
algorithm, not a new hard-real-time guarantee. Views are resolved at dispatch,
not retained as pointers in queued work, and destruction removes pending work.

Source epoch, execution baseline, contract trace and prepared-context index
identity jointly determine validity. The context-index identity also restarts a
previously unavailable job when progressive layout admits more source entries
without emitting another contract event. Publication of new contracts marks
the cache invalid. Invalidating an edit only marks the result unusable; token
tables are retired in the deferred continuation. Cursor movement changes its
own request without invalidating the buffer statistics. Missing environments
stop a generation rather than scheduling an endless retry loop.

Scheduling and lifecycle logic are native C++; no Scheme implementation or new
Scheme binding is added. Existing input heuristics receive zero until a valid
result is ready and retain their existing non-statistical behavior meanwhile.

Production built with `-j16`
(`/tmp/athena-native-scheduled-statistics-final-build.log`). The temporary
source/actor check passed at 23:54
(`/tmp/athena-scheduled-statistics-probe.log`): sliced counts equal synchronous
counts, partial and unavailable results stay hidden, owned keys survive source
mutation, getters do not complete a traversal, edits supersede queued work,
cursor moves preserve buffer statistics, local rules and actor isolation hold,
and closing a view with pending work completes normally. No permanent test
suite was added. This completes the scheduled-statistics delivery; analysis
responsiveness and final integration/deployment remain open.

### Persistent Input Receipts And Cooperative Freezing (2026-10-31)

`evaluation_inputs::record` now updates one persistent receipt-map branch.
Taking a snapshot returns the retained immutable root instead of draining a
mutable map of all writes since the last checkpoint. Repeated assignments still
receive distinct receipts, deletions remove the name, and restored branches
retain their previous receipt identities. Baseline construction remains an
explicit complete initialization.

Interactive analysis capture now retains an owner-side `frozen_tree_builder`
when a value cannot finish in one slice. It traverses a bounded number of tree
actions per advance, preserving the ordinary deep-copy and metadata semantics.
Only the completed detached tree becomes a `frozen_tree`; publication does not
perform another full copy. A changed receipt discards that partial freeze before
continuing, and the name stays unresolved until the replacement is complete.
The builder is noncopyable, and closing the analysis owner destroys its partial
freeze on that owner before transferring detached analysis state to the worker.
Capture compares retained payload identities, leaving semantic equality to the
detached analysis stage rather than comparing large frozen values on capture.

This removes checkpoint batch folding and whole-tree freezing from interactive
capture, not all remaining owner-side bulk work. Input/scope diff enumeration,
dependency closure retirement, and the latter's semantic comparisons remain
explicit follow-up work before completion and deployment.

Production built with `-j16`
(`/tmp/athena-native-capture-receipts-final-build.log`). The temporary native
check passed at 00:30 (`/tmp/athena-capture-receipts-probe.log`): immutable
receipt checkpoints survive repeated assignments, removals and restored
branches; sliced freezing preserves metadata and detached ownership; incomplete
freezes cannot publish; edits supersede partial large captures; unrelated
inference remains usable while capture is pending; and closing an owner during
freezing releases its source handles on the owner. The completed background
projection is compared semantically, not by pointer identity across independent
analysis engines. No permanent test suite was added. This batch is complete;
the remaining analysis work and final deployment above are not.
