<TeXmacs|2.1.4>

<style|<tuple|tmdoc|english>>

<\body>
  <\tmdoc-title>
    Welcome to <ATHENA> version <texmacs-version>
  </tmdoc-title>

  Thank you for using <ATHENA>. Version 0.10 is a source-only release.

  <hlink|Changes since 0.9 and upgrading an existing vault|release-0.10.en.tm>

  <ATHENA> stands for the <em|Advanced Typesetting and Hypertext Environment
  for Notes and Archives>. It is a mathematics-centered knowledge work
  environment built from GNU <TeXmacs>, combining structured WYSIWYG
  typesetting with vaults, wikilinks, transclusions, namespaces, Materials,
  semantic Artifacts, rendered search, cloud task sync, continuous RAG, and
  import tooling for large mathematical note collections.

  <\description>
    <item*|Warning: not for everyone>

    Use <ATHENA> only if you are disappointed with <strong|both <LaTeX> and
    Obsidian>. If Markdown plus plugins is enough, use Obsidian. If
    source-first batch typesetting is enough, use <LaTeX>. <ATHENA> exists
    for the uncomfortable middle: interactive mathematical writing at scale.

    <item*|Vaults, wikilinks, and transclusions>

    <ATHENA> vaults are self-contained mathematical knowledge bases. They
    support preview-backed wikilinks to persistent document nodes and
    transclusions of ordered node selections. UUIDs live in the source
    documents; the location index is a rebuildable cache, not the owner of
    link identity. Generated anchor pairs and file recovery hints are no
    longer required.

    <item*|Namespaces>

    Namespaces classify files by filename templates rather than by filesystem
    folders. <ATHENA> supports abstract, semi-concrete, and concrete
    namespaces; namespace homepages; namespace summaries at
    <samp|tmfs://ns/!name>; namespace-aware quick switching; namespace-aware
    search; namespace-aware file creation; custom C sorters; and generated
    sub-product namespaces.

    <item*|Search and navigation>

    The global search pane shows individual occurrences rather than just
    filenames. Hits can be filtered by namespace and enunciation type, and the
    preview pane renders a small read-only <ATHENA> document around the hit.

    <item*|Mathematical input>

    <ATHENA> provides Mathematica-style math shortcuts, an <key|Esc>-based
    symbol picker, convenient enunciation aliases, extended textual math
    operators, upright <samp|\\mathrm>, script <samp|\\mathscr>, boldsymbol
    input, paired angle brackets, and norm brackets.

    <item*|Enunciations>

    Theorem-like environments are treated as first-class structure. <ATHENA>
    uses a common enunciation node with a kind property for definitions,
    theorems, proofs, solutions, and other categories. Persistent UUIDs and
    structured name, attribution, year, and proof-target properties separate
    identity and metadata from the rendered body.

    <item*|Materials and citations>

    Vault-native Materials replace the legacy bibliography workflow. Books,
    articles, chapters, and other sources have stable UUIDs, typed metadata,
    managed attachments, CSL citations, and referenced-Materials lists.
    Libraries can be imported from BibTeX or from Zotero's Local API.

    <item*|Artifacts and radioactive links>

    <ATHENA> indexes enunciations and bold-text definitions as stable semantic
    Artifacts. Artifact names in ordinary prose can become automatic
    radioactive links with case, inflection, possessive, eponym, and
    same-name disambiguation handling. Realtime document overlays make newly
    edited objects available across the vault; structural background extraction
    complements the full artifactization workflow and its range selection.

    <item*|Obsidian/AOFM conversion>

    <ATHENA> can import Obsidian-style mathematical vaults, including
    wikilinks, transclusions, callouts, proofs, anchors, images, tables, PDF
    links, card links, formula normalization, generated titles, and table of
    contents.

    <item*|Maintenance, backups, and publishing>

    Vault maintenance is now a modular pass pipeline. It can health-check
    documents, create zstd backups, purge old full backups and pre-save
    histories, normalize referenced assets, collect orphan assets, and generate
    maintenance summary pages. Continuous Maintenance canonicalizes old
    enunciation nodes. UUID, Maintenance, Artifacts, and NPU RAG workers share
    one progress indicator. Multiple backup dispatchers can mirror a vault after saves,
    maintenance, or idle periods. Static websites are generated incrementally
    and may include per-document PDFs, redirects, and post-generation
    deployment commands. Live SQLite databases are backed up through snapshots.
    PDF export uses the native backend and can optionally generate temporary DataArt
    cover images.

    <item*|Google Tasks and continuous RAG>

    <ATHENA> can connect to Google Tasks, synchronize cloud todo lists in
    documents, show task updates through toast notifications, and expose a
    headless RAG MCP server for vault search and retrieval. Optional continuous
    embedding runs in an isolated OpenVINO Intel NPU worker, processing the
    active vault until it converges and revisiting changed sources.

    <item*|Native Qt interface>

    <ATHENA> uses native Qt panes and dialogs for preferences, vault
    exploration, namespaces, global search, page properties, paragraph
    properties, metadata, error messages, custom styles, wikilinks,
    transclusions, Google Tasks, command palette, font selection, and color
    selection. The application owns the menubar, command palette, and global
    shortcuts, even without an open buffer. Each document has its own tab and
    editor toolbars; Focus commands follow the active work pane. Menus and
    toolbars use native C++ behavior and JSON declarations.
    Preferences provides direct search across categories, tabs,
    sections, and individual settings.

    <item*|Large documents>

    Long, transclusion-heavy documents use progressive screen typesetting and
    shared structural, font, and vault caches. <ATHENA> presents the first
    usable viewport promptly and continues screen layout in bounded batches,
    while print and export retain complete deterministic typesetting.

    <item*|Modern runtime and mathematical input>

    <ATHENA> ships a private <name|Guile> 3 runtime with parallel incremental
    Scheme bytecode compilation. A native Handwritten Symbol pane recognizes
    mouse, touch, or tablet input locally and inserts ranked mathematical
    symbols without a network service.

    <item*|Foundations and divergence>

    <ATHENA> is a fork of GNU <TeXmacs>. We gratefully acknowledge the
    decades of foundational work by <name|Prof. Joris van der Hoeven> and the
    <TeXmacs> contributors.

    <ATHENA> is <strong|not> a conservative distribution of GNU <TeXmacs>. To
    support its knowledge-management features, <ATHENA> introduces
    native UTF-8 text, XML documents, node properties, and incompatible runtime
    behavior. Legacy import remains available for supported content, but
    compatibility with upstream <TeXmacs> is not guaranteed in either
    direction. Back up old vaults and use the offline upgrade tools before
    continuing work in the new document model.

    <item*|Development status>

    <ATHENA> <texmacs-version> is active experimental software. Expect rough edges. Expect
    features to be deeper than their polish. The current build and runtime
    workflow is tested on Linux with Qt; legacy non-Qt GUI backends have been
    removed from the maintained path.
  </description>

  For new users, we recommend \P<hlink|Getting started with
  <ATHENA>|start.en.tm>\Q.

  <\tmdoc-copyright>
    1998\U2026

    2026
  <|tmdoc-copyright>
    <person|Joris van der Hoeven>

    <person|Nuaptan Felix Evalisk>.
  </tmdoc-copyright>

  <tmdoc-license|Permission is granted to copy, distribute and/or modify this
  document under the terms of the GNU Free Documentation License, Version 1.1
  or any later version published by the Free Software Foundation; with no
  Invariant Sections, with no Front-Cover Texts, and with no Back-Cover
  Texts. A copy of the license is included in the section entitled "GNU Free
  Documentation License".>
</body>

<\initial>
  <\collection>
    <associate|font|roman>
    <associate|font-family|tt>
    <associate|math-font|roman>
  </collection>
</initial>
