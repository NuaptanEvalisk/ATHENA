# Native HTML Conversion

## Current Status

Export body recursion and presentation are native C++; `tmhtml.scm` is deleted.
The resource adapters and converter entrypoints are native too; the entire
`ATHENA/progs/convert` directory has been removed.
Import syntax is native Lexbor, and recursive HTML-to-document semantics now
live in `html_import.cpp`. `htmltm.scm` and its former entry adapters are deleted. Import namespace names now come directly from the Lexbor DOM; the
second Scheme prefix-resolution pass is removed. Migration and the final scope
audit are complete. The dated implementation slices below describe intermediate
states, not remaining tasks or alternate supported export backends.

The obsolete Scheme exporter regression module is removed: it asserts the
retired physical HTML tags and calls removed global picture/table helpers.
The focused native conversion runtime check retains representation-boundary
coverage. Unexpanded `hybrid`, `latex`, `symbol` and `rewrite-inactive` nodes
now fail source validation instead of silently disappearing from output.

The unreferenced `sxhtml`, `stm` and `old-tmtable` Scheme helper modules are
removed as well. Embedded web images accept both bare format identifiers and
filenames, preserving their raw bytes without raster conversion. The picture
resource boundary rejects plain inline `math` too, not just specialized v3
mathematical nodes. The focused headless runtime check covers these resource
boundaries and passes against the deployed native converter.

HTML MathML import uses the existing native Presentation MathML importer,
including semantics/TeX annotations, then normalizes external mathematical
input to v3. It does not use the narrower strict-v3 MathML interchange grammar.
The strict native file reader and HTML exporter are not relaxed. This route
passes import, native-v3 validation and HTML reexport with annotated MathML.

## Completion Audit

- Native syntax: `parsehtml.cpp` and `serializehtml.cpp` use Lexbor DOM parsing
  and serialization. Runtime checks cover UTF-8, escaping, foreign namespaces,
  raw text, template content and HTML5 table recovery.
- Native semantics: `html_import.cpp`, `html_semantics.cpp` and
  `html_structure.cpp` own recursive conversion and assembly. `tmhtml.scm`
  and all former Scheme conversion modules/adapters are removed.
- Export representation: the metadata-bearing tree route prepares v3 source
  before serialization. Passive-buffer export exercises rich Definition fields,
  MathBlock, MathGrid and scripts. Source validation rejects legacy mathematics,
  multicol and exposed input macros; unknown markup raises an explicit error.
- Output policy: MathJax is the only mathematics backend and modern CSS is the
  only styling backend. Picture callbacks reject mathematical content, including
  plain inline math. There is no Flash output branch or tmdoc export support.
- Preserved presentation: native markup/style/table/list/ornament and balloon
  handlers retain decoration and web interaction. Runtime checks cover these
  conversions, HTML attributes, JavaScript and document hyperlinks. Editor
  Scheme actions are not executed in exported HTML.
- Resources: per-export adapters retain image bytes and explicit source/target
  context. Runtime checks cover embedded format/filename identifiers, repeated
  export and a colliding image filename in the destination directory.
- Import compatibility: old HTML tables, fonts, whitespace and lists remain
  supported. MathML, Wikipedia TeX images and MathJax import produce canonical
  v3 mathematics and pass reexport. Scilab-specific dispatch is absent.
- Integration: `tests/Data/Convert/Generic/native_conversion_test.cpp` exercises
  the native conversion graph, metadata, exception recovery and passive buffer
  export directly, replacing the retired Scheme converter entrypoints.
  Builds use `-j16`.
  No real Notes files are loaded or modified by these checks.

Converter registration, dispatch, HTML and LaTeX conversion are native.
Editor-owned typesetting/resource operations retain their own ownership. It does not claim a
full Personal Website publication run or visual QA of every document.

## Boundary and Dependency

HTML conversion has moved from Scheme to C++. This is not a replacement
of HTML parsing with another handwritten parser.

The new HTML converter explicitly does not support `tmdoc`. The inherited
documentation will be replaced independently, without that format. Reject
`tmdoc` document styles and `tmdoc-*` markup with an explicit export error;
do not migrate their rendering, CSS, navigation or compatibility behavior.
This does not remove existing help documents from the repository.

## Supported Scope

- Export only canonical v3 document content, with modern CSS and MathJax.
  MathBlock and MathGrid are first-class inputs, not legacy eqnarray fallbacks.
- Do not support old eqnarray/rsub-style mathematical input, HTML-simulated
  mathematics, mathematical rasterization, exposed macro-source rendering,
  multicol or Flash video fallbacks.
- Keep ordinary images/graphics, decorations and interactive presentation.
- Keep HTML import, including old HTML and Wikipedia-specific mathematical
  extraction. Remove Scilab-specific conversion; its PRE content imports as
  ordinary code instead of a Scilab node.
- No alternate non-CSS or non-MathJax output backend is supported.
- Build and verify after substantial module-level changes, not after each
  individual handler migration.

The HTML syntax layer uses Lexbor 3.0.1, pinned to commit
`7e278c0188489bfce6b71ced0cc900cbb31e6244` in CMake FetchContent. It is built
as a private static dependency; no system package or additional shared runtime
is required. Its Apache-2.0 license and NOTICE are shipped under
`ATHENA/misc/licenses/lexbor/`.

Implementations considered:

- Lexbor: maintained WHATWG HTML tokenizer, tree builder, DOM and serializer,
  all available through C APIs. Selected because both directions share one DOM.
- libxml2: already used elsewhere, but its HTML tree builder does not implement
  HTML5 tree construction. It remains appropriate for XML, not this boundary.
- Google's original Gumbo: archived, parser-only; it would still require a
  separate DOM writer. Not selected.

References: <https://github.com/lexbor/lexbor>,
<https://www.lexbor.com/modules/html/>,
<https://gnome.pages.gitlab.gnome.org/libxml2/html/HTMLparser_8h.html>,
<https://github.com/google/gumbo-parser>.


## Conversion Registry And Entry Points

- `ATHENA/misc/convert/{formats,converters,options}.json`: declarative registry
  data, not executable Scheme initialization.
- `src/Data/Convert/Generic/`: native registry, graph, options and executor.
- `src/Data/Convert/Xml/`: HTML import/export, Lexbor syntax and document codecs.
- `src/Data/Convert/Tex/`: native LaTeX conversion.
- `src/ATHENA/Data/buffer_export.hpp` and `new_buffer.cpp`: document/actor
  export boundary.

Xmgrace, GeoGebra, PostScript conversion, Inkscape and tmdoc are not supported
conversion targets. Do not restore removed adapters from historical handoffs.
The standalone PostScript printer implementation is not itself a converter
registration; inspect its actual callers before making a separate removal.
