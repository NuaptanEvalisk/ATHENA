# ATHENA Document Storage

This directory owns ATHENA's native document codec, explicit legacy-document
import boundary, and revision-checked document storage transactions.

The UTF-8/XML migration is complete. This file documents the current storage
contract; it is not a migration checklist or a record of intermediate cutover
states.

## Runtime text contract

- Ordinary native tree atoms and tag identities are valid UTF-8.
- Native text positions are UTF-8 byte offsets.
- Editing boundaries are ICU grapheme boundaries. Codepoint indices, UTF-16
  indices and byte offsets are distinct and require explicit conversion.
- No implicit Unicode normalization is performed by the document codec.
- RAW_DATA contains exactly one atomic byte payload. Those bytes are not text
  and are never validated or transcoded as UTF-8.
- Non-Unicode legacy glyph identities that cannot be represented as ordinary
  text are imported structurally, for example through named-symbol.

The native tree and string containers remain the runtime representation.
Legacy Cork encoding is accepted only at explicit compatibility/import
boundaries; it is not a native runtime text model.

## Native XML version 1

athena_document_xml.{hpp,cpp} implements the native versioned XML codec.
A complete document uses the athena-document envelope; a standalone tree
fragment uses athena-tree. Both require version="1" and text-model="utf-8".

For example:

    <?xml version="1.0" encoding="UTF-8"?>
    <athena-document version="1" text-model="utf-8">
      <node tag="document">
        <text></text>
        <node tag="custom-macro"><text>argument</text><text/></node>
        <node tag="raw-data"><bytes encoding="base64">AP8=</bytes></node>
      </node>
    </athena-document>

node/@tag stores the tree tag identity independently of XML element-name
syntax. Child order, empty atoms, zero-argument compounds, unknown registered
tags and atom whitespace are significant. Formatting whitespace between tree
elements is not document content.

Ordinary text is stored in &lt;text>. Text that cannot be represented literally
without XML 1.0 normalization or exclusion, including carriage return and XML
1.0-forbidden characters, is stored as canonical Base64 with
encoding="base64-utf8". Tag identities use tag-encoding="base64-utf8" when
the XML attribute representation would not preserve them exactly.

The single atomic child of raw-data is stored as
&lt;bytes encoding="base64">...&lt;/bytes>. Base64 encodings must be canonical.
Binary payloads are never interpreted as text.

The codec is deliberately strict. It rejects unsupported format/text-model
versions, malformed UTF-8/XML, DTDs, unresolved entities, namespaces, unknown
structural attributes, invalid Base64, invalid RAW_DATA structure, opaque
native values and configured resource-limit violations. The parser and writer
use local Qt XML stream objects; codec state is not shared across calls.

Full-document XML must contain a DOCUMENT tree without the obsolete top-level
legacy (TeXmacs "...") version node. strip_legacy_document_version performs
that shallow envelope migration and can return the corresponding root-child
mapping. It does not remove nested content that happens to use the TeXmacs tag.

## Reading documents and legacy compatibility

document_file_codec.{hpp,cpp} is the common bytes-to-document entry point.
decode_document_bytes classifies input by explicit file signatures:

- native XML version 1;
- legacy TeXmacs markup beginning with &lt;TeXmacs|;
- supported legacy Scheme document serialization.

Native XML is parsed directly. Legacy formats are semantically imported in
memory through legacy_document_import; reading never rewrites the source file.
Random bytes are not guessed to be legacy text merely because they resemble a
particular encoding.

Legacy import is a compatibility boundary, not a second runtime representation.
It uses the canonical legacy character tables, preserves raw bytes, maps
unrepresentable presentation glyphs to structural identities where required,
and applies DRD/style slot contracts without Scheme evaluation. When source-path
context is supplied, source-local style lookup is confined to the document's
allowed lookup rules.

