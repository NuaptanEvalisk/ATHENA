# UTF-8 Document Storage

## Activation Status

These APIs are migration infrastructure, **not the active document format**.
Some mathematical/editor paths, bundled resources, legacy converters and
protocols still use Cork; the migrated text/editing boundaries below do not.
Do not pass a legacy runtime tree to `write_xml`, or pass `read_xml` output to a
Cork editor. ASCII-only tests do not establish that those crossings are safe.
No setting enables a mixed runtime. Normal saves must remain on the existing
path until the text, symbol, position, persistence and protocol migrations are
complete and accepted together.

The migration branch now routes native tree cursor validation/traversal, editor
Delete/Backspace and selection endpoints through UTF-8 ICU grapheme boundaries.
Those entry points require UTF-8, not Cork; the remaining input, resource and
typesetting paths must be converted before deploying this intermediate build.
`utf8_editor_test` exercises native editor transactions and undo/redo with UTF-8
atoms. Qt key text and IME commits/preedit now enter as UTF-8; preedit cursor
positions explicitly convert UTF-16 to bytes and snap to ICU grapheme stops.
Shift-key preferences retain complete UTF-8 strings under a new key namespace,
and generic keyboard declarations specify UTF-8. Legacy mathematical keymaps
and named-key fallback still need conversion.

The native Guile bridge now uses strict UTF-8 string/symbol/keyword APIs in both
directions, without Latin-1-range heuristics. Binary values use bytevectors,
including generated `bytes` bindings, Base64 payloads, binary file IO and the
single child of `raw-data` in Scheme content. Native containers remain unchanged.
`utf8-text->bytes` / `utf8-bytes->text` explicitly encode/decode UTF-8. Guile
string indices remain codepoint indices; byte length, substring and bidirectional
index conversion have explicit native APIs. Core content ranges and cursor
queries use byte offsets; Scheme character navigation uses ICU graphemes. The
remaining Scheme consumers, legacy converters and resource data still require
migration before runtime activation.

Qt's general string bridge now converts strictly between native UTF-8 and
QString UTF-16, preserves embedded NULs and rejects malformed UTF-8 or isolated
surrogates. It no longer guesses Cork from byte contents or interprets literal
angle-bracket strings. Qt font text, menu labels and preview/export image paths
use the same contract.

Native cross-process clipboard offers use `application/x-athena-selection+xml`,
carrying a versioned XML fragment containing content, mode and language. Binary
RAW_DATA stays binary through the codec. Invalid native offers are rejected,
not silently pasted as their plain-text alternative. Legacy TeXmacs clipboard
MIME is no longer emitted or interpreted as a native UTF-8 tree; external apps
can exchange plain text. The ordinary text clipboard bypasses legacy snippet
parsers and locale/language rewrites; insertion still runs on the editor owner.
Verbatim conversion uses UTF-8 internally, ICU grapheme counting and deletion,
and owner-local ICU converters with stop-on-error callbacks for explicitly
selected external file encodings. `auto` now means UTF-8, not byte guessing.
HTML/LaTeX converter internals and other serialized clipboard consumers remain
part of the broader converter migration; this does not activate document saves.

Hunspell now receives original UTF-8 words, without Cork decoding or legacy
case rewriting. Dictionary-specific casing stays in Hunspell; its declared
external encoding is converted with per-call state and checked for exact
representability. Unrepresentable words never alias a replacement `?`, and
personal words remain UTF-8 even when the installed dictionary cannot encode
them. Suggestions return UTF-8 with the count expected by interactive spelling.
ICU word segmentation supplies byte ranges for manual and live checking.
Live traversal retains viewport priority, word/node/time budgets and bounded
4 KiB text windows, carrying incomplete edge words forward and skipping words
longer than the existing 512-byte dictionary-input budget. Symbol identities
and binary payloads are not spellchecked, including in source access mode.

