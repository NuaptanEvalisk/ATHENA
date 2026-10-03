# nIKIS Workflows

GPL-3.0-or-later example plugin for ATHENA, using the standalone C++ AUDMAP SDK
and Qt 6 Widgets. All course discovery, document reads and edits go through
AUDMAP. The plugin never opens the vault's files or databases directly.

## Commands

- **New note for course**: select a course with a Lecture Notes namespace.
  Continue its canonical Roman sequence and the last lecture's maximum `§N`
  section. Read the online source, including unsaved edits, when it is open.
- **New assignment for course**: continue the Assignments sequence for the
  course's academic year. No initial section is added.
- **New section**: insert the next `§N` after the maximum preceding section
  number in the focused source buffer, at the cursor.
- **New subsection**: insert the next `§N.M` in the preceding section.

New documents use the filename without `.ath` as their title, Pagella, Reflow,
and `unnumbered-sections-generic`. They open in ATHENA with the cursor in an
empty paragraph. Missing series start at I; missing preceding sections start
at §1. A subsection requires a preceding numbered section. Existing files are
never overwritten. Selections or intervening edits/cursor movement cause a
reported conflict, not insertion at a guessed position.

An AVD's focused source buffer is the editing target, not the whole compound
document. Closing a dialog cancels the command. Errors appear in a native Qt
dialog. A transport failure stops the plugin without replaying a possibly
completed write.

## Build and Install

From the ATHENA repository root (do not build the editor's default ALL target):

```sh
cmake -S ATHENA/examples/nikis-workflows -B build_qt6/nikis-workflows -DCMAKE_BUILD_TYPE=Release
cmake --build build_qt6/nikis-workflows --target nikis-workflows-package -j20
```

Install `build_qt6/nikis-workflows/nikis-workflows.zip` through ATHENA's Plugins
manager, review its license/permissions, then start it. The executable requires
Qt 6 Widgets and libzmq; the SDK itself is statically linked. This is a native
Linux package, not an architecture-independent Python package.

The plugin requests **desktop** access for its course picker/error dialogs.
On X11 this grants access to the display server, which can observe/control other
applications; it is not a filesystem or network grant. It requests specific
AUDMAP operations rather than unrestricted command access. Choose a connection
trust mode in the plugin manager; no authentication is bypassed.

The vault must already provide its course/series namespaces and the
`unnumbered-sections-generic` style. ATHENA ships the style example in
`ATHENA/examples/namespaces`; nIKIS already uses it. `settings.json` contains
the vault name, namespace suffixes and document settings, not executable code.
Academic years come from course homepages or unambiguous sibling courses in
the same parent namespace. Ambiguous directories/years/sequences are errors.

## Host API Additions

- `directory:create_document {name,document}`: new `.ath` document-model-v3
  tree without metadata; the host validates it, assigns fresh source UUIDs
  and exclusively creates XML v2. Parents must already exist.
- `file:open {}`: open an `.ath` through normal ATHENA tab handling.
- `buffer:context {}`: body, body-relative cursor/paragraph start, source epoch,
  view and selection.
- `buffer:insert_at_cursor {tree,epoch,cursor,view}`: owner-actor insertion in
  one edit transaction, with an unchanged-context precondition; no implicit save.
- `buffer:set_cursor {path,epoch,cursor,view}`: move within the same unchanged
  source context, using native cursor validation.

All numbering and course conventions live here, not in a nIKIS-specific
provider. Source files are included in the package; the SDK and host source
are in this same GPL ATHENA repository. Tests must use a separate temporary
vault, never the user's Notes.
