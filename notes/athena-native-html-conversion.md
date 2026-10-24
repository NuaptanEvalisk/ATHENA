# Native HTML Conversion

## Current Status

Export body recursion and presentation are native C++; `tmhtml.scm` is deleted.
`html-export.scm` is a resource/entry adapter with per-export lexical state.
Import syntax is native Lexbor, and recursive HTML-to-document semantics now
live in `html_import.cpp`. `htmltm.scm` is reduced to entrypoints and the LaTeX
adapter. Import namespace names now come directly from the Lexbor DOM; the
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
  and the unreferenced old Scheme algorithm modules are removed; remaining
  Scheme adapters do not contain a recursive document converter.
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
- Integration: generated native bindings are exercised by
  `tests/scheme/headless-html-export-test.scm`, including the standard converter
  graph and actual buffer export. The final run passed; builds use `-j16`.
  No real Notes files are loaded or modified by these checks.

This completes the converter migration, not a rewrite of the separate LaTeX
engine or editor-owned typesetting/resource operations. It does not claim a
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
- Remove obsolete CSS/MathJax/image-math option branches as the remaining
  dispatcher is migrated. No alternate non-CSS or non-MathJax output backend
  is part of the new converter.
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

## Historical Implementation Log

The following slices retain the migration history. Statements about work still
remaining describe their respective intermediate states; Current Status and
Completion Audit above are authoritative.

### First Slice

- `parse-html` parses documents; `parse-html-fragment` parses body-context
  fragments. Snippet conversion uses the latter explicitly.
- DOM namespace identity is carried into SXML so HTML5 MathML/SVG integration
  does not depend on an authored `xmlns` attribute.
- MathJax delimiters are recognized in DOM text, outside literal/code contexts,
  after detecting its runtime script. Formula payloads live in the result,
  replacing the global numeric-id side table.
- `native-serialize-html` builds a DOM from SXML and uses Lexbor's serializer.
  Text and attribute values are raw UTF-8, not pre-escaped HTML. Escaping happens
  exactly once. Script/style contents and preformatted whitespace are not
  rewritten by a generic pretty-printer.
- Each invocation owns its DOM and conversion state. Unsupported serialization
  structures raise errors with an SXML path; void-element contents are not
  silently discarded.
- Scheme retains the semantic document conversion for now. Its `serialize-html`
  entrypoint is only an adapter. Paragraph-wrapper normalization belongs to the
  semantic producer, not the serializer.

## Remaining Semantic Migration

Completion requires deleting `ATHENA/progs/convert/html/tmhtml.scm`, removing
its module dependencies and moving its conversion algorithms to C++. Moving
the Scheme implementation to another filename does not meet this goal. Any
remaining Scheme entrypoints must only adapt converter registration/options
and explicitly editor-owned operations, not retain the body conversion core.

The second slice moves mathematical HTML wrapping/output validation,
enunciation layout planning and page/head resource assembly to
`html_semantics.cpp`. These use invocation-local plans, not native global state.
Mathematical conversion continues to use the existing native LaTeX driver;
Scheme only connects its owned preprocessing callbacks to the HTML plan/output
interfaces. Lossy LaTeX preservation output is rejected before serialization.

Enunciation kind names, aliases, variants and title/body layouts come from
`enunciations.json`; HTML grouping/background defaults now live in the same
declaration. The expansion whitelist is generated from the registry, replacing
the duplicated Scheme enumeration. Structured headings and counters remain
owned by the existing native enunciation presentation/expansion layer. An
unexpanded property-backed enunciation cannot be exported by stripping its
metadata to stree: the converter reports this explicitly.

Document plans select the title, external/inline CSS and JavaScript, favicon,
MathJax resources before converting body content.
This also corrects the old site-version ordering and prevents that option from
leaking into subsequent exports.

Move the dispatch in `tmhtml.scm` to a native per-export context, receiving the
metadata-bearing source tree rather than an intermediate stree. Preserve native
v3 enunciation fields, MathGrid, scripts, delimiters and labels. Reuse the existing
native LaTeX path for MathJax, and preserve explicit failures for unsupported
exports instead of silently substituting lossy output.

