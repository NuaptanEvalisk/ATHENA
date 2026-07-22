<TeXmacs|2.1.4>

<style|tmdoc>

<\body>
  <tmdoc-title|ATHENA knowledge workflows>

  <ATHENA> combines structured mathematical editing with vault-wide
  organization, navigation, interoperability, and computation. This chapter
  introduces the principal <ATHENA>-specific workflows. The Preferences and
  Namespaces chapters describe their configuration and namespace model in
  greater detail.

  <section|Vault foundations>

  A vault is a directory of <verbatim|.ath> documents together with
  <verbatim|Vaultfile.json> and vault-local databases. The JSON file records
  paths for the UUID map, namespace registry, preferences, RAG index,
  websites, startup pages, and related vault metadata. Opening a legacy
  <verbatim|Vaultfile> migrates it once and preserves the old file as a
  non-overwriting backup.

  Document and anchor identities are stored in <verbatim|map.sqlite>. A
  legacy <verbatim|map.tmdb> is migrated automatically. UUIDs remain the
  authoritative link identity; filename and anchor hints are only aids for
  humans and repair tools.

  Rename vault files and directories from Vault Explorer. <ATHENA> first
  displays an operation summary. A confirmed rename updates the filesystem,
  journals the UUID-map change, and structurally rewrites affected local
  image and asset paths while preserving relative references. RAG indexes,
  recent-file entries, and auxiliary UUID hints are intentionally not
  rewritten in real time.

  Use <menu|File|Compare two files> to compare two <verbatim|.ath> documents.
  The resulting side-by-side document panes align document structure first
  and then highlight character changes inside corresponding text nodes.

  <section|Finding and moving through knowledge>

  Global Search reports occurrences, not merely filenames. It can search the
  whole vault or one namespace, limit results to a structural target type,
  and use case-insensitive or fuzzy matching. <menu|Any> includes headings,
  paragraphs, and enunciations. Exact results precede approximate results.

  Wikilink and transclusion inserters provide the same namespace, target-type,
  case, and fuzzy controls. Their file-first pages support ranked completion;
  their content-search pages can be stopped without discarding results already
  found. Default wikilink display text is configurable separately for files,
  headings, and anchors.

  The Quick Switcher includes Raw, Structured, and Recents views. Recents
  contains only <verbatim|.ath> files actually opened in the active vault.

  <menu|View|Neighborhoods> aligns several natural orderings around the active
  note: files in the same directory and files in each direct namespace.
  Select a row to choose the active neighborhood. Use
  <key|Ctrl+Alt+left> and <key|Ctrl+Alt+right> to open adjacent notes in the
  current document pane. On supported touchpads, Shift with a horizontal
  swipe performs the same navigation. When UNIX primary selection is
  disabled, middle-click cycles the selected neighborhood.

  Namespace hierarchy graphs show containment. Local Reference Graph shows
  direct incoming document references; Reference Graph follows them through a
  configurable number of levels or without a limit. Hover highlights direct
  incoming references and Shift-hover traces the complete incoming subgraph.
  The reference panes also report connected components, first integral
  homology, and free fundamental groups of the displayed graph. Interactive
  graph nodes move only when dragged.

  <section|Interoperability>

  <menu|Edit|Paste from|Markdown> converts clipboard Markdown through the AOFM
  parser. <menu|Edit|Paste from|ChatGPT> first repairs the formula delimiters
  and multiline mathematics commonly lost when copying a ChatGPT response,
  then uses the same structured Markdown conversion.

  When a link opens a non-native local file, <ATHENA> asks whether to convert
  it into an <ATHENA> document, edit it as plain text, or use the system
  application. Failed or empty conversions report an error instead of opening
  a blank buffer.

  LaTeX export uses UTF-8, includes modern image files directly, and can copy
  referenced images beside the destination for portability. Versioned
  <verbatim|ATHENA-DATA> records preserve structures that ordinary LaTeX
  cannot represent, including wikilinks, card links, transclusions, anchors,
  metadata, styles, and native commutative diagrams. Import understands these
  records, restores serialized <ATHENA> trees, observes skip ranges and image
  size auxiliaries, and warns when the exporting <ATHENA> version is newer.

  <section|Native mathematical tools>

  Insert a native commutative diagram from
  <menu|Insert|Mathematics|Commutative diagram> or by typing <key|\ cd> in
  mathematics mode. Click the grid to create an editable formula vertex; drag
  from one vertex to another to create an arrow. Hover halos reveal selectable
  geometry, vertices can be moved, and <menu|Arrow style> opens a live styling
  pane. See <hlink|Commutative diagrams|../math/man-commutative-diagrams.en.tm>
  for the complete AST and editing model.

  Right-click a formula and choose <menu|Inspect AST> to view its structured
  tree in a reusable graph pane. Generated tables of contents have screen-only
  folding controls; folding does not change document source or printed output.

  <section|Artifacts, Codex, and RAG>

  The Artifacts system incrementally indexes enunciations, associated proofs,
  and bold-text definitions in vault-local SQLite databases. Build indexes for
  the current document or the complete vault from <menu|Tools|Artifacts>, then
  browse and search them in <menu|View|Artifacts>. If a compatible small GGUF
  model is installed, llama.cpp selects the paragraph range belonging to a
  bold definition; otherwise a deterministic structural fallback is used.

  Artifact Definition Span Delegation moves only that model decision to an
  authenticated ATHENA backend. Candidate paragraphs are deduplicated and
  submitted to an asynchronous FIFO queue; the backend combines work into
  microbatches while local progress reports queued, running, and completed
  items. Local ATHENA validates every request identifier and paragraph range
  before beginning its SQLite transaction. A failed, incomplete, or cancelled
  remote job leaves the existing Artifact databases unchanged.

  <ATHENA> integrates the official OpenAI Codex AppServer for authenticated
  document completion. Completion inserts a non-editable Thinking marker in a
  new paragraph while work runs asynchronously, then replaces it with normal
  editable document content. If the selection contains images, native graphics,
  or commutative diagrams, <ATHENA> renders temporary PNG assets, substitutes
  stable figure placeholders in the LaTeX prompt, and supplies the matching
  images through Codex's multimodal input. Temporary prompt, response, and
  image files are removed when the request finishes. Authentication lives in a
  dedicated Codex home configured under <menu|Edit|Preferences|Other|AI>.

  Continuous RAG keeps search data in the vault-local <verbatim|rag.sqlite>
  database and can serve it through a read-only MCP endpoint. Optional RAG
  Delegation encrypts changed <verbatim|.ath> documents for a trusted remote
  ATHENA backend or <verbatim|athena-transmitter>. Assets, backups, preferences,
  UUID maps, and existing databases are not sent. The local client merges only
  the returned embedding-row patch and continues to perform local search.

  <section|Workspace interaction>

  Documents and tools are Advanced Docking System panes. A tool opened by a
  shortcut or menu is placed in the active ATHENA top-level window, including
  an independent floating document window. <key|Ctrl+w> closes the focused
  non-document pane as well as ordinary document panes.

  Native Wayland and Windows support pinch view zoom without re-typesetting on
  every gesture update. Optional auto-hidden toolbars expand as an overlay, so
  revealing them does not resize or move the document viewport.

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
