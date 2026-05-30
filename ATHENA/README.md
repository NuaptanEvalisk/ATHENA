# ATHENA

<p align="center">
  <img src="misc/images/icon_fullsize.png" alt="ATHENA icon" width="160">
</p>

**Advanced Typesetting and Hypertext Environment for Notes and Archives**

ATHENA is a mathematics-centered knowledge work environment built from GNU
TeXmacs. It combines high-quality structured WYSIWYG typesetting with vaults,
wikilinks, transclusions, namespaces, fast search, and import tooling for
large Obsidian-style mathematical note collections.

![ATHENA screenshot](../screenshot.png)

## Warning: ATHENA Is Not For Everyone

Use ATHENA only if you are disappointed with **both LaTeX and Obsidian**.

ATHENA is not a LaTeX editor, not an Obsidian clone, and not a conservative
TeXmacs distribution. It is an experimental research notebook and mathematical
knowledge infrastructure. It assumes that you want semantic mathematical
documents, native typesetting, cross-document structure, vault-wide operations,
and deep customization more than you want broad ecosystem compatibility.

If plain Markdown is enough, use Obsidian. If batch typesetting and source-text
control are enough, use LaTeX. ATHENA exists for the uncomfortable middle:
interactive mathematical writing at scale.

## What ATHENA Is

ATHENA is a fork of GNU TeXmacs with major changes:

- The application and runtime tree are rebranded as ATHENA.
- The default document format uses `.ath`.
- The user configuration directory is `~/.ATHENA`.
- The executable is `ATHENA.bin`.
- ATHENA introduces AST nodes and runtime behavior that upstream TeXmacs does
  not understand.

ATHENA can still benefit from TeXmacs' core strengths: structured documents,
native mathematical editing, high-quality layout, TeX-style fonts, Guile/Scheme
extension support, and a deep style system. The project also inherits decades
of work by Joris van der Hoeven and the GNU TeXmacs contributors.

## Major Features

### Vaults

ATHENA vaults are self-contained mathematical knowledge bases.

- Load and track a current vault through `Vaultfile`.
- Use `.ath` as the primary ATHENA document format.
- Keep vault-scoped preferences in addition to global preferences.
- Open recent vaults and optionally auto-open the startup vault/buffer.
- Browse vault contents in native Qt ADS panes.
- Use a vault quick switcher, command palette, outline pane, backup viewer, and
  error messages pane.
- Persist ADS pane layout across sessions.

### Wikilinks

ATHENA supports Obsidian-like links adapted to structured mathematical
documents.

- Link to files and anchors through stable UUID-backed mappings.
- Insert wikilinks through a Qt wizard.
- Locate targets by choosing a file first or by vault-wide search.
- Preview target context using an embedded rendered ATHENA preview.
- Filter link search by namespace and enunciation type.
- Repair and resolve links when filenames or anchors move.
- Render card links for files, PDFs, external links, and TMFS destinations.

### Transclusions

ATHENA can embed content from another document into the current one.

- Transclude theorem-like enunciations.
- Transclude arbitrary ranges between anchors.
- Use preview-backed Qt insertion workflows.
- Jump from a transclusion back to its source.
- Detect cycles to avoid recursive rendering failures.

The arbitrary-anchor transclusion path currently has a documented known issue:
in some cases closing the preview wizard can produce a nonfatal segmentation
fault popup after the transclusion has already inserted correctly.

### Namespaces

Namespaces are one of ATHENA's central knowledge-organization features. They
classify files by filename templates rather than filesystem folders.

Namespace support includes:

- Abstract, semi-concrete, and concrete namespaces.
- SQLite-backed namespace persistence inside vaults.
- Template derivation checks for parent/subspace inference.
- Explicit and derived parent relationships.
- Concrete namespace style paths and initial-content templates.
- Namespace manager ADS pane with wizard-based creation/editing.
- Namespace explorer ADS pane.
- Namespace TMFS pages: `tmfs://ns/name`.
- Technical summaries via `tmfs://ns/!name`.
- Optional namespace homepages written as `.ath` documents.
- Dynamic homepage tags such as namespace name, type, parents, children,
  filename template, sorter, matches, and technical-summary link.
