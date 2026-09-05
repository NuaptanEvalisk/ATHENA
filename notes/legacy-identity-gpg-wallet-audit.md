# Legacy Identity, GPG and Wallet Audit

## Removal Decision and Implementation

Update: 2026-09-05. ATHENA is single-user. The legacy identity, GPG and wallet
subsystems have been removed, without compatibility support. Cryptomator is
a future, separate feature; this change does not implement encryption.

- Removed `db-users`, identity widgets, permission wrappers and contributor
  generation. The independent Data tool remains, selecting its database through
  the profile preference `database storage,<kind>` and defaulting to
  `$ATHENA_HOME_PATH/server/global.tmdb`.
- Removed GPG/keychain/wallet modules, lazy entry points, native preference
  defaults and controls, document menus and encryption style packages.
- Removed the native save encryption hook and Scheme load decryption hook.
  Old encrypted markup, passphrases and per-identity database preferences are
  not interpreted or migrated by ATHENA.
- Preserved existing files and platform credentials. No old destroy command,
  GPG invocation, private-key inspection or user-data cleanup was performed.
- Preserved script-execution security, TLS, OAuth, delegation and the independent
  password-generation module.
- Removed the Windows startup display-name probe, whose only consumer was the
  legacy identity creator, and corrected the retained Data chooser's lazy
  module registration to `database/db-widgets`.

Verification: normal `build_qt6` (`icpx`, `-j20`) and `athena_local_runtime`
deployment passed. All 362 retained Scheme modules compiled; the installed
binary matches the build output and retired modules have no installed `.go`
files. Modified Scheme sources passed Guile's reader. No GUI smoke tests or
Windows build were run; the earlier GUI test sequence remains paused.

The inventory below records the pre-removal baseline. Its old entry points and
alternative migration suggestions are historical, not current capabilities.

## Historical Inventory

Date: 2026-09-05. Baseline: eb1f3b853.
Source audit only. No GUI tests, GPG commands, private-key reads, user-data
migrations, or feature removals were performed for this inventory.

## Functional Boundaries

The old user subsystem is local identity metadata and database access policy,
not an ATHENA account-login service. GPG supplies document encryption and key
management through an external executable. The wallet stores GPG passphrases.
These are coupled modules but have different persistent stores.

Dependency direction:

    database db-base <- db-format <- db-users <- db-version / database UI
                                        ^
                                        |
                                  gpg-base
                                   ^    ^
                                   |    |
                              gpg-wallet gpg-widgets / gpg-edit / gpg-menu
                                   ^            |
                              wallet-base <-----+
                                   ^
                              wallet-menu -> platform keychain helpers

Actual wallet consumers found in shipped Scheme code are inside GPG; outside
security modules, only startup registrations and an Emacs syntax entry refer
to its public APIs. This is not evidence of usage by out-of-tree plugins.

## 1. Legacy Users and Identities

Sources: `ATHENA/progs/database/db-users.scm`, `db-widgets.scm`, `db-menu.scm`.

- Data -> Open identities is registered in `db-menu.scm:248`; the Data menu
  is gated by the database-tool preference, which defaults to off.
- The identity UI lists, selects, creates, edits and deletes local identities.
  It edits pseudo, name, email and links the user's GPG fingerprint/key manager
  (`db-widgets.scm:180-260,327`).
- `users-master.tmdb` under `$ATHENA_HOME_PATH/users` stores user records,
  `root/default-user`, user attributes and preferred database records.
- `get-default-user` (`db-users.scm:138`) lazily reads that record and creates
  an identity from OS login/full-name information if absent. Current `add-user`
  uses the pseudo as the record ID, not a generated UUID.
- Per-kind databases default to `users/<uid>/<pseudo>-<kind>.tmdb`
  (`db-users.scm:169`). The general kind is the default.
- `db-current-user`, `with-user`, group delegation expansion and the
  owner/readable/writable checks wrap database operations. `#t` bypasses these
  Scheme checks (`db-users.scm:253-354`). They are not OS authentication or a
  boundary enforced on every native TMDB call.
- Database editing uses the current identity as contributor metadata
  (`db-edit.scm:313,422`). Removing users independently breaks this layer.
