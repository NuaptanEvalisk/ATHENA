<TeXmacs|2.1.4>

<style|<tuple|tmdoc|english>>

<\body>
  <tmdoc-title|Materials, citations, and referenced lists>

  <ATHENA> Materials are a vault-native replacement for traditional
  <name|BibTeX> bibliography workflows. A Material is a structured record for
  a book, journal article, chapter, thesis, report, conference paper, slide
  deck, web page, or another research source. Every record has a stable UUID,
  typed metadata, creators, identifiers, tags, provenance, and optional
  managed attachments.

  <section|Storage and identity>

  Each vault stores Material metadata in a SQLite database. Its vault-relative
  location is configured by <menu|Edit|Preferences|Vault|Vault Info|Materials
  database path> and defaults to <verbatim|materials.sqlite> at the vault
  root. Managed source files are separate: their folder is configured by
  <menu|Stored materials folder> and defaults to <verbatim|materials/>.

  Dropped files are copied into that folder. <ATHENA> does not retain useless
  source names such as <verbatim|paper2.pdf> or an ISBN followed by
  <verbatim|.pdf>; it derives a readable canonical filename from creator,
  year, title, and extension. The original filename, content hash, MIME type,
  and stored path remain recorded in SQLite.

  A Material UUID remains its identity when metadata or attachment filenames
  change. The address <verbatim|tmfs://material/UUID> opens its Material page.
  Aliases preserve incoming links when duplicate records are merged.

  <section|The Materials manager>

  Open <menu|Workspace|Materials manager>. The left side searches the vault's
  Material records. The right side edits type-specific metadata, creators,
  identifiers, additional fields, tags, and attachments using the bundled,
  pinned Zotero item schema.

  Drop one or more source files onto the landing pad, or use <menu|Add files>.
  <ATHENA> extracts local metadata and text first, recognizes identifiers such
  as DOI, ISBN, arXiv ID, and PMID, and proposes a record for review. Enabled
  external metadata providers may enrich that proposal. Files themselves are
  never sent to those providers.

  File hashes and normalized identifiers are used for deduplication. When a
  duplicate is found, <ATHENA> reuses or merges the canonical record instead
  of creating another independent copy. Imported or uncertain metadata stays
  reviewable before it becomes the authoritative record.

  <menu|Import BibTeX> imports a <verbatim|.bib> library into Materials. This
  is an import path, not the former TeXmacs bibliography subsystem: imported
  entries become ordinary UUID-backed Material records.

  <menu|Import Zotero> connects to the official Zotero Local API and bulk
  imports My Library or a locally available group library. Start Zotero and
  enable <menu|Settings|Advanced|Allow other applications on this computer to
  communicate with Zotero> first. <ATHENA> preserves Zotero item types,
  fields, creator roles, identifiers, tags, collections and relations, and can
  copy locally available file attachments into the vault's managed Materials
  folder. Zotero notes and annotations are not bibliographic Materials and are
  therefore not imported.

  Every imported Zotero record keeps its stable Zotero library identity and
  item key as provenance. Repeating an import, including after restarting
  Zotero, is consequently idempotent. Strong
  identifiers such as DOI and ISBN also match existing Materials, so importing
  a Zotero library does not create a second record for a source already entered
  by another route.

  <section|Citing a Material>

  Use <menu|Insert|Material citation>. Search for one or more Materials and,
  when needed, specify a locator such as page, chapter, section, paragraph,
  figure, table, volume, or issue. A citation stores the Material UUID and
  locator structurally; its rendered text is derived from the selected CSL
  style. A direct target can therefore take a form such as
  <verbatim|tmfs://material/UUID?locator=page&value=42> without making the
  human-readable citation its identity.

  Use <menu|Insert|Referenced Materials> to insert the document's referenced
  Materials list. Its automatic part is collected from Material citations in
  the document. You may also append uncited records manually from its
  <menu|Focus> menu, for example for background reading. The list stores the
  manually included UUIDs as document structure. It normally inherits the
  document Citation Style; an individual list may select an explicit override
  from its <menu|Focus|Citation style override> menu.

  Use <menu|Document|Update|Referenced Materials>, or
  <menu|Document|Update|All>, after citations or Material metadata change.
  Updating resolves aliases, rerenders citation clusters, and rebuilds every
  referenced Materials list.

  <section|CSL styles and metadata providers>

  The default CSL style and provider choices are configured under
  <menu|Edit|Preferences|Vault|Materials>. CSL rendering and BibTeX import use
  the bundled <name|Hayagriva> engine. The default style is
  <verbatim|springer-mathphys>. Preferences provides a searchable menu of the
  independent CSL styles bundled by the engine. A document can override the
  default through <menu|Document|Citation Style>; this setting is stored in the
  document and governs its citations and inherited referenced-Materials lists.
  Choose <menu|Use Preferences default> there to remove the document override.
  Legacy BibTeX style names, including <verbatim|ams>, are not accepted as CSL
  styles.

  Crossref and OpenAlex enrich DOI records; Open Library and Google Books
  enrich ISBN records; arXiv enriches arXiv records; and PubMed enriches PMID
  records. Open Library is enabled by default, while the remaining providers
  are disabled by default. A contact email can be supplied for services whose
  responsible-use policy requests one. Local extractor command names are
  configurable separately.

  <section|LaTeX interoperability>

  A normal LaTeX citation or reference-list rendering cannot represent a
  vault UUID, managed attachment, manual inclusion, or all Material metadata.
  Consequently, <ATHENA> exports useful visible LaTeX while also embedding the
  exact Material citation and referenced-list AST in versioned
  <verbatim|ATHENA-DATA> records. The visible fallback is skipped when that
  LaTeX is imported back into <ATHENA>, and the original structure is injected
  instead. This preserves Materials across a single-file
  <verbatim|.ath -\<gtr\> .tex -\<gtr\> .ath> round trip.

  <tmdoc-copyright|2026|Nuaptan Felix Evalisk>

  <tmdoc-license|Permission is granted to copy, distribute and/or modify this
  document under the terms of the GNU Free Documentation License, Version 1.1
  or any later version published by the Free Software Foundation; with no
  Invariant Sections, with no Front-Cover Texts, and with no Back-Cover
  Texts. A copy of the license is included in the section entitled "GNU Free
  Documentation License".>
</body>
