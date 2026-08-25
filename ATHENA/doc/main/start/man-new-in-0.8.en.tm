<TeXmacs|2.1.4>

<style|tmdoc>

<\body>
  <tmdoc-title|What is new in <ATHENA> 0.8>

  <ATHENA> 0.8 connects mathematical writing more closely to sources and
  reusable knowledge. It introduces vault-native Materials, stable semantic
  Artifact links, handwriting recognition, and a private modern Scheme
  runtime. This page summarizes user-visible changes since version 0.7.

  <section|Materials replace legacy bibliographies>

  Materials are a vault-scoped SQLite catalog for books, articles, chapters,
  theses, reports, slides, and other sources. Every record has a stable UUID,
  typed Zotero-compatible metadata, identifiers, creators, provenance, and
  optional managed attachments. Generic source filenames are replaced by
  readable canonical names when files are copied into the vault.

  The Materials manager supports editing, searching, reviewed metadata
  recognition, deduplication, BibTeX import, and bulk import from personal or
  group Zotero libraries through Zotero's official Local API. Repeated imports
  preserve source identity instead of creating duplicate records.

  Insert citations with <menu|Insert|Material citation> or <verbatim|\?>.
  Citations use UUID-backed <verbatim|tmfs://material> targets, typed locators,
  and per-document CSL styles. Referenced Materials lists combine citations
  discovered in the document with optional manually selected reading. Exact
  native structure survives <LaTeX> export and re-import through
  <verbatim|ATHENA-DATA> records.

  <section|Stable Artifacts and radioactive links>

  Artifact UUIDs now remain stable across incremental edits and safe file
  renames when structural evidence identifies the same mathematical object.
  Exact anchors and unique neighboring context are preferred; ambiguous copies
  receive new identities instead of inheriting the wrong history.

  Ordinary text can become a radioactive link to an indexed Artifact without
  modifying the document tree. Matching is Unicode case-insensitive, handles
  common inflections and mathematical possessives, and recognizes classical
  eponym forms such as <verbatim|Euler>, <verbatim|Euler's>, and
  <verbatim|Eulerian>. Several Artifacts may share one name; in that case the
  link opens a vault-font disambiguation page. Link color is configurable
  separately from ordinary hyperlinks.

  <section|Handwritten mathematical symbols>

  Open <menu|Insert|Handwritten Symbol> to draw with a mouse, touch screen, or
  tablet. The pane recognizes the drawing asynchronously, presents ranked
  native symbol previews, and inserts the selected ATHENA command. Undo, redo,
  Enter, and Escape are supported. Recognition uses the pinned Hand TeX model
  through an in-process ncnn runtime; no network service is required.

  <section|A private Guile 3 runtime>

  <ATHENA> no longer depends on the obsolete Guile 1.8 runtime. The source tree
  includes a private, ATHENA-specific Guile 3 with native implementations of
  module loading, lazy definitions, requirements, and compatibility behavior
  needed by the existing Scheme corpus.

  Scheme modules are compiled to dependency-ordered bytecode during the build.
  Incremental builds recompile only changed modules and their reverse
  dependency closure, using parallel workers and transactional publication.
  The private runtime and its statically linked parallel garbage collector are
  built with ThinLTO and native CPU tuning for interactive throughput.

  <section|Interface, navigation, and startup>

  Heading fold buttons are no longer inserted into document trees. The
  rendering layer draws Mathematica-style cell brackets along the right edge:
  click to select a heading cell and double-click to fold or unfold it.

  Startup and blocking waits use compact native progress windows instead of
  decorative splash artwork. Noncritical startup work is deferred, fonts and
  closest-font decisions are cached, and Scheme compilation is reported
  explicitly when it is needed. <menu|File|Restart ATHENA> replaces the current
  process after the normal unsaved-buffer confirmation, so an installed new
  binary takes effect immediately.

  Namespace definitions, hierarchy, file inventory, and template membership
  are maintained by a background ontology service with a persistent SQLite
  cache. Namespace panes consume immutable snapshots and show indexing status
  instead of freezing the interface during first expansion. Smooth scrolling
  reuses the backing pixmap and repaints exposed strips rather than repeatedly
  redrawing the complete viewport.

  <section|Publishing and platform correctness>

  Generated websites now open canonical document pages directly. A compact
  floating toolbar opens the Vault Explorer, Namespace Explorer, outline,
  Global Search, Quick Switcher, and optional PDF download as focused overlays;
  the old desktop-window and iframe metaphor has been removed.

  HTML export preserves full-width transcluded enunciations. <LaTeX> export
  distinguishes true middle delimiters inside retained
  <verbatim|\left>--<verbatim|\right> pairs from standalone evaluation and
  restriction bars. PDF generation handles slideshow dependencies and compact
  transclusion frames more reliably.

  On native Wayland, <ATHENA> now lets the compositor supply logical scaling
  once, aligns menu popup geometry with painted menubar items, and avoids
  unsupported tooltip and docking mouse-grab paths.

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
