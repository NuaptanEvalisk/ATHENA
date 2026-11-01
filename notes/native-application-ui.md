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

## Passive-View GUI Dispatch

During the 2026-11-02 file-frontend migration, an isolated offscreen GUI process
running `-X -c source.ath output.txt -x ...` completed conversion and its native
file API assertions, but did not exit after the continuation requested
`(exec-global (lambda () (quit-TeXmacs)))`. The equivalent headless check exited
successfully. The conversion continuation runs on the source actor, whose view
was not opened in a visible document window. Its endpoint existed, but no canvas
was registered to drain its UI effects. Global commands and native application
continuations now dispatch through the GUI event queue when the originating
view is unattached, instead of leaving requests in that endpoint's queue.

Do not use `primitive-exit` from a BufferActor to bypass this observation: a
temporary diagnostic did so and crashed during process exit. No such bypass
was added to production. Offscreen conversion checks verified normal exit both
through `exec-global` and through the native `safely-quit-ATHENA` entrypoint.
The lower-level `quit-TeXmacs` server binding is not an actor-dispatching
application entrypoint; calling it directly from an actor is rejected by the
realtime-save context check. No save-policy change was needed. Temporary
checks and logs are removed after the batch.