Native tree search and document replacement now use ICU full case folding with
fine-grained Edits mappings back to original UTF-8 byte positions. Search-only
folding never rewrites the source. Matches must start and end on original
grapheme boundaries and cannot consume half a case-fold expansion: `ss` can
match a whole sharp-s, but a single `s` cannot select half of it. There is no
implicit NFC/NFKC normalization. Structural wildcard matching shares the same
mapped leaf view instead of repeatedly lowercasing entire atoms; native search
navigation no longer parses angle-bracket spellings. Binary payloads and named
symbol identities remain exact structural values rather than ordinary searched
text. Focused native editor tests exercise length-changing replacement and undo.

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
  Sorted, nonoverlapping `font_style_span` byte ranges provide font family,
  size and language overrides through Pango attributes on the same source.
  Gaps retain the base request; paragraph direction cannot change inside a
  span. Effective horizontal/vertical DPI may vary for font-profile adjustments:
  Pango descriptions express the corresponding size in the shared font map,
  while native shaping retains each run's original point size and DPI. Spans
  may meet inside a grapheme without introducing editing stops. Selected
  physical runs retain their own size, device scale and language
  through shaping, wrapping, expansion, raster recording and native PDF export.
  Identical neighboring requests are coalesced so redundant style markup does
  not split ligatures. Span validation rejects overlaps, scalar interiors and
  excessive declarations; configuration is consumed once, not borrowed by lines.
  ICU remains the authority for bidi levels and editing stops. Paragraph
  integration and its remaining limitations are described below.
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
  requested style. Fixed-strike fonts now decode FreeType BGRA/gray/mono pixels
  into owner-local bounded caches and preserve requested layout dimensions,
  including independent horizontal scaling. Font selection recognizes only
  verified fixed-strike normalization, allowing one Pango device-size quantum.
  Qt records owned image pixels; native PDF emits color images plus an empty
  Type 3 text carrier for exact ActualText extraction and run geometry.
  Synthetic outline transforms and other color-font formats remain work.
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
  `utf8_line_box` connects the multi-font paragraph result to those box APIs.
  Wrapped lines share immutable paragraph source and selected fonts. Local box
  paths retain both byte and caret affinity, whereas document tree paths retain
  only the absolute byte. Visual selections are unions of selected run intervals,
  not a single rectangle spanning unselected bidi text. Scroll traversal and
  symbol/shorter modifiers distinguish structural child indices from the leaf's
  position suffix. Tests cover nested boxes, clipping positions, expansion and
  font-domain-independent recorded pixels. Native editor cursors retain visual
  affinity separately from the source tree path. Clicks, cursor copies,
  retypesetting and physical-movement comparisons preserve this state, and
  composite box queries route it to the terminal position without treating it
  as a child index. Logical moves to a different source position start with
  downstream affinity. Source fragments expose which logical sides they contain;
  composites use that contract to choose the previous/next wrapped line or inline
  fragment at a shared endpoint, without assuming visual child order is logical
  order. Opaque/legacy positions do not claim this contract. This is not yet
  whole-document bidi navigation: inline objects, cross-atom line reordering
  and undo/position persistence still need integration.
  The actual text-mode concater now instantiates Unicode line boxes, deriving
  font metadata and effective size/resolution from the profile-selected physical
  font (including the existing monospace x-height adjustment). Atomic text shares
  one paragraph analysis across its fragments; ICU supplies candidate line breaks
  instead of the Cork language scanner. Rigid unlinked atomic text is shaped as
  one line. Rigid concatenations now also join adjacent compatible text and
  explicit source markers into one styled Unicode paragraph: source spans retain
  node paths and byte offsets, and caret paths retain the source-span identity at
  shared endpoints. Font-style boundaries preserve joining context; ICU computes
  bidi order and grapheme stops across source nodes. Foreground/background spans
  retain separate drawing runs without restarting paragraph analysis. Backgrounds
  are painted before glyphs, including reordered RTL runs and justified spaces.
  Transparent direct-link and locus wrappers expose semantic references, ids and
  anchors through an explicit typesetter interface. Joined lines retain visual
  hit regions, locus outlines, page-number collection and PDF annotations; their
  geometry follows justification and glyph expansion. Nontransparent wrappers
  and explicit inter-item layout spacing retain their original boxes.
  Before wrapping, compatible adjacent source atoms and font-style markers
  share a paragraph analysis and source map. Interior grapheme boundaries do
  not become line breaks merely because a source/style node ends there. The
  shared ICU line-break stream replaces atom-local end penalties, admitting
  cross-node CJK breaks and retaining word-joiner constraints across nodes.
  Empty atoms and source markers do not duplicate a logical break opportunity.
  Explicit negative penalties are retained; control items terminate a flow.
  The paragraph formatter reassembles selected fragments after line breaking and
  justification, including lines beginning inside a different source atom or
  style span. Source markers and source-relative offsets survive this mapping
  and glyph expansion. Lines retain the original
  paragraph's bidi analysis and byte coordinates, while final ASCII-space glue
  widths update glyph advances, caret positions and selection intervals together
  in visual order. Glyph outlines are translated, not stretched by word-space
  justification. Non-text wrappers remain intact. Automatic wrapping still needs
  shared analysis across opaque wrappers, nested composite links and inline
  objects. Legacy dictionary hyphenation is deliberately
  not applied to these UTF-8 fragments because it reconstructs Cork boxes. Font synthesis/effects,
  mathematical strings and runtime import/input remain gates. Programming text
  now uses the same Unicode paragraph and line boxes, retaining KF6 colors and
  grapheme endpoints. KF6 receives explicit UTF-16 text with a scalar-to-byte
  mapping, including surrogate pairs; literal angle-bracket text is not decoded
  as a Cork symbol. Rigid code lines retain their source spaces for joining.
  This intermediate implementation must not be deployed.
  Regression coverage includes actual mixed-color/background RTL painting,
  linked source selection and glyph expansion, and inspection of final native
  PDF link dictionaries, resolved internal destinations, URIs and extracted text.