Separate editor-owned preparation and image/typesetting resources from pure
conversion. Website export must pass explicit source/destination context, not
depend on a selected UI buffer. Then migrate HTML-to-document semantic handlers
from `htmltm.scm`. Remove obsolete Scheme conversion cores only after their
actual entrypoints have been replaced; registering a native parser does not
complete the semantic rewrite.

The remaining Scheme body dispatcher, formatting-environment layout and image
resource callbacks have not yet been replaced. In particular, these steps do
not make the entire exporter independently concurrent or editor-free.

The third slice moves source paragraph normalization (`surround`, nested
`document`/`concat`/`with` and `locus`), block preservation, adjacent output text
merging and final HTML node/doctype assembly into `html_structure.cpp`.
The two Scheme-global paragraph accumulators are removed; each normalization
owns its state, including nested style scopes. HTML namespace removal leaves
attribute names, values, text and foreign element namespaces intact.

List source grouping is now native as well: item markers start groups, following
paragraphs remain in that item, description terms remain separate from bodies,
and nested lists are left to their own conversion scope. The Scheme list
handlers still invoke body conversion and assemble the outer HTML list tags;
the old `transform-items` implementation and its grouping helpers are removed.

Native markup dispatch now owns common phrase tags, headings and the custom
HTML tag/attribute/class/style, JavaScript and video handlers. Its plan selects
the source children requiring recursive conversion; its output assembles the
HTML nodes. Scheme currently performs that recursive conversion but no longer
duplicates these tag registrations or output algorithms. Unknown markup now
raises an explicit export error instead of returning an empty node set.

The same native markup dispatcher now handles source/rendered figure and table
captions plus ATHENA/TeX/LaTeX/TeXmacs logos. Native concat output also owns
heading/table anchor consolidation and left-tab wrapper removal. It preserves
existing destination IDs and the previous right-fold priority for labels next
to tables; unused labels remain present rather than being discarded.

The attempted tmdoc title/navigation/copyright/license migration has been
removed, along with its body wrappers and stylesheet rules. General CSS
image-followed-by-word grouping remains native. The old CSS pass treated the
body node set as a single element and skipped its first node; the native pass
processes every body node.

The default export stylesheet now lives in `ATHENA/misc/html/export.css`.
Native code loads that resource and appends explicitly supplied document/link/
selection colors. Scheme retains only preference collection, not CSS assembly.
Obsolete HTML-math styling and the disabled non-CSS backend branches are removed;
font and paragraph formatting use spans/divs with CSS. The balloon hover selector
is corrected in the stylesheet rather than compensated for by renderer code.

Length conversion now uses the native length parser, with explicit CSS,
HTML-attribute and pixel output modes. Color conversion is native as well;
Scheme retains only adapters. These changes share the same conversion surface
for subsequent table, margin and decoration work.

Ordinary table export now uses a native plan containing an HTML skeleton and
the cell sources requiring conversion. It flattens outer formatting wrappers,
resolves negative row/column indices and applies formatting rules in source
order, followed by row-local and cell-local rules. Unlike the former Scheme
property partitioning, broad rules do not accidentally move ahead of earlier
specific rules. Native output handles proportional cell widths, CSS borders,
padding/backgrounds and row/column spans. Covered cells are not separately
converted or emitted. MathGrid continues through MathJax, not this table path.
Scheme retains only recursive conversion of the plan's cell bodies under the
cell margin environment; its table normalization/property/output core and
`tmtable` module dependency are removed.

List container/item assembly, labels, hyperlinks, title-block wrappers and
balloon wrappers now use the native markup dispatcher. Local `.ath` and `.avd`
link suffixes become `.html`, retaining query strings and fragments; remote
web and tmfs destinations are left intact. Ornament CSS and wrapper selection
are native too: Scheme supplies only the active ornament environment values
and recursively converted body. Environment entries no longer contain Scheme
style-conversion procedures. A single horizontal/vertical padding value applies
to both sides, while paired values preserve their distinct sides.
Paragraph layout is native as well: inline runs become paragraphs, flow blocks
remain siblings, and phrasing-only style wrappers containing blocks become
styled block containers. Transparent anchors retain their link semantics.
Nested divs are not collapsed: separate backgrounds, borders and spacing remain
separate layout boundaries. Leading/trailing source vertical space becomes
margin on the first/last output block; mixed units use CSS `max()` instead of
discarding all but the first unit. The obsolete Scheme paragraph-layout core
and CSS/length utility imports are removed.
These two slices were built together with `-j16`; the focused headless HTML
check passes. It exercises paragraph style/block boundaries and mixed-unit
spacing, plus actual list, document-link, balloon and ornament export through
the generated runtime bindings. The resulting executable is deployed locally.

