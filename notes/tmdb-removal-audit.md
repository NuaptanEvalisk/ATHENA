# TMDB Removal Audit

Date: 2026-09-05. Baseline: master, bb80a57eb2.

Removal update (2026-09-05): Literate programming has subsequently been removed
at the user's request: all three `utils/literate` Scheme modules, the `literate`
package, and its example document. The five-group inventory below describes
the audited baseline; group 3 is now retired, leaving four dependency groups.
No user `lp-master.tmdb` cache files were deleted. The other groups remain.

## Scope and Evidence

This is a source/call-site audit, not a new GUI test run. The paused smoke tests
remain paused. No database, migration, or application behavior was changed.
Searches covered tracked repository code, Scheme modules, plugins, styles,
packages, build configuration, and tests. Native overloads were distinguished
from identically named file/material helpers. Third-party implementations and
external user plugins were not treated as ATHENA call sites.

TMDB means the native engine in `src/Subsystems/Database`, its `tmdb-*` glue,
and the Scheme `db-*` wrappers. It does not mean SQLite, every file under
`progs/database`, the font database, or any arbitrary `db-*` identifier.

Existing runtime evidence independently proves that this engine is still used:
`/tmp/athena-workflow-smoke-ig57bmxx/app.log:1` reports main-thread
`sync_databases` racing actor-side `tmg_tmdb_get_field -> get_field ->
get_database -> check_for_updates`. This stack does not identify the Scheme
feature responsible; the feature inventory below is based on source evidence.

No claim is made that all retained UI workflows are currently bug-free or that
the user actively uses them. Dynamic Scheme evaluation and out-of-tree plugins
remain compatibility limits of a static removal audit.

## Executive Conclusion

TMDB is not dead code. Removal has five functional dependency groups:

1. User identities and the indirect GnuPG/wallet dependency.
2. The optional generic database UI, entry editing, query and history facilities.
3. Literate-programming incremental build timestamps.
4. Legacy Vault map import into SQLite.
5. Scheme bytecode compilation setup.

The ordinary modern Vault map and namespace stores do not require TMDB for
their own CRUD operations. Global synchronization hooks still couple the
application and Vault close to TMDB. Deleting the engine alone would break
remaining callers, compilation, and compatibility migration.

## 1. Identities, GnuPG, and Wallet

Primary source: `ATHENA/progs/database/db-users.scm:34-208`.

- `$ATHENA_HOME_PATH/users/users-master.tmdb` stores user records, the default
  user under `root/default-user`, user attributes, and preferred database paths.
- `get-default-user` reads the master and creates a user/default record when
  absent (line 138). The current `add-user` uses the pseudo as record ID; the
  alternative random-ID implementation is commented out.
- `get-user-info` / `set-user-info` expose profile fields. The identity editor
  in `db-widgets.scm:184-260` handles pseudo, name, email, and GPG fingerprint.
- Per-user databases default to `<users-dir>/<uid>/<pseudo>-<kind>.tmdb`
  (`db-users.scm:169`). This is separate from the master database.
- Read/write/owner permission expansion and contributor metadata are also
  implemented by `db-users.scm:226-354`; these are generic database semantics,
  not the modern Vault namespace system.

GPG is an indirect but concrete consumer:

- `security/gpg/gpg-base.scm:14` imports `db-users`.
- `gpg-userdir` (line 49) calls `get-default-user`; `gpg-homedir` uses that
  directory's `gnupg` child.
- The top-level `gpg-collected-public-keys-url` initializer (line 159) calls
  `gpg-homedir`. Consequently merely loading this module accesses the identity
  database, even before a successful `supports-gpg?` check. Turning off the
  experimental-encryption preference does not remove this load-time dependency.
- `gpg-widgets.scm:37,48,96,99` saves a user's key fingerprint and reads name/email
  for key creation. `gpg-edit` and `gpg-menu` provide encryption/key workflows.
- `security/wallet/wallet-base.scm:14` imports `gpg-wallet`, which imports
  `gpg-base`. Wallet data itself lives in
  `$ATHENA_HOME_PATH/system/gnupg/wallet/table.gpg`, not TMDB
  (`gpg-wallet.scm:25-29`). Do not describe the encrypted wallet as a TMDB table.
- Lazy entry points remain in `init-athena.scm:458-469`; encrypted-document
  markup can also load GPG via `packages/standard/std-security.ts:32`.