- Shaped drawing borrows the original UTF-8 input, passing the selected range
  separately from glyph ids to the renderer. The native PDF renderer emits
  Unicode cluster mappings and per-run ActualText (PDF 1.5), preserving original
  ligature spelling and combining sequences even when they share a font glyph.
  This also covers bitmap Type 3 fallback; no glyph id is cast to Unicode. The
  PDF 1.4 output is promoted through the catalog version when it uses this
  feature; version state belongs to each renderer. The legacy drawing entry
  remains separate until the runtime switch. PDF text strings use Qt's UTF-16BE
  encoder, never the Cork converter. Editor/PDF end-to-end integration remains
  migration work.
  `shaped_pdf_test` checks native and Type 3 CMaps, exact Poppler extraction,
  effective PDF version, qpdf structure and rendered pixels. It exercises the
  production printer factory even with retired native-PDF preferences disabled.
  PDF export is always native: there is no PS intermediary or distillation.
  Ghostscript is restricted to importing PS/EPS images.
- The codec does not interpret `<...>` inside text. Complete semantic symbols
  use the built-in `NAMED_SYMBOL` / `(named-symbol identity)` node, separate
  from the old incomplete-input `SYMBOL`. Its identifier is inaccessible during
  normal editing; navigation and deletion treat the entire symbol atomically.
  `misc/symbols/named-symbols.json` is the native registry for symbol identity,
  mathematical class and glyph/slant recipes. Recipes are rendering data, not
  replacement source text. The initial entries cover ten mathematical constants
  and differential operators from the legacy roman/italic/upgreek encodings.
  They use owner-local Pango selection and HarfBuzz shaping, with explicit slant
  and inherited font family, weight, point size and device scales. No angle-token
  parser or Cork converter is involved. Missing definitions remain intact and
  display an error marker instead of an approximate substitute. Virtual glyph
  recipes, extensible symbols and the remaining symbol inventory are still
  integration work, as is migrating keyboard input to construct these nodes.

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

1. Integrate the role-aware legacy importer with application metadata and
   style-defined macro contracts, and finish the `named-symbol` registry's
   remaining glyph recipes and mathematical layout integration.
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