- `remove-user` removes a master record after selecting another identity; it
  does not recursively erase the identity's directory or keys. The UI's broad
  data-loss wording must not be mistaken for implemented filesystem cleanup.

## 2. GPG Functionality

Modules: `ATHENA/progs/security/gpg/gpg-{base,widgets,edit,menu,wallet}.scm`.

### Entry Points and Operations

- Startup lazy registrations: `ATHENA/progs/init-athena.scm:464-469`.
- Native Preferences retains an Encryption toggle and a note directing users
  to dedicated maintenance commands (`QTMPreferencesDialog.cpp:2593-2604`).
  It does not embed the old GPG/wallet Scheme preference widgets there.
- Data -> Open key manager is conditional on `supports-gpg?`.
- Fold/content menus expose public-key and passphrase inline/block encryption
  when experimental encryption is enabled (`dynamic/fold-menu.scm:142`,
  `gpg-menu.scm:23`). Bulk encrypt/decrypt functions exist, but their entries
  in this menu are commented out; do not count those entries as exposed UI.
- Document menus expose whole-buffer passphrase encryption, passphrase change
  and disabling encryption (`generic/document-menu.scm:838,874,942,1045`,
  `gpg-menu.scm:218`).
- Key functionality includes listing, RSA-4096 generation, import/export of
  public/private keys, deletion, fingerprint selection and collected public
  keys. Implementation uses `evaluate-system` to run GPG, not native crypto.
- `supports-gpg?` checks experimental encryption, executable availability and
  a public keyring / initialization (`gpg-base.scm:80`). Default experimental
  encryption is off; executable discovery checks gpg then gpg2.

### Persistent State and Document Representation

- User keyring directory: `$ATHENA_HOME_PATH/users/<default-user>/gnupg`.
  Default GPG commands explicitly use this homedir, not implicitly `~/.gnupg`.
- Collected public keys: `collected-public-keys.scm` beneath that directory.
- User name/email/fingerprint metadata: the TMDB identity record.
- Global GPG/wallet preferences: native preference store, not identity TMDB.
- Partial encryption uses `gpg-encrypted`, `gpg-encrypted-block` and their
  passphrase/decrypted variants; GPG attachments also carry key metadata.
- Whole-document encryption serializes the document, encrypts it, and wraps
  armored content in `gpg-passphrase-encrypted-buffer`. It is not simply a
  plain .gpg file rename (`gpg-edit.scm:506-524`).
- `src/ATHENA/Data/new_buffer.cpp:654-670` calls `tree-export-encrypted` during
  texmacs export if an initial `encryption` attribute exists.
- `ATHENA/progs/athena/athena/tm-files.scm:637-638` detects the encrypted-buffer
  markup after loading and invokes decryption. `gpg-edit.scm:577` hooks Save As
  for passphrase association. Autosave URLs also receive passphrase entries.
- `packages/standard/std.ts:23` imports `std-security`; that package defines
  encrypted-content rendering. Its recipient macro can load `gpg-edit` through
  EXTERN (`std-security.ts:31-32`). Three customization packages select the
  GPG information display level.

### Hidden Load-Time TMDB Dependency

`gpg-base.scm:159` initializes `gpg-collected-public-keys-url` at module load.
This calls `gpg-homedir -> gpg-userdir -> get-default-user`, which reads or
creates identity records. It happens outside `supports-gpg?` and therefore
can touch TMDB with experimental encryption off. Merely hiding menus does
not eliminate the dependency. Loading std-security's macro definitions alone
is distinct from invoking the recipient macro that loads Scheme code.

## 3. Wallet

Sources: `security/wallet/wallet-{base,menu}.scm`, `security/gpg/gpg-wallet.scm`.

- Portable backend is GPG. `supports-wallet?` delegates to `supports-gpg?`.
- Wallet has its own keyring directory:
  `$ATHENA_HOME_PATH/system/gnupg/wallet`.
- Entries are an encrypted serialized association table at `table.gpg` in
  that directory, not TMDB rows (`gpg-wallet.scm:25-31`).