Legacy reads also return exact node and text-position relocation data. Interior
bytes of old character tokens are not legal cursor positions and are rejected
rather than snapped heuristically. Native XML reads require no relocation map.

Read-only legacy compatibility remains supported until that compatibility window
is intentionally removed. New native persistence does not emit legacy formats.

## Semantic and storage revisions

semantic_document_fingerprint hashes the canonical native XML serialization of
the already imported document tree. It identifies logical document content.
Raw-byte hashes, file metadata and descriptor revisions identify storage state.

Keeping those concepts separate allows a legacy-to-XML rewrite with unchanged
document semantics to preserve semantic identities and caches while still
detecting storage changes. Subsystems that persist semantic revisions should use
the shared fingerprint instead of deriving a hash from the source file bytes.

Protocol, AUDMAP and subsystem-specific persistence-version contracts are
documented with their owning subsystems, in particular
src/ATHENA/Interop/README.md. They are not duplicated here.

## Normal saves

document_upgrade_file.{hpp,cpp} provides the document_file storage handle used
for local document saves.

Opening an existing document captures the concrete storage object and its
revision. A later save verifies that the path still refers to the captured object
and that its revision has not changed. The document is serialized to native XML,
round-tripped through the configured XML reader, and then published through the
descriptor-backed atomic replacement path. An external modification therefore
causes the save to fail rather than silently overwriting newer bytes.

New documents are created directly as native XML. Existing native XML documents
remain native XML on subsequent saves.

For a captured legacy document, the first explicit normal save performs the
format transition. Before replacement, the exact original bytes are preserved
and verified. Vault-contained files use:

    .backup/format-migration/v1/<sha256>/<relative-source-path>

Files outside a supplied vault use a sibling
filename.pre-utf8-SHA256 backup. Merely reading or opening a legacy file
does not create this backup and does not rewrite the file.

After a successful legacy replacement, the same document_file handle becomes
native XML storage. Subsequent saves use the normal XML revision-checked path.
The save result distinguishes a fully durable replacement from the case where
the atomic replacement succeeded but the parent-directory sync could not be
confirmed.

Buffer/editor ownership is outside this codec. Application save code must keep
the live tree and its captured storage state on the owning BufferActor rather
than moving mutable editor state across thread boundaries.

## Offline whole-vault upgrade

Legacy vaults can be converted explicitly with:

    ATHENA.bin --upgrade-vault-format /absolute/path/to/vault

This is an offline, exclusive compatibility operation, not routine maintenance.
Applications and services using the vault must be closed first.

The upgrader validates the live document inventory, builds and verifies a
private sibling snapshot, converts legacy documents and eligible persisted tree
payloads there, verifies semantic document fingerprints, rechecks the original
inventory, and commits with Linux renameat2(RENAME_EXCHANGE). It does not
publish documents one by one and does not fall back to a non-atomic per-file
vault conversion.

The complete original vault is retained in the sibling upgrade backup after a
successful exchange. Existing native XML documents are accepted unchanged, so
rerunning the command on an all-XML vault is a validated no-op.

Production vaults are not codec test fixtures. Automated tests use synthetic
documents and isolated temporary directories.

## Source and tests

The main implementation boundaries are:

- athena_document_xml.{hpp,cpp} — native XML document/tree-fragment codec;
- document_file_codec.{hpp,cpp} — format dispatch, legacy import and semantic
  fingerprints;
- legacy_document_import.{hpp,cpp} and legacy_cork.{hpp,cpp} — read-only
  legacy compatibility;
- document_upgrade_file.{hpp,cpp} — revision-checked normal-save storage;
- vault_format_upgrade.{hpp,cpp} and vault_upgrade_indexes.cpp — explicit
  offline whole-vault conversion.

Focused regression coverage lives under tests/Data/Convert/Xml/, with
application-level save-path coverage in the relevant ATHENA tests. When changing
the storage contract, update the implementation and its focused tests together;
do not turn this README back into a chronological migration log.
