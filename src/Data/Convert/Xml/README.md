# UTF-8 Document Storage

## Activation Status

These APIs are migration infrastructure, **not the active document format**.
The editor, bundled resources, legacy converters and protocols still use Cork.
Do not pass a legacy runtime tree to `write_xml`, or pass `read_xml` output to a
Cork editor. ASCII-only tests do not establish that those crossings are safe.
No setting enables a mixed runtime. Normal saves must remain on the existing
path until the text, symbol, position, persistence and protocol migrations are
complete and accepted together.

Native document constructors no longer add a TeXmacs compatibility version.
Until XML activation, only the legacy document serializers add their required
`TeXmacs 2.1.4` format signature to a temporary serialization tree. This is not
a configurable compatibility promise, and generic tree serialization remains
unchanged. Legacy readers still recognize existing file headers.

## Text Contract

- The existing native `tree` and `string` containers remain in use. Ordinary
  atoms and tag identities passed to this codec are strictly valid UTF-8.
- `RAW_DATA` has exactly one atomic **byte** payload. Its bytes are not text and
  are never validated or transcoded as text. Other opaque native objects are
  not serializable document values.
- UTF-8 byte offsets identify native text positions. Codepoint/scalar and
  UTF-16 indices require explicit conversion with `unicode_text.hpp`.
- Editing boundaries are ICU extended grapheme clusters, not bytes or
  codepoints. `grapheme_cursor` borrows immutable bytes, owns its ICU iterator,
  and must be rebound after the text revision changes. No normalization is
  implicit in validation, conversion, segmentation or serialization.
- `unicode_paragraph` owns ICU bidi state and its required UTF-16 copy while
  borrowing immutable UTF-8 source. A scalar-boundary index maps both directions
  without rescanning prefixes; surrogate interiors are rejected. ICU resolves
  a paragraph once and each selected line separately, including UBA trailing
  whitespace reset. UText line breaking returns UTF-8 opportunities and hard
  breaks, filtered through ICU grapheme boundaries. One analysis accepts one
  Unicode paragraph, enforces an input budget and rejects lines crossing a hard
  break. Source bytes, isolates and directional controls are never rewritten.
  Script itemization uses Pango ScriptIter (LGPL-2.0-or-later, dynamically linked),
  not a handwritten Common/Inherited/paired-punctuation table. ICU does not expose
  an equivalent public script-run iterator. Pango is a required development
  dependency; this integration uses no GTK widgets, font map or GUI ownership.
  Visual bidi runs intersect logical script ranges to produce ISO 15924 shaping
  items. These are scalar ranges, not promises of editing caret boundaries;
  paragraph caret placement retains ICU graphemes across item boundaries via
  `shape_line`: it places shaped items in visual order while keeping absolute
  byte positions and upstream/downstream caret affinity at bidi boundaries.
  Shaping-only fragment edges are discarded, never published as editing stops.
  Each line borrows the analyzed source and restricts joining context to its
  chosen boundaries. Glyph/caret budgets apply across the entire line. Font
  selection is supplied explicitly by the caller; missing glyphs remain visible
  in the result rather than being silently treated as successful fallback.
  `font_paragraph` supplies Pango/Fontconfig fallback and subdivides these items
  at physical font changes, reversing font subitems within RTL runs. Font
  itemization is done once per immutable paragraph, not once per wrapped line.
  ICU remains the authority for bidi levels and editing stops. The existing
  paragraph formatter still needs integration.
