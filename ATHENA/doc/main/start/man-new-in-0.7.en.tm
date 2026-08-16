<TeXmacs|2.1.4>

<style|tmdoc>

<\body>
  <tmdoc-title|What is new in <ATHENA> 0.7>

  <ATHENA> 0.7 focuses on the daily behavior of large mathematical vaults:
  opening long documents, protecting changing data, publishing complete
  websites, and keeping structural editing predictable. This page summarizes
  user-visible changes since version 0.6.

  <section|Long documents become interactive sooner>

  Screen typesetting is now progressive and time-budgeted. <ATHENA> lays out
  enough of a long document to present a usable first viewport, preserves
  estimated geometry for the remainder, and continues work in bounded batches
  after repaint. Paper layout, printing, and export still perform complete
  deterministic typesetting.

  Native <verbatim|.ath> parsing avoids an unnecessary generic conversion
  path. Transcluded source documents and anchors, font metadata, hyphenation
  decisions, and negative TeX lookups are reused instead of recomputed.
  Vault activation likewise reuses SQLite state and performs quick integrity
  checks during ordinary startup.

  On the project's long, transclusion-heavy benchmark document, these changes
  reduced the median warm open-to-first-paint time from approximately 10.7
  seconds to approximately 0.38 seconds. The exact result depends on the vault,
  document, storage, fonts, and machine, but the architectural change applies
  to ordinary long-document editing.

  <section|Vault backup dispatchers>

  A vault may define multiple one-way backup dispatchers under
  <menu|Edit|Preferences|Vault|Backup>. Each dispatcher combines an
  <verbatim|rsync> destination with one trigger: every successful save, the
  completion of Vault Maintenance, or five minutes without user input.

  Dispatchers run asynchronously and coalesce requests instead of starting
  overlapping transfers. They mirror deletion as well as creation, exclude
  ATHENA's backup internals, and reject local destinations that overlap the
  source vault. Pre-save histories are also published atomically, so rapid
  consecutive saves cannot expose a partial compressed snapshot.

  <section|Incremental website publishing>

  Static website generation records source and configuration hashes. Unchanged
  pages are retained, documents affected by source or transclusion changes are
  regenerated, and stale outputs are removed safely.

  A website may optionally include an incrementally generated PDF for every
  exported document. Download controls appear in both the website's document
  viewer and standalone pages. Website definitions can also generate a
  Cloudflare Pages <verbatim|_redirects> file from validated shortcut-to-document
  rules. If a post-generation deployment command fails, it may be rerun from
  the output pane without repeating document export.

  Figure and table caption numbering, block-styled document structures, and
  mathematical minus signs now survive website and LaTeX conversion more
  faithfully.

  <section|Preferences and navigation>

  The native Preferences window has a search field. It indexes categories,
  tabs, section titles, labels, and controls from the actual constructed UI,
  so new settings become searchable automatically. Choosing a result opens the
  correct page, scrolls the setting into view, and focuses its control.
  <key|Ctrl+F> focuses the search field.

  <key|Page Up> and <key|Page Down> navigate Quick Switcher results. Embedded
  previews in Global Search and the wikilink and transclusion inserters now
  scroll and repaint their own document viewport reliably.

  <section|Structural editing and mathematics>

  Cursor and selection bookkeeping now survives more tree joins and structured
  formatting operations. Selections can cross table boundaries, approach the
  far left without jumping to the start of the document, and cover the whole
  document without recentering the viewport. Pinch gestures are routed to the
  document window that actually received them.

  Native commutative diagrams support self-loop arrows with hover, selection,
  creation, and style editing. The finite-part integral is available as
  <verbatim|\fint>. Long generated tables of contents paginate correctly in
  Book layout and PDF output.

  Colored enunciations no longer force broad environment recomputation while
  typing, improving responsiveness in long documents.

  <section|LaTeX import and export>

  The LaTeX importer removes leaked helper definitions, normalizes titles and
  theorem-like environments into native ATHENA structure, and handles common
  LyX and custom-wrapper forms more deliberately. The exporter preserves
  block-styled mathematical structures and avoids emitting internal
  nonconverted-minus placeholders.

  These changes complement the existing <verbatim|ATHENA-DATA> round-trip
  records: native information is preserved structurally where possible and
  serialized explicitly where LaTeX has no equivalent concept.

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