Formatting environments now use native plans for text/math mode, display mode,
font/color/size, paragraph alignment and indentation, compact paragraph spacing
and ornament settings. Plans carry the body, one local environment assignment
and an optional output wrapper. Nested margins retain absolute-to-relative
conversion against the parent margin. Scheme only applies the scoped state,
recursively converts the planned body and asks native code to assemble output;
the old formatting dispatcher, font map and style algorithms are removed.
This formatting slice is included in the combined verification below.

Native markup dispatch also owns text decorations, visibility wrappers, floats,
horizontal space, line breaks and transparent body wrappers (including loci,
actions and marks). Their Scheme handlers and dispatcher registrations are
removed. Visibility wrappers choose block/inline containers from the source
body. Source vertical-space nodes are consumed by paragraph layout rather than
emitted independently. Mathematics still takes its MathJax path before text
presentation dispatch.

Document/line sequence plans now own paragraph preparation and assembly,
preformatted line separators and explicit line breaks. Standalone code blocks
are made block-level before recursive conversion. Verbatim preparation and
wrapper output use the native formatting plan, with a scoped preformatted
state; inline code uses the modern `code` element. Text ligature replacement
is native and bypassed in preformatted content. Scheme sequence and verbatim
handlers now only connect those plans to recursive body conversion.

Formatting, presentation and sequence slices were built together with `-j16`.
The headless HTML export check passes through the actual generated runtime
bindings, including code-block whitespace/ligature preservation followed by
normal-text ligature conversion (local preformatted state restoration). The
binary is deployed locally; no Notes files were used or modified.

Tab normalization and left/center/right grouping are now native, along with
alignment wrapper output and named-symbol lookup through the shared symbol
registry. Weak first/last tabs are resolved against their original traversal
positions, including nested concat/with bodies. This fixes the old last-tab
lookahead counting the tab itself as a following tab. The HTML exporter no
longer imports Scheme `tmconcat`; other converters may still use that module.
This slice is included in the native-driver build and runtime check below.

Image HTML dimensions and rendered-picture geometry/label assembly are native.
Image dimensions use modern CSS rather than percentage-valued HTML width/height
attributes. Rendered pictures preserve additional labels as destinations instead
of dropping all but the first. The remaining Scheme resource adapter owns
snippet rendering, extraction/copying, names and the per-export image cache;
failure to produce a rendered picture is an export error, not empty success.
Linked images resolve against the explicit export source, never the current UI
buffer or an unrelated same-named file already present in the output directory.
Embedded images already extracted into the output directory bypass that source
resolution; external web URLs remain external. These changes are included in
the combined build with the tab/symbol slice and native driver.

## Native Export Driver

`ATHENA/progs/convert/html/tmhtml.scm` is deleted. The actual converter entrypoint
now calls `html_export_convert`, whose per-call C++ context owns recursive body
conversion and scoped formatting/math state. It uses the native plans above
for sequences, tables, markup, enunciations, mathematics and page assembly.

`html-export.scm` contains resource adapters and converter entry preparation,
not a renamed body conversion core. It supplies preferences, file inclusion,
image extraction/copying, snippet rendering and the existing owned LaTeX call.
Image names and cache are lexical state of each export's adapter. The adapter
catches Scheme exceptions into an explicit success/error reply before crossing
back into C++; failures then unwind the native recursive context normally.
Source and target context are restored with dynamic-wind, including failures.

Converter registration, runtime consumers and startup bindings no longer load
the removed module. The obsolete `ext-tmhtml-eqnarray*` startup registration is
removed. Old test assertions about disabled MathJax/image-math switches and
Scheme handler internals are retired; native runtime checks exercise the real
export path instead.