- Physical Unicode fonts expose `shape_utf8` separately from their legacy
  encoded-string methods. HarfBuzz (the existing MIT-licensed dependency) is
  the shaping engine, not a second handwritten ligature/mark parser. It returns
  positioned glyph ids and absolute UTF-8 byte clusters; `<alpha>` is literal
  text. OpenType design metrics avoid dependence on another font's mutable
  FreeType size/charmap. Immutable shaping fonts are cached in the owning font
  domain and released before their FreeType faces; mutable shaping buffers are
  private to each call. Glyph clusters are not editing boundaries: ICU remains
  the caret/deletion authority. Runs belong to their font domain, and renderer
  recording must consume them there. This is a single-font, homogeneous-script
  shaping primitive; paragraph bidi, smart-font fallback and math symbol
  dispatch still require integration before runtime activation.
  Physical shaping also accepts an explicit absolute UTF-8 filename and
  FreeType face index. Collection faces share owner-local immutable file bytes,
  but retain independent mutable FreeType faces, glyph caches and HarfBuzz
  named-instance settings. Explicit variable-font design coordinates are kept
  in axis order as OpenType 16.16 values and applied to both FreeType and
  HarfBuzz; they participate in cache identity and export metadata. Export
  receives the same file/index/size descriptor,
  not a filename or font size guessed by parsing a resource name. Native PDF
  embeds the selected collection member; unsupported named-instance or
  anisotropic embedding uses its actual rasterized glyphs instead of silently
  embedding a different outline. Tiny original TTC/variable-font fixtures cover
  indices, instance isolation, Unicode filenames and native embedding. Their
  checked-in bytes are regenerated with fontTools only for fixture maintenance;
  the font tests do not invoke Python or fontTools.
  PangoFT2 (LGPL-2.0-or-later) selects physical font spans through a private
  Fontconfig configuration and font map in each font domain. It includes the
  existing application/imported font roots without changing the process-global
  Fontconfig configuration or sharing Qt font objects. Tests use an isolated
  catalog whose collection faces have disjoint coverage, including font changes
  within Greek and RTL Hebrew runs. Pango's selected variation coordinates are
  copied from its immutable HarfBuzz font, not parsed with a second variation
  syntax implementation. Embedded NUL bytes are retained as separate control
  runs across Pango's C-string boundary. Synthetic font transforms currently
  report an explicit unsupported error rather than silently discarding the
  requested style; synthesis and color-font rendering remain activation work.
  Integration contracts: [Pango itemization](https://docs.gtk.org/Pango/func.itemize_with_base_dir.html),
  [private Fontconfig configuration](https://docs.gtk.org/PangoFc/method.FontMap.set_config.html),
  [immutable HarfBuzz font access](https://docs.gtk.org/Pango/method.Font.get_hb_font.html).
  Shaping context can be restricted to a chosen line without copying or
  renumbering the source atom; HarfBuzz receives only that line's surrounding
  text and beginning/end flags. Returned clusters and carets remain absolute
  source byte offsets, even when a wrapped line starts inside an atom.
- Editable shaping runs retain ICU grapheme stops with absolute byte offsets
  and physical caret coordinates. HarfBuzz GDEF supplies ligature carets when
  present; otherwise cluster advance is divided among its graphemes, not bytes
  or combining codepoints. RTL keeps logical byte order and reversed physical
  positions. Hit testing and caret lookup use the same shaped geometry.
  `utf8_text_box` connects these runs to native box display, cursor, selection
  and source-path interfaces. It retains a COW source atom with joining context,
  rather than copying substrings for every caret or shaping a prefix on each
  mouse move. Its explicit UTF-8 factory is not selected by the legacy concater:
  paragraph itemization, microtypography and mathematical glyph corrections
  must migrate before that cutover. Font-domain rendering and PDF tests exercise
  the box directly, including destruction after recording and source COW edits.
- Shaped drawing borrows the original UTF-8 input, passing the selected range
  separately from glyph ids to the renderer. The native PDF renderer emits
  Unicode cluster mappings and per-run ActualText (PDF 1.5), preserving original
  ligature spelling and combining sequences even when they share a font glyph.
  This also covers bitmap Type 3 fallback; no glyph id is cast to Unicode. The
  PDF 1.4 output is promoted through the catalog version when it uses this
  feature; version state belongs to each renderer. The legacy drawing entry
  remains separate until the runtime switch. PostScript carries the same
  source through ActualText pdfmark spans for PDF conversion. Both paths use
  Qt's UTF-16BE encoder for PDF text strings, never the Cork converter.
  Editor/PDF end-to-end integration remains migration work.
  `shaped_pdf_test` checks native and Type 3 CMaps, exact Poppler extraction,
  effective PDF version, qpdf structure and rendered pixels for both native
  PDF and Ghostscript-converted PostScript. The bitmap PostScript prologue
  accumulates real glyph bounds instead of declaring a zero FontBBox.
- The codec does not interpret `<...>` inside text. Structural symbols are a
  separate tree-model concern; the old incomplete-input `SYMBOL` is not a
  substitute for the planned `named-symbol` representation.

## XML Version 1

The root is `athena-document` for a full `DOCUMENT`, or `athena-tree` for a
fragment, with required attributes `version="1"` and `text-model="utf-8"`.
The envelope contains exactly one tree. The format version is independent of
the ATHENA program version. Legacy top-level `(TeXmacs "...")` version
metadata is discarded during upgrade; other source metadata remains in the tree.
`strip_legacy_document_version` performs this explicit shallow transformation
and can return the root-child index mapping (`-1` for removed metadata).
It never removes a nested TeXmacs macro or logo from document content. Full XML
document reads/writes reject the obsolete version field instead of retaining a
TeXmacs compatibility claim. Tree fragments remain generic and lossless.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<athena-document version="1" text-model="utf-8">
  <node tag="document">
    <text></text>
    <node tag="custom-macro"><text>argument</text><text/></node>
    <node tag="raw-data"><bytes encoding="base64">AP8=</bytes></node>
  </node>
</athena-document>
```

`node/@tag` holds the tag identity, independent of XML element-name rules.
Child order, empty text, zero-argument compounds, unknown tags and atom whitespace
are significant. Formatting whitespace *between* tree elements is not content.
The writer does not pretty-print or expand macros.

Text containing XML 1.0-forbidden characters or CR uses
`<text encoding="base64-utf8">`. Decoded content must still be valid UTF-8.
Tags requiring the same escape (including attribute-normalized whitespace) use
`tag-encoding="base64-utf8"`. Binary `bytes` requires `encoding="base64"` and is
only valid as the single child of `raw-data`. Base64 must be canonical, without
whitespace or omitted padding. These are explicit representations, never
encoding guesses or fallbacks for invalid UTF-8.

Parsing uses local QXmlStreamReader instances. DTDs, unresolved entities,
namespaces, unknown structural attributes, unsupported format/text-model
versions, malformed UTF-8/XML and unexpected content are rejected. There is no
external entity resolver. Predefined XML escapes such as `&lt;` are supported.
Limits bound source bytes, output bytes, decoded tag/text/payload bytes, tree
nodes and tree depth. Syntax diagnostics include Qt's line, column and XML
character offset; the latter is diagnostic metadata, not an editor byte offset.

QXmlStreamReader/Writer was selected over pugixml (MIT) because Qt Core is
already required and a second DOM is unnecessary. Unicode segmentation uses
ICU UText and BreakIterator, not locally approximated Unicode rules.

## Legacy Import

`import_legacy_document_bytes` recognizes the legacy markup/S-expression
signatures and uses bounded, read-only entry points into the existing readers.
It rejects malformed delimiters, incomplete strings/lists, invalid hex payloads,
trailing data and depth/parse-budget violations. No Guile evaluation is involved.

`legacy_cork_table` loads only the canonical byte and named-character tables,
not the lossy `*-oneway`, fallback or math-export substitutions. It does not
guess whether old bytes happen to be valid UTF-8. Unicode character escapes
become scalars; unrepresentable glyphs become `named-symbol` nodes with
`texmacs:` or `cork:` identities. There is no normalization or macro expansion.

Import distinguishes presentation content, identity strings, scalar fields,
code and raw bytes. Standard DRD child types provide the built-in slot policy;
callers can supply explicit policies for application/custom macro fields.
Unknown glyphs in a scalar/code field cause a diagnostic rather than a lossy
substitution. RAW_DATA remains byte-for-byte binary. These policies still need
integration with all application metadata and style-defined macro contracts
before opening arbitrary migrated trees in the live editor.

The result includes exact node relocation and text spans with preceding/following
affinity at structural splits. ASCII runs map interior positions linearly;
interior bytes of old character tokens are rejected. Removed metadata has no
destination. Maps are bounded separately from text and node counts. This
mapping is not yet connected to database migrations or live editing.

## Upgrade Storage Transaction

`legacy_file::capture` pins a legacy file and retains its original bytes,
revision and SHA-256 without writing anything. Its format check uses the
existing legacy reader signatures, not UTF-8 plausibility; it does not replace
the importer's syntax or semantic checks.

`commit` accepts an already migrated, caller-owned UTF-8 document. It strips
the obsolete top-level version metadata without changing the input, verifies
an XML round trip, preserves and verifies the original, then calls the existing
descriptor-backed atomic replacement with the captured revision. Vault backups
use `.backup/format-migration/v1/<sha256>/<relative-source-path>`. Files outside
the supplied vault use `<filename>.pre-utf8-<sha256>` alongside the original.
No backup is created merely by opening or reading the source.

The filesystem `preserve` primitive creates missing backup directories privately,
refuses symlinks, writes/fsyncs an unnamed temporary file, and links it without
overwriting an existing backup. An interrupted attempt can reuse an existing
backup only after exact byte verification. The file and directory chain are
fsynced before the source may be replaced. File write permissions and source
revision changes are checked during replacement. These optimistic revision
checks detect external changes, but are not a kernel compare-and-swap against
uncooperative writers racing the final rename.

Pre-commit exceptions leave the original in place. If replacement succeeds but
its parent directory cannot be synced, the result is `replaced_not_durable`,
not a false claim of either success or rollback. Result preparation happens
before rename; the result contains a pinned replacement entry, which callers
can inspect without confusing a later pathname replacement with their write.
It also returns the root-child mapping caused by metadata removal. This is not
a replacement for the importer's complete text-offset and tree-path mapping.

This transaction is **not wired into normal saves or maintenance**. It cannot
be enabled independently of the database/position migrations below. Tests cover
legacy markup/Scheme captures, vault and sidecar backup placement, repeat
attempts, corrupt/blocked backups, bounded serialization, read-only originals
and stale source revisions. Disk-full and post-rename fsync fault injection,
batch cancellation/resume and database recovery are still integration work.

## Remaining Integration Gates

The paragraph-level PDF regression currently exposes a PostScript conversion
defect: Ghostscript places a graphics-state restore before the closing
ActualText mark; Poppler consequently extracts wrong text bounds and separates
adjacent script items into different lines. Native PDF extraction and geometry
remain covered independently. The failing PostScript assertion is retained;
single-span text extraction alone is not sufficient verification of that path.

1. Integrate the role-aware legacy importer with application metadata and
   style-defined macro contracts, and register/render `named-symbol` identities.
   The old `Strict-Cork` converter is **not** a substitute for this importer.
2. All editor/parser/font/IME/Guile/Qt boundaries, bundled resources and undo
   paths must agree on the UTF-8 model before these trees become live.
3. AUDMAP, SDK, delegates and caches need explicit model/protocol versions.
4. Database migrations must preserve identities, decisions and vectors,
   relocate ranges explicitly, and distinguish format rewrites from logical
   content changes before any normal-save or maintenance upgrade is enabled.
5. Route all document consumers and writers through the common codec; opening,
   previewing and indexing must never upgrade files in place.

Production Notes and remote backends are not test inputs. Tests use synthetic
trees and isolated temporary directories. No deployment or database migration
is performed by the codec tests.