Removal decision: either retain identities in another store and preserve user
IDs/key-directory lookup, or explicitly retire the identity/GPG/wallet features.
Do not silently generate a different identity and orphan existing key material.
Migrating identity metadata does not require moving private keys into SQLite.

## 2. Generic Database Tool and Documents

The tool is still registered, but optional: `database tool` defaults to `off`
in `src/System/Boot/preferences.cpp:127`. It can be enabled through
`ATHENA/progs/athena/menus/interface-menu.scm:31`; main-menu Data links are at
`main-menu.scm:78,161`. Startup registers lazy menus, toolbar, URL predicate,
and the `tmfs://db/` handler in `init-athena.scm:449-453`.

Retained functional layers:

| Module under ATHENA/progs/database | Responsibility |
| --- | --- |
| db-base.scm | Native field/entry CRUD, query, pagination, history, completions; time/database context |
| db-format.scm | Field encoding/decoding, entry formats and kind tables |
| db-users.scm | Identity, database selection, owner/read/write policy |
| db-version.scm | Entry versions, deduplication and import conflict handling |
| db-edit.scm | Insert/edit/confirm/remove entries and fields, persistence into selected database |
| db-convert.scm | Entry/tree conversion, load/save helpers, URL recognition and import/export hooks |
| db-tmfs.scm | Load/query/save virtual database documents |
| db-markup.scm | Database-entry presentation macros |
| db-widgets.scm | Database chooser, identities and preferences dialogs |
| db-menu.scm | Data menu, editing actions, search/order/limit/presentation toolbar |

`db-tmfs.scm:80-118` reads `user-database`, searches and renders entries, and
saves through `db-confirm-entries-in`. Search/sort/presentation preferences
themselves use ordinary preferences (`db-tmfs.scm:21-30`), not TMDB storage.
Search and spelling widgets restore the database toolbar when active
(`generic/search-widgets.scm:780`, `generic/spell-widgets.scm:587`).

Important limits, not confirmed working features:

- `db-kind-table` is empty at definition; no concrete kind registrations were
  found in shipped source. Comments mentioning bibliography/address books are
  not evidence that current bibliographic/material functionality uses TMDB.
- `db-convert.scm:166-181` defaults import/export capability predicates to false
  and specialized operations to no-op. No shipped overrides were found. Menu
  hooks and generic conversion machinery are not complete import/export support.
- `init-athena.scm:449` refers to `(database db-widget)` (singular), while the
  file and actual chooser definition are `db-widgets.scm:334` (plural).
  This is a pre-existing registration mismatch, not proof the chooser works.
- `styles/test/database.ts:29` loads `db-markup`. Existing documents containing
  database-entry markup may need a retained read-only presentation layer even
  if editing/storage is removed.

Removal decision: explicitly retire this tool or replace its storage semantics.
If retained, a replacement must account for multi-valued fields, temporal
records/history, queries/completions, version links, and permission metadata,
not merely map one entry ID to one flat JSON value.

## 3. Literate Programming

Status: removed after this audit. The following records the former dependency,
not an available ATHENA feature. Its timestamp-cache replacement is unnecessary.

`ATHENA/progs/utils/literate/lp-build.scm:139-166` directly calls TMDB without
the db-base wrapper. It stores source/target timestamp keys in
`$ATHENA_HOME_PATH/system/database/lp-master.tmdb`, with history disabled.

The package `ATHENA/packages/utilities/literate.ts:23` loads `lp-menu`, which
loads `lp-build`. Buffer and directory build actions remain in `lp-menu.scm`.
Crucial distinction: `lp-build` (line 200) bypasses timestamp storage for a
buffer already open; unopened-file/directory builds use `lp-build-conditional`.
Thus testing only Build buffer would miss this dependency.

Removal decision: replace this cache or intentionally rebuild every file.
It is rebuildable cache data, unlike identities or user database entries.

## 4. Vault Compatibility and Synchronization

`src/ATHENA/Data/vault_map_sqlite.cpp:686` handles the map format:

- `.sqlite` returns the selected path without invoking TMDB migration.
- `.tmdb` imports through `read_tmdb_snapshot` (line 110), using native
  `sync_databases`, `query` and `get_field`. It reads UUIDs and `v-path`,
  `v-anchor-begin`, `v-anchor-end` active values.
