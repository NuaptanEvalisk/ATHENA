# UTF-8 Document Storage

## Activation Status

These APIs are migration infrastructure, **not the active document format**.
The editor, bundled resources, legacy converters and protocols still use Cork.
Do not pass a legacy runtime tree to `write_xml`, or pass `read_xml` output to a
Cork editor. ASCII-only tests do not establish that those crossings are safe.
No setting enables a mixed runtime. Normal saves must remain on the existing
path until the text, symbol, position, persistence and protocol migrations are
complete and accepted together.

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
- The codec does not interpret `<...>` inside text. Structural symbols are a
  separate tree-model concern; the old incomplete-input `SYMBOL` is not a
  substitute for the planned `named-symbol` representation.

## XML Version 1

The root is `athena-document` for a full `DOCUMENT`, or `athena-tree` for a
fragment, with required attributes `version="1"` and `text-model="utf-8"`.
The envelope contains exactly one tree. The format version is independent of
the ATHENA program version; original source metadata remains in the tree.

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

## Remaining Integration Gates

1. Legacy import must classify body text, identifiers, macro parameters, code
   and bytes, preserve symbol identity, and emit exact position mappings. The
   existing `Strict-Cork` converter includes one-way symbol substitutions and
   is **not** a lossless migration implementation.
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
