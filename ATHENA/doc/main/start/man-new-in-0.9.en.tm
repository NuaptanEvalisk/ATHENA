<TeXmacs|2.1.4>

<style|tmdoc>

<\body>
  <tmdoc-title|What is new in <ATHENA> 0.9>

  <ATHENA> 0.9 is the release where ATHENA's own runtime architecture becomes
  the normal execution model. It also substantially deepens Artifacts,
  knowledge links, Materials, Vault Maintenance, mathematical editing, and
  native Qt workflows. This page summarizes user-visible and architectural
  changes since version 0.8.

  <section|Per-buffer execution actors and shared rendering>

  Every live document buffer now has one long-lived <verbatim|BufferActor>.
  Mutable document state, editor state, cursor and selection, undo information,
  typesetting structures, save state, and buffer-bound Scheme execution belong
  to that actor. Commands for one buffer execute serially, while independent
  buffers can work concurrently.

  Qt remains the owner of windows and Qt objects. It exchanges numeric actor and
  view identifiers with the document side instead of borrowing editor objects.
  Dialogs, menus, autosave orchestration, buffer switching, zooming, scrolling,
  file choices, and other inherited main-thread workflows have been migrated to
  explicit actor/UI boundaries.

  Rendering uses one shared <verbatim|RenderService>. Buffer actors submit
  immutable render commands through fixed transport storage; Qt presents
  completed frames and does not traverse actor-owned document or box trees.
  Stale frames and resources are rejected by generation and identity checks.

  <section|Knowledge links become structural>

  Radioactive links now understand native mathematical definition names, not
  only flattened text. Artifact extraction preserves mathematical name trees,
  aliases, source ranges, and distinct identities for formulas which merely look
  similar when converted to text.

  Artifact-name resolution supports exact and normalized partial matches.
  Ambiguous or partial queries open stable read-only disambiguation pages rather
  than silently choosing one source. Matching is also available from selection
  context menus.

  Hold <key|Shift> while hovering a wikilink or radioactive artifact link to
  open a rendered target preview. The preview is typeset on the owning buffer
  actor, does not open or change a GUI buffer, and can itself contain links that
  open nested previews. Wheel input and clicks are captured by the preview so
  the document underneath is not accidentally edited.

  Radioactive matching now works through transcluded content, excludes complete
  defining regions more carefully, and advances incrementally through document
  layout. A document containing transclusions can also be flattened into a new
  standalone document.

  <section|Materials become a practical source library>

  The Materials manager can import complete directories recursively or
  non-recursively without opening a review dialog for every file. Recognition
  runs concurrently with a configurable worker count while retaining the same
  identifier and content-hash deduplication rules as interactive import.

  PDF recognition is more conservative about title pages and authors, handles
  ordinary text-layer and OCR coordinate systems, and improves CJK creator and
  title recovery. Unusable embedded OCR can be rejected in favor of a better
  recognition path.

  Repeated Zotero imports reconcile attachments by source identity and content
  instead of duplicating them. Managed files can be re-identified from their
  current attachment, renamed to canonical creator-date-title filenames after
  metadata changes, and checked for unreferenced files. Vault Maintenance can
  maintain Material attachments as one of its passes.

  <section|Search, spelling, and completion>

  <key|Ctrl+F> now opens an actor-owned in-document search bar. Search state,
  selection, and cursor movement remain on the owning BufferActor while Qt
  displays the query and match count. <key|Ctrl+H> provides the corresponding
  actor-owned replacement bar.

  Live spelling now uses in-process <name|Hunspell>. Dictionaries, engines,
  session words, and verdict caches are isolated by thread. Automatic checking
  proceeds in bounded batches beginning near the visible viewport and abandons
  stale work after edits. Suggestions are generated only when requested.

  Text completion uses a native KDE-style candidate list. Formula shortcuts may
  activate as soon as a sequence is unambiguous instead of requiring an extra
  <key|Enter>.

  <section|Mathematical and structured editing>

  Evaluation bars are now first-class stretchable mathematical structures.
  <ATHENA> can wrap the current row prefix or an explicit selection in an
  automatically sized right evaluation bar without crossing fraction, bracket,
  or table-cell boundaries.

  Table rows and columns can be resized with the mouse. Equation arrays survive
  clipboard operations more reliably, matrix and determinant layout is protected
  from accidental mouse resizing, nested delimiters resolve their full height,
  and automatic delimiters are centered on the mathematical axis.

  Unicode mathematics gained fixes for n-ary operators, vector accents, hidden
  multiplication, supplementary-plane input, and legacy calligraphic rendering.
  Font fallback uses loaded face metrics for alignment, and document and vault
  font profiles now share one model including CJK fallback choices.

  <section|Vault Maintenance and vault-wide transformations>

  Maintenance Setup is generated from one canonical pass registry shared by
  planning and execution. Optional operational passes can be enabled or skipped
  without changing mandatory validation and safety passes.

  Maintenance can scan structural image references for missing files, remove
  redundant block wikilinks, maintain Material attachments, refresh artifacts
  and Continuous RAG, and continue to generate websites and backup mirrors.
  Read-only anchoring candidate checks can run in parallel while actual document
  mutation remains sequential.

  Delegated Artifact and RAG work is more incremental and resilient. Artifact
  definition-range decisions are checkpointed by source fingerprint, remote
  results are validated strictly, payload sizing adapts to transport failures,
  and returned database patches remain local transactions.

  A new Workspace command applies one user-supplied Scheme transformation across
  a vault. It previews changed documents, validates staged results, checks for
  concurrent changes, creates backups, applies updates transactionally, and can
  roll back on failure. Materials lookup and citation-aware rendering are
  available to these transformations without exposing mutable GUI state.

  <section|A smaller, more explicit ATHENA runtime>

  ATHENA no longer ships the inherited TeXmacs plug-in architecture or external
  Session transports. CAS, Python, shell, Jupyter, proof-assistant, plotting,
  and legacy ChatGPT Session adapters are gone. In-process Scheme sessions and
  executable fields remain, while Codex, Delegation, RAG, Materials, and other
  external work use purpose-built protocols.

  The native TMDB engine and Data tool have been removed. Modern vault maps and
  namespaces use SQLite; legacy TMDB vault maps are not imported. Legacy local
  identity management, GPG document encryption, wallet integration, literate
  programming, collaboration-era Projects, Git/SVN views, Comments, Relate,
  live-document mirroring, old References management, and UI translation
  infrastructure have also been retired.

  Scheme native bindings are now generated directly from validated XML
  descriptions instead of checked-in generated wrappers. ATHENA still uses its
  vendored and modified <name|Guile> 3.0.10 runtime and private BDW-GC; the
  vendored Guile tree has been trimmed of the REPL, readline, examples,
  benchmarks, Guix/Emacs integration, unused Web/SXML/Texinfo libraries, and
  other components not shipped by ATHENA.

  Program-text highlighting now uses <name|KF6 Syntax Highlighting> rather than
  inherited handwritten language lexers and Scheme keyword tables.

  <section|Diagnostics and platform reliability>

  Linux builds can opt into an external <verbatim|ATHENA-Watchdog> supervisor.
  The Qt event loop emits a nonblocking heartbeat; a stalled process can be
  diagnosed without moving ownership-sensitive editor state onto the watchdog
  thread. Native crash reports also resolve application addresses through
  <verbatim|addr2line> when available.

  Startup, save, autosave, export, namespace sorting, font previews, buffer
  teardown, menu providers, link navigation, and many other workflows were
  hardened against thread-affinity and ordering errors during the actor
  migration. These changes are intentionally architectural: ownership guards
  remain strict instead of allowing old global editor access to return.

  <tmdoc-copyright|2026|Nuaptan Felix Evalisk>

  <tmdoc-license|Permission is granted to copy, distribute and/or modify this
  document under the terms of the GNU Free Documentation License, Version 1.1
  or any later version published by the Free Software Foundation; with no
  Invariant Sections, with no Front-Cover Texts, and with no Back-Cover
  Texts. A copy of the license is included in the section entitled "GNU Free
  Documentation License".>
</body>

<initial|<\collection>
  <associate|language|english>
</collection>>