- Initialization creates a wallet key named `__TeXmacs_wallet__`, stores its
  fingerprint in preferences and writes the table. Unlock loads a Scheme hash
  table; lock writes it and replaces the in-memory table with an empty string.
- Set/delete write the wallet immediately when unlocked (lines 158-173).
- Commands/UI cover initialization, unlocking/locking, passphrase/key
  reinitialization, destroying the wallet and deleting selected entries.
- `with-wallet` can prompt to unlock when persistent status is enabled; it
  otherwise executes its body without ensuring a writable/unlocked wallet
  (`wallet-menu.scm:284`). Wallet setters are conditional on unlocked state.
- Stored application keys include `(gpg fingerprint)` and
  `(gpg-buffer-passphrase url)` for private-key and document passphrases.
- Separate in-memory `gpg-buffer-passphrase-table` stores document/autosave
  passphrases (`gpg-edit.scm:443-469`).

### Platform Keychain Is a Separate Layer

`wallet-menu.scm:29-53` optionally remembers the wallet master passphrase as
service `TeXmacs`, account `wallet`:

- macOS: `security/keychain/macos-security.scm`, external `security` command.
- Windows: `security/keychain/win-security.scm`, external `winwallet` helper.
- No Linux branch is present in `wallet-can-remember-passphrase?`. This does
  not mean the GPG wallet itself is unavailable on Linux.

Google Tasks does not use this wallet: `src/Subsystems/Google/GoogleOAuth.cpp:142`
uses its own `google-tasks-token.json`. Do not remove OAuth handling or generic
script-execution security alongside these legacy modules.

## Existing Risks Found During Reading (Not Patched or Runtime-Tested)

1. Whole-document export fails open: `tree-export-encrypted` returns original
   plaintext tree `t` after reporting missing passphrase/encryption failure
   (`gpg-edit.scm:506-524`). The C++ caller then converts and saves the returned
   tree (`new_buffer.cpp:661-668`). Error notification does not abort this path.
   This is source evidence of a plaintext-save path, not a claim that it was
   reproduced against a user's encrypted document.
2. TMDB identity access can occur at GPG module load, while native global
   synchronization runs on the UI thread. Previous smoke logs demonstrate
   native TMDB races, but do not identify GPG as that particular trigger.
3. Default-user, current-user/database contexts, wallet tables and per-buffer
   passphrase tables are mutable Scheme globals. Their cross-actor ownership
   has not been validated. Do not call them thread-safe merely because Guile
   can execute concurrently.
4. Current GPG compatibility is unverified: batch commands include
   `--no-use-agent` and the passphrase-fd flow. No GPG subprocess was run in
   this audit, so available commands are not certified end-to-end workflows.
5. No dedicated GPG/wallet test files were found by the scoped test-file search.
   Destructive key deletion/wallet reset must not be used as casual smoke tests.

## Removal / Retention Decisions Needed

If retiring all three, account for all of these surfaces rather than only menus:

- Identity UI and db-users dependencies in the still-retained generic database
  version/edit/convert stack. Choose removal or replacement for that stack.
- Native export hook, Scheme load/decrypt hook, Save As and autosave behavior.
- Startup lazy definitions, native preference defaults/callbacks/toggle/note,
  Scheme widgets, Emacs macro name and menu registrations.
- std-security import, encrypted markup and customization packages. Existing
  encrypted documents should fail explicitly rather than look empty or save
  plaintext. Decide whether to retain read-only legacy decryption separately.
- Platform keychain helpers only after confirming no other callers; keep
  unrelated security, OAuth and delegation functionality.
- Rebuild/prune Scheme bytecode through the normal deployment pipeline.
- Preserve all user identities, private keys, wallet files and platform
  credentials unless explicitly authorized to migrate/delete them. Code
  removal is not authorization to invoke the existing destroy commands.

If retaining GPG while removing TMDB, first separate identity metadata and
stable key-directory lookup from db-users. If retaining wallet, its own encrypted
store need not be converted to SQLite just to remove the identity dependency.

The user's actual stored identities/keys/wallets have not been inventoried.
That is a separate, explicitly scoped metadata-only task if needed.
