# ATHENA XML v3: current document and synchronization contract

Updated: 2026-10-20. This is a current architecture reference, **not** a
migration assignment. Source code and `AGENTS.md` govern implementation details.

## Current state

- Native `.ath` documents in the production Notes Vault use canonical XML v3
  (Vault `node_model_version=2`). The real Notes migration, history/index
  conversion, and post-publication read checks completed on 2026-10-18.
  The later big-operator correction was also published. **Do not migrate Notes
  again** as part of maintenance.
- Linux desktop uses the deployed XML v3 runtime. The production TrueNAS
  `athena-hodarium-node` was upgraded to v3 on 2026-10-18, retaining its
  device identity, membership, Vault binding, and private state. The NAS Vault
  was initialized as a v3 replica. **Production Hodarium content convergence
  is complete and in use** (user-confirmed 2026-10-20); do not repeat the
  former "waiting for convergence" claim.
- Hodarium document revision descriptors use `ath-xml-v3` with semantic
  version `4`. Current wire domains are `ATHENA-HODARIUM-VAULTS-v3`,
  `ATHENA-HODARIUM-EXCHANGE-v2`, and
  `ATHENA-HODARIUM-REVISION-v2`. Old-format peers/revisions cannot be
  treated as the new protocol; v3 uses separate `revisions-v3-*` and
  `conflicts-v3-*` journals.
- Runtime opening of a **published** v3 Vault is supported. An unpublished
  candidate with `.athena/xml-v3-candidate-manifest.json` is still rejected.
  An old recorded failure to open `node_model_version=2` is **not** the
  current behavior.
- v1/v2 readers, old mathematical paths, and explicit upgrade tools remain
  in parts of the source tree. This is **remaining cleanup**, not a reason
  to implement mixed-format Hodarium replication, repeat production migration,
  or revive the cancelled old-revision-DAG bridge. The XML v3 Reader itself
  never falls back to those old formats.
- This note does not claim an unrecorded iPad build/deployment status. Consult
  the current platform build and deployment records before platform work.

## Strict native XML v3 format

The authoritative implementations are
`src/Data/Convert/Xml/athena_document_xml_v3.cpp`,
`src/Data/Convert/Xml/athena_document_xml.hpp`, and
`src/Data/Tree/math_tree_v3.{hpp,cpp}`.

| Concept | Canonical XML |
| --- | --- |
| Complete document | `<a v="3" k="d">ROOT</a>`, with DOCUMENT root |
| Single-tree fragment | `<a v="3" k="t">ROOT</a>` |
| UTF-8 text atom | `<t><v>text</v></t>`, including for empty/metadata-free text |
| Raw bytes | `<b><v e="b">BASE64</v></b>` (RAW_DATA payload) |
| General compound | `<n t="document">CHILDREN</n>` |
| Source UUID | `i="UUID"` on its owning native node |
| Typed properties | `<x><p n="NAME" t="TYPE">VALUE</p></x>` |

Typed property codes: `s` UTF-8 string, `b` bool, `i` int64,
`f` finite double, `l` list (unnamed `<i>` items),
`d` dictionary, `r` UUID reference, `h` native rich-text subtree.
Do not confuse the `<i>` list element with a node's `i` UUID attribute.
Use canonical Base64 where XML text cannot preserve a byte sequence.
The codec rejects malformed shapes, unknown native math nodes, UUID conflicts,
invalid encodings, unsupported versions, and configured resource-limit violations.

A v3 Reader must construct the **native** v3 tree, without manufacturing legacy
`AROUND`, `RSUB`, `BIG_AROUND`, or `TFORMAT` trees. `CONCAT` (horizontal
sequence) and `DOCUMENT` (vertical document) are not interchangeable;
preserve independently identified nodes.

## Mathematical tree contract