The native driver and the extracted adapter both pass the focused headless
runtime check after the `-j16` build. Remaining full-migration work includes
HTML import semantics, resource boundary coverage and the end-to-end scope
audit. Native parsing alone does not complete HTML import migration.

The passive-buffer image check exposed missing source/target context at the
`buffer-export` boundary: direct C++ export previously relied on a UI command
having set Scheme globals. The actor now passes its source URL to `export_tree`,
whose converter call scopes both source and destination with dynamic-wind.
This corrects ownership at the entrypoint instead of guessing a source inside
the image adapter. The check exports into a directory containing a conflicting
same-named image and verifies the copied bytes come from the source document.
After rebuilding the corrected entrypoint, the complete focused headless check
passes, including this collision case and CSS image dimensions. The executable
is deployed with `install-local-executable.py`; existing processes are not
terminated and no Notes documents are changed.

## Import Semantics

`html_import.cpp` starts the native HTML-to-document semantic layer. Anchor
targets retain their original URL spelling. Ordinary image dimensions accept
legacy numeric pixels and percentages, preserving the previous intrinsic-size
fallback. Legacy relative FONT sizes use the native mapping. The Scheme
handlers retain recursive body conversion and label placement temporarily, but
no longer implement those element transformations. Wikipedia image handling
still runs before ordinary image import.

Whitespace normalization now lives in the same native layer. Adjacent text
nodes are merged before collapse; mixed content trims only outer boundaries.
HTML ASCII whitespace includes form feed, but does not include nonbreaking
spaces. PRE preserves the text returned by Lexbor, without repeating the
parser's initial-newline removal or guessing which final blank lines to delete.
Scheme keeps only environment-to-policy adapters for these operations.

Serial/paragraph assembly now also lives in `html_import.cpp`: it joins inline
runs, flattens concat nodes, preserves explicit document boundaries, and trims
visible whitespace around line breaks without removing intervening labels.
Preserved-space contexts bypass that trimming. `htmltm-serial` is now an
adapter to the generated native binding.

List marker/first-paragraph grouping and label insertion now also use native
operations. Continuation paragraphs remain separate; consecutive markers and
nested list content are preserved. Labels remain after ITEM, inside ITEM*, and
outside section titles, descending through document-bearing wrappers where
appropriate. The corresponding Scheme algorithms are removed from the HTML
path. The shared STM helpers remain for unrelated converters.

The assembly/list/label slice was built together with table output and legacy
colors using `-j16`. The focused headless HTML runtime check passes, including
actual old-HTML import with a rowspan, multi-paragraph cell, frame width,
percentage table width, AQUA background and standard GREEN text. It also
checks continuation paragraphs, adjacent list markers and heading destinations.
The binary was deployed with the local executable deployment helper. Remaining
table traversal and full native dispatch have not yet been migrated or verified.

Legacy FONT color attributes now use the existing native SVG color catalog,
not a duplicated Scheme palette or the editor's differently ordered named-color
lookup. Valid RGB hex attributes use the existing native color conversion.
This fixes the old `aqua` entry and preserves HTML `green` semantics.

Table width, frame/rule borders, background, span-format serialization and
the final tabular tree now come from `html_import_table`. HTML import no longer
depends on `old-tmtable`, `tmlength` or `tmcolor` Scheme modules. `bgcolor` is
handled as the background color; the removed implementation incorrectly used
the background-image attribute and referenced an unbound variable.
Cell traversal, occupancy and row-group ordering now use the native table
planner. It computes a rectangular set of cell ancestry paths and span formats;
Scheme only applies its remaining content converter to these paths. Combined
row/column spans reserve covered cells, and footers retain the former import
ordering after body groups. The old Scheme table walker is no longer used by
the HTML converter. The planner was built and verified together with the
recursive import driver, including combined row/column spans.

Recursive dispatch is now native, including ordinary formatting, paragraphs,
lists, tables, Wikipedia branches, MathML and MathJax routing. The importer
returns a native tree. V3 metadata is explicitly retained while assembling
intermediate serial content and restored in the final tree; plain stree
conversion would otherwise discard script placement and other mathematical
properties. The converter graph has a direct HTML-stree to native-tree edge.
SUB/SUP now create canonical script nodes instead of legacy RSUB/RSUP.