- Migration checks existing targets, verifies copied nodes/integrity, archives
  the legacy file, and updates Vaultfile. It also handles a previously migrated
  target when the old source has already disappeared.
- `tests/Plugins/Qt/vault_map_sqlite_test.cpp:149-210` still exercises legacy
  creation/migration/recovery and a legacy fixture. Do not remove the entire
  SQLite test just because some cases depend on TMDB.

Native synchronization callers outside the engine are:

1. `src/ATHENA/Server/tm_server.cpp:334`, every interpose-handler invocation.
2. `src/ATHENA/Data/vault.cpp:211`, closing an active Vault.
3. `src/ATHENA/Data/vault_map_sqlite.cpp:115`, legacy snapshot import.

These hooks operate the process-global TMDB registry. The Vault-close call is
not evidence that modern Vault node CRUD is backed by TMDB. That CRUD goes
through `AthenaVaultMapSqlite` (`vault.cpp:228-266`).

Removal decision: keep a separate offline legacy converter, retain an isolated
compatibility reader, or explicitly discontinue old-Vault import with a clear
error and migration instructions. Merely leaving the migration function in
place prevents unlinking the old engine.

## 5. Scheme Bytecode Compilation

`src/ATHENA/ATHENA/athena.cpp:142-179`, `athena_compile_scheme_file`, explicitly:

1. Imports `(database db-base)`.
2. Resolves its `current-database` and `global-database` bindings.
3. Sets the context to `$ATHENA_HOME_PATH/server/global.tmdb`.
4. Wraps `compile-file` in `with-database`.

This is a real dependency of the build/startup-recompilation path, not proof
that the compiler's bytecode files are stored in TMDB. Selecting the URL alone
does not open the engine; evaluated module code can access it. This path must
be updated even if every visible Data menu is removed.

## False Positives and Coupling to Preserve or Remove Separately

- `database/title-markup.scm` and `title-transform.scm` perform document-title,
  author, affiliation and footnote transformations. They do not import db-base
  or invoke the engine. `packages/header/title-base.ts:23` and
  `progs/education/edu-markup.scm:15` depend on them. Preserve these modules.
- `src/ATHENA/Data/namespaces_db.cpp` uses SQLite. Its `namespaces.hpp` includes
  the legacy header for the `strings` alias (`array<string>`); this is a header
  dependency, not TMDB operations. `vault.hpp` also exposes `strings`.
- `vault_maintenance_pass_anchors.cpp` includes the legacy header but has no
  native TMDB calls. Audit/remove the include rather than removing maintenance.
- Material recognition's `set_field(MaterialRecord&, ...)` is unrelated to
  `set_field(url, ...)`. Filesystem `get_attributes` is likewise unrelated.
- Modern Vault mapping and namespace CRUD must survive removal; do not equate
  `tmfs://ns/` with `tmfs://db/` or remove SQLite stores to remove TMDB.
- Font database code is unrelated and outside the removal boundary.

## Mechanical Removal Checklist (After Functional Decisions)

- Remove/migrate the five consumers above before deleting native APIs.
- Remove only the retired db-* modules, lazy registrations, tool preference,
  menu/mode/toolbar hooks, and applicable legacy styles; keep title modules.
- Remove the 12 glue declarations in `src/Scheme/Glue/build-glue-basic.scm:638-649`,
  regenerate `glue_basic.cpp`, and update `ATHENA/progs/prog/glue-symbols.scm`.
- Remove the include from `src/Scheme/Scheme/glue.cpp:92` when no longer needed.
- Decouple common string-array types from `Database/database.hpp`.
- Remove native synchronization hooks when no TMDB consumer remains.
- Remove the five engine .cpp files and header, and their CMake glob at
  `CMakeLists.txt:695`; update legacy-specific tests and fixture dependencies.
- Update README migration promises and invalidate/rebuild Scheme bytecode
  manifests/caches through the existing build pipeline, not hand-edited .go files.
- Do not delete users' .tmdb files, key directories, wallets or old Vault backups.
  Inventory user data only with an explicitly scoped follow-up request.

Suggested acceptance scope after removal: clean-profile startup, existing-profile
identity/key lookup if retained, bytecode recompile, absence of the retired
Literate package/modules/menu, modern SQLite Vault load, chosen legacy-import behavior, and normal
document title rendering. These checks have NOT been run as part of this audit.