- `DELIMITED`: exactly six physical slots
  `[left glyph, right glyph, sizing, left manual size, right manual size, body]`.
  Only the body is editable content. XML `<d l="(" r=")" s="a">BODY</d>`
  uses `a` auto, `f` natural, `m` independent explicit `ls`/`rs`
  sizes. Both endpoint attributes must be present, even if one is empty.
  Do not persist unmatched legacy LEFT/RIGHT or infer a missing paired glyph.
- `MATH_MIDDLE`: separate middle delimiter, e.g. `<m k="|"/>`.
  Lexical division `/` and set difference `\` are not delimiters.
- `BIG_OPERATOR`: Unicode operator glyph and upright flag; XML
  `<o k="∑" u="0"/>`. The glyph must be valid for the native operator;
  historic names such as `cup` are not accepted as Unicode glyphs.
- `MATH_SCRIPTS`: **five content slots**
  `[base, left-sub, left-sup, right-sub, right-sup]` (empty slots retained).
  `math:scripts-policy` is serialized as `p`: `a` auto, `d` display,
  `o` over/under, `r` right. Both operator limits and ordinary scripts
  use this structure; an empty base can be intentional.
- `MATH_BLOCK`: display expression or `MATH_GRID`, with
  `math:block-numbered` serialized as `b`.
  `MATH_GRID` comprises ROWs of CELLs, with
  `math:grid-kind` serialized as `k`.
  Kinds include `matrix`, `eqnarray`, `align`, `cases`, `stack`,
  `generic`.
- A direct `MATH_GRID` ROW in `eqnarray`/`align`/`stack` can carry
  `math:row-numbering` (missing means inherit, explicit values
  `numbered` or `unnumbered`) and a separate rich-text
  `math:row-tag`. Custom tags do not advance the auto equation counter.
  Do not infer identities from display numbers; retain UUIDs and references.
- Other native types include `MATH_ACCENT`, `MATH_ANNOTATION`,
  `MATH_LONG_ARROW`, `MATH_VARIANT`, and `MATH_SYMBOL`; the latter
  holds a registered stable semantic symbol ID, not a legacy glyph recipe.
- Math is tokenized, not a mandatory fully semantic expression tree:
  `A/B` stays a lexical sequence, not automatically a fraction.
  Do not silently degrade unsupported native math on export.

The physical form is validated by the v3 codec; the live editor,
typesetter, export paths, clipboard, and BufferActor must preserve the
same structural and identity contracts.

## Implementation entry points and remaining maintenance

- XML v3 codec: `src/Data/Convert/Xml/athena_document_xml_v3.cpp`;
  version dispatch: `native_xml_codec.cpp` and `document_file_codec.cpp`.
- Math grammar: `src/Data/Tree/math_tree_v3.{hpp,cpp}`, tree labels and DRD.
- Editing: `src/Edit/Modify/edit_math.cpp` and `src/Edit/Interface/`.
  Recent 2026-10-19/20 commits repaired native scripts, delimiters, formula
  focus, MathBlock/MathGrid insertion, and grid row editing. Read HEAD rather
  than redoing earlier handoff TODOs.
- Typesetting: `src/Typeset/Concat/concat_math.cpp`,
  `src/Typeset/Table/`; export/import: `src/Data/Convert/Tex/`,
  `ATHENA/progs/convert/mathml/`, and `ATHENA/progs/convert/html/`.
- Vault lifecycle: `src/ATHENA/Data/vault.cpp`; Hodarium descriptors and
  transport: `src/ATHENA/Hodarium/revisions.hpp`,
  `peer_replication.cpp`, `revision_transfer.cpp`.
- Tests: `tests/Data/Convert/Xml/xml_v3_test.cpp`,
  `tests/Edit/Modify/native_v3_math_editor_test.cpp`,
  `tests/Edit/Interface/native_v3_grid_commands_test.cpp`, and
  focused runtime tests under `tests/scheme/`.

Remaining source cleanup of legacy readers/migration entry points and
regression work should be scoped to a current user task, not the old
2026-10-16/18 pre-publication checklist. Any future real-data change or
cross-device deployment requires its own explicit scope; routine code or
documentation investigation does not authorize changes to production Notes.