LaTeX-backed MathJax/Wikipedia imports normalize the external converter's
detached mathematical output through `normalize_imported_math`. This shares
the established structural conversion engine with the offline migrator but
does not run vault migration, identity pruning or compatibility fallback in
the v3 reader/exporter. Ambiguous imported structures are errors. Its public
import boundary is in `math_source_normalization.hpp`; the shared engine still
resides in the legacy-named implementation and must be retained if migration
entrypoints are retired later.

The old global quote-rewriting postprocessor is removed: importing HTML no
longer rewrites literal quoted text, including code. The native assembler
replaces the old `clean-html` pass rather than running its legacy mathematical
and whitespace rewrites over canonical nodes.

The import-specific DOM adapter now emits canonical HTML/MathML/SVG prefixes
from actual DOM namespace identity, and self-contained MathML declarations.
The generic parser output is unchanged. The importer no longer reconstructs
namespace bindings discarded by Scheme. The unused `xmltm.scm` dispatcher and
its obsolete parser/legacy-representation checks are removed, along with the
old HTML importer test registration. Current native boundary checks remain.
The namespace/cleanup slice was built with `-j16`, passed the focused headless
HTML runtime check (including mathematical import/reexport), and was deployed
with the local executable helper. No Notes files were modified.

Remaining work: end-to-end scope/representation audit.

The native recursive driver is built with `-j16` and deployed. The focused
headless runtime check passes, including separate import/native-v3-validation/
HTML-reexport paths for nested SUB/SUP, MathML scripts, Wikipedia TEX images,
and MathJax source. This caught and corrected two representation boundaries:
MathML requires explicit namespace bindings after old prefix normalization,
and the Scheme entry must use `tm->tree`, not the metadata-dropping
`stree->tree`, when assembling a document containing native tree objects.
The existing SUB/SUP handlers also still produce legacy scripts and must be
replaced in the canonical-v3 import boundary. Wikipedia extraction remains
supported throughout that migration.

The first import elements and whitespace layer were built together with
`-j16`. The headless HTML runtime check passes, exercising the generated
import bindings, PRE blank-line preservation at the whitespace boundary,
mixed-content text runs and percentage/pixel image dimensions, alongside
the existing native export checks. This is not yet an end-to-end verification
of the remaining import assembly and cleanup stages.

## Verification

The scope exclusions, stylesheet extraction, native length/color conversion
and ordinary table export were built together with `-j16`. The focused headless
HTML check passes against that binary, including actual v3 document export,
MathBlock/MathGrid and the new generated table bindings. The table assertions
cover source-order formatting overrides, cell-local overrides and combined
row/column spans with covered-cell omission. This does not yet verify all
unsupported-source diagnostics or the still-unmigrated import semantics.

Built `ATHENA.bin` with `-j16` and exercised the generated bindings in the
binary. The focused headless HTML export check covers actual v3 source loading,
passive-buffer export, Unicode/escaping, HTML5 table construction, template
contents, foreign namespaces, raw-text elements and self-contained MathJax
payloads. The existing HTML5 math export check also passes, including native
MathML import and MathJax output. No Notes files are used or modified.

The second slice also passes both focused checks against the rebuilt binary.
The headless fixture now includes a v3 Definition with a rich-text name and year,
plus a MathBlock containing MathGrid/scripts. It exercises the native semantic
bindings through actual buffer export, rejects lossy mathematics and checks
that per-document resources do not leak into the next plan.

The third slice passes the same headless export check, extended specifically
for nested styled paragraphs with surround prefixes/suffixes, empty paragraphs,
single-paragraph block preservation and namespace-aware output finalization.
The native list grouping check also passes through the generated runtime
binding, covering description terms, continuation paragraphs, leading prose,
counter assignments and nested lists.
Native markup dispatch is exercised through `texmacs->html`, including headings,
rich text, nested class attributes, custom tags and unescaped script text. The
same check verifies that unknown markup is rejected rather than dropped.
The figure/logo path and heading/table anchor consolidation pass the focused
runtime export check, including preservation of existing and additional IDs.
