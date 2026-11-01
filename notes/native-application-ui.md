# Native Application Menus And Toolbars

Updated: 2026-11-01. The application shell owns menus, toolbars, command palette
and application shortcuts. The former Scheme UI migration inventories and the
October command-loss handoff are retired; they are not current TODO lists.

## Sources Of Truth

- `ATHENA/misc/ui/application-shell.json` and the other UI JSON files author
  presentation, command references, grouping and conditions.
- `src/Subsystems/Qt/QTMCommandRegistry.cpp` owns command registration,
  dispatch and dynamic providers.
- `QTMDocumentMenuCommands.cpp`, `QTMFocus*Commands.cpp` and other provider
  modules supply context-dependent entries.
- `QTMToolbarController.cpp` owns toolbar materialization.
- `src/Edit/Interface/native_editor_actions.cpp` executes editor actions on
  the owning BufferActor. State comes from published editor/pane snapshots.
- `ATHENA/misc/input/` holds keyboard declarations. Registry, event dispatch
  and generic editing are native; `progs/kernel/gui` is removed.

Native UI ownership does not imply every underlying business operation has
already lost its Scheme implementation. Follow the actual command handler
when assessing a remaining native rewrite; do not infer it from a command ID.

## Lessons From The Command-Loss Bug

- A valid registry proves that declared commands resolve, not that an older
  menu's useful capabilities were all migrated.
- Do not derive semantic menu membership solely from command-ID prefixes.
  Menus and toolbars should reference the same commands through authored data.
- Menu conditions must be consumed by the presentation path, not just parsed.
- An empty source atom may carry UUID metadata. Use text-content comparison
  for empty-input conditions, not equality against a metadata-free tree.
- Property panels can legitimately replace older menu branches. Distinguish
  changed presentation from lost capability before restoring anything.

The user-provided-icons toolbar, Scheme API documentation UI and docgrep were
intentionally removed. Plugins retain their Plugins menu. Do not restore those
retired surfaces from historical inventories.

Real-window IME validation after native keyboard/event dispatch is complete,
confirmed by the user on 2026-11-01. Historical commit `abdd53b8e` records an
earlier unverified checkpoint, not an open acceptance item.