- Namespace-aware quick switcher.
- Namespace-aware global search.
- Namespace-aware file creation.
- Custom C sorters compiled with libtcc.
- Built-in trivial sorting behavior.
- Generated sub-product namespaces and product sorters.
- Reverse hierarchy graph panes and insertable reverse hierarchy diagrams.
- Namespace export to a book-style document with cover metadata, table of
  contents, selected hierarchy diagrams, optional reverse hierarchy context,
  DataArt covers, rewritten internal wikilinks, and working PDF jumps.
- A dedicated namespace manual under Help -> Manual.

### Search

ATHENA has a rendered, occurrence-level global search pane.

- Search entire vaults or restrict to a namespace.
- Restrict hits to a chosen enunciation type, or search without that
  requirement.
- Show individual occurrences rather than just filenames.
- Preview hit neighborhoods in a rendered read-only ATHENA buffer.
- Navigate by double-clicking an occurrence.
- Preserve preview width, document zoom, and ADS pane behavior.
- Use improved fuzzy ranking for vault and namespace search workflows.

### Editing Workflow

ATHENA adds editing modes and feedback aimed at large mathematical notes.

- Live spell checking.
- Typewriter mode for reflow-oriented editing.
- Configurable editor focus boxes and marked-space rendering.
- Scroll and cursor preservation across resize and automatic anchoring.
- Unsaved-buffer listing before quit.

### Mathematical Input

ATHENA heavily customizes the math typing experience.

- Mathematica-style shortcuts for common structures:
  - `Ctrl+/` fraction
  - `Ctrl+2` square root
  - `Ctrl+9` text in math
  - `Ctrl+6` superscript
  - `Ctrl+-` subscript
  - `Ctrl+7` overscript
  - `Ctrl+4` underscript
  - `Ctrl+Space` jump out
  - `Ctrl+>` and `Ctrl+.` enlarge structural selection
- Mathematica-style table editing shortcuts:
  - `Ctrl+,` insert column right
  - `Ctrl+Shift+,` insert column left
  - `Ctrl+Enter` insert row below
  - `Ctrl+Shift+Enter` insert row above
- ESC symbol picker at the cursor.
- Data-driven ESC symbol picker table loaded from
  `ATHENA/misc/input/escape-symbol-picker.json`.
- ESC aliases for Greek letters, blackboard symbols, operators, arrows,
  brackets, norm bars, math fonts, limits, group names, and common snippets.
- Additional backslash aliases for theorem-like environments.
- Extended textual math operators, including algebra/category/geometry names
  such as `Hom`, `Aut`, `Spec`, `coker`, `rank`, `trdeg`, and `rel`.
- Correct support for symbols such as `varinjlim`, `varprojlim`, degree,
  `mathscr`, upright `mathrm`, and boldsymbol-style input.

### Enunciations

ATHENA treats theorem-like environments as first-class document structure.

- Extended enunciation set: theorem, lemma, definition, proposition, corollary,
  conjecture, axiom, question, example, remark, caution, disambiguation,
  solution, proof, alternative proof, and more.
- Configurable rendering colors and presets.
- Solution rendering fixed to behave like remarks/proofs rather than exercise
  indentation.
- CJK line breaking in solution and enunciation contexts.
- Automatic enunciation anchoring on save or during maintenance.
- Native confirmation dialog for planned enunciation anchor changes.
- Display-first enunciation titles are kept left-aligned while display
  formulas remain centered.

### Obsidian / AOFM Conversion

ATHENA includes a converter for importing Obsidian-style mathematical vaults.

The converter supports:

- AOFM single-file and vault conversion command-line modes.
- Vaultfile generation.
- Model vault import for preferences and namespace data.
- Wikilink and transclusion conversion.
- Anchor generation and proof pairing.
- Callout-to-enunciation mapping.
- Markdown highlights and external links.
- Tables, images, SVG rendering through resvg, PDF links, and card links.
- Obsidian image width conversion.
- Optional generated titles, build warnings, and table of contents.
- Formula normalization, including semantic differential `d`, blackboard `i`,
  textual operator recognition, matrices, cases, aligned equations, integrals,
  limits, and delimiter cleanup.

### PDF Export And Covers

ATHENA can generate richer PDF output than plain converted notes.

- DataArt cover image generation for PDF export/preview.
- Namespace export with cover page, table of contents, hierarchy diagram,
  imported source documents, generated labels, and internal PDF jumps.
- A uv-managed Python/matplotlib generator in `ATHENA/tools/data-art`.
- Generated cover images live in temporary storage and are not saved to the
  vault.
- Correct PDF destination emission for labels, wikilinks, tables of contents,
  and exported namespace documents.
- Optional build warnings and automatic table of contents for converted
  documents.
- Reverse-video mode is confined to document rendering, including pictures,
  cursor, and selection, instead of pretending to be a GUI dark mode.

### Vault Maintenance

ATHENA has headless vault maintenance support.

- Create zstd-compressed full backups.
- Limit the number of retained full backups.
- Preserve or purge pre-save histories by configured duration.
- Collect orphan assets into an `orphan/` directory with an `orphans.lst` map.
- Run enunciation anchoring across the whole vault.
- Report progress and maintenance summaries.

### UI And Native Qt Work

ATHENA has moved much of the knowledge-work interface into native Qt.

- Qt Advanced Docking System panes.
- Vault Explorer.
- Namespace Manager.
- Namespace Explorer.
- Reverse Hierarchy Graph pane.
- Global Search.
- Error Messages pane.
- Custom Styles Manager.
- Backup Viewer.
- Command Palette.
- Visual Studio-style buffer switcher.
- Native Qt dialogs for file selection, color picking, information messages,
  font selection, wikilink insertion, transclusion insertion, and namespace
  workflows.
- Startup splash progress reporting from real startup phases.
- Reliable text toolbar dropdowns for document style, theme, font, and font
  size.

### Performance And Stability Work

Recent ATHENA work includes substantial low-level engineering:

- mimalloc integration and global allocation overrides.
- Ref-counting and tree/string/list performance improvements.
- Move semantics for core tree/string structures.
- Large-document stack and parser fixes.
- Font discovery caching.
- Startup warming for font-menu probes to avoid first-open font dropdown lag.
- Cache invalidation when Scheme/package sources change.
- Shared-memory backed runtime temporary files.
- PDF/export fallback font safety.
- Resizable and reopenable ADS panes.
- Crash reporting through native dialogs.

## Build And Run

ATHENA development is currently tested on Linux. The inherited Windows and
macOS paths exist, but the ATHENA build process is not tested there.

See [COMPILE](../COMPILE) for dependency installation and build instructions.
The recommended compiler is Intel oneAPI `icpx`.

After building from the repository root, copy the executable into this runtime tree before running:

```bash
cp -f ../build/src/ATHENA.bin bin/ATHENA.bin
./StartATHENA.sh
```

## Compatibility

ATHENA is based on TeXmacs, but ATHENA documents and vault features are not
guaranteed to be readable by upstream TeXmacs. The reverse direction is more
likely to work: ATHENA can often open TeXmacs documents, but once ATHENA
features are used, the document may become ATHENA-specific.

## Status

ATHENA 0.1 is an active experimental system. It is powerful, opinionated, and
still changing quickly. Expect rough edges. Expect features to be deeper than
their polish. Expect the best experience on the developer's Linux setup.

## Licensing

ATHENA is free software under the GNU General Public License, version 3 or
later. See [LICENSE](../LICENSE), [COPYING](../COPYING), and
[ATHENA/COPYING](./COPYING).

Copyright (C) 1998-2026 Joris van der Hoeven and others.

Copyright (C) 2026 Nuaptan Felix Evalisk.
