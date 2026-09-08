# Sessions and Codex Completion

> Historical audit of the implementation before plugin removal.
> TeXmacs plugin discovery, transports, adapters, and external sessions have
> since been removed. See [plugin-removal-20260908.md](plugin-removal-20260908.md)
> for the resulting architecture and validation. Paths below describe the
> pre-removal tree, not the current runtime.


Date: 2026-09-08. Static source audit; no external interpreter or AI request
was launched. See `legacy-plugin-audit-20260908.md` for the full plugin inventory.

## Document and execution model

`progs/dynamic/session-edit.scm::make-session` inserts a `session` node with
language, session name, and a document of input/output fields. Evaluation uses
`session-feed`, output tree pointers, and callbacks registered through
`progs/utils/plugins/plugin-eval.scm`. The UI supplies evaluation, interruption,
folding, input modes and output presentation independently of the interpreter.
Saving this transcript is not saving the interpreter's live execution state.

The pending queue is keyed by `(language session)`; C++ connections use
`name + "-" + session`, not a document or actor id. Identically named sessions
of the same language in different documents therefore share a backend.

## Registration and transport

`progs/kernel/athena/tm-plugins.scm::plugin-configure-cmd` interprets:

- `:require`: availability detection.
- `:launch`: pipe-backed process launcher (possibly overridden by Jupyter).
- `:session`: entry in the Session menu; this does not implement evaluation.
- `:serializer`: document input to program source/protocol bytes.
- `:scripts`: separately exposes script evaluation outside a session transcript.
- `:tab-completion`, `:commander`, `:handler`: optional protocol capabilities.

The common path is document input -> preprocessing/serializer -> connection
write -> adapter/interpreter -> framed output -> input parser -> Scheme
notification -> pending callback -> document output. Formats and channels
support text, mathematical/document output, images, prompts and errors.
This protocol also has a trusted executable command channel.

`src/System/Link/connection.cpp` constructs pipe, command-line or dynamic-library
links. Scheme accepts `:socket`, but this constructor has no socket branch.

## Implementations

| Family | Implementation |
| --- | --- |
| Python | `plugins/python/progs/init-python.scm` launches `plugins/tmpy/session/tm_python.py`; multiline input ends with `<EOF>`, persistent `my_globals` holds Python state, AST execution handles statements and final expressions; helper protocol implements completion, help and image output. |
| Scheme | Built into `session-edit.scm::scheme-eval`; evaluates in ATHENA's own Guile process, not a child Scheme interpreter. `plugin-write` special-cases Scheme with delayed evaluation and feeds results through the common notification queue. Session names do not create separate Guile interpreters. |
| ChatGPT | Plugin id `codex`, display name ChatGPT; launches `athena-codex-bridge`, which launches `codex app-server --listen stdio://`. |
| CAS/numerical | Maxima, FriCAS, PARI, Maple, Mathematica, Sage, SymPy and Octave use external programs and language-specific adapters/converters. |
| Other interpreters | Lisp, shell and Coq have their own launchers/adapters. |
| Jupyter | Discovers installed kernels, registers sessions dynamically, and can replace supported native launchers with a Jupyter client. |
| Graphics | Asymptote, Eukleides, Gnuplot, Graph, Graphviz, PlantUML, Texgraph, TikZ and XYpic use session and/or script interfaces. |

These are registered source integrations, not a claim that all dependencies are
installed or that each plugin currently works. `tmpy` is shared infrastructure;
`code` has a no-op initializer, not an active interpreter session.

## ChatGPT versus AI Completion

Shared implementation:

- Both resolve `athena-codex-bridge` and the `codex home` preference, falling
  back to `$ATHENA_HOME_PATH/codex`.
- The bridge sets `CODEX_HOME` and speaks AppServer JSON-RPC.
- Both use the same initialization, thread creation and turn implementation.
- Thread creation requests `ephemeral: true`, read-only sandbox and no approval;
  base instructions prohibit file edits. These are configured policies, not
  a claim that the prompt itself enforces security.

Independent execution:

| Property | ChatGPT Session | AI Completion |
| --- | --- | --- |
| Scheme entry | `plugins/codex/progs/init-codex.scm` | `progs/athena/athena/tm-codex.scm` |
| Frontend infrastructure | Generic session/plugin connection queue | Native asynchronous QProcess completion |
| Process lifecycle | Bridge/AppServer retained across prompts | New bridge/AppServer for each `--one-shot` request |
| Conversation | One thread reused across turns of the live session | Fresh thread for each completion; no Session history supplied |
| Input | Text, or structured input converted to LaTeX | Selection converted to LaTeX with continuation instructions; visual nodes rendered to attached PNGs |
| Output | Framed `verbatim:` text | Output file parsed as LaTeX and inserted into a captured placeholder |
| Destination | Session output fields | Current document or a new temporary buffer |
| Options | Launcher supplies home; no per-turn effort or image options | Model, effort, service tier, web search and destination options |

Sharing a Codex home means sharing authentication/configuration, not a running
AppServer or conversation id. There is no thread resume implementation here.
The Session banner mentions history, but `ephemeral: true` means it should not
be treated as an implementation of durable conversation recovery.

## Concrete limitations and ownership concerns

1. `runSession` reads one line per turn. The serializer does not provide a
   multiline message envelope, unlike Python's `<EOF>` framing. Embedded
   newlines therefore become separate prompts at the bridge boundary.
2. AppServer deltas are accumulated until turn completion. Session output is
   emitted afterwards, not incrementally streamed into the document. LaTeX in
   a Session answer is returned as verbatim text, not typeset math.
3. The generic queue/connection callbacks do not establish an explicit
   actor-owned request/response boundary. Changing undo author with `with-author`
   does not establish BufferActor ownership. See the earlier audit for details.
4. `src/Scheme/Scheme/native_interfaces.cpp::athena_codex_run_completion_async`
   directly creates a QProcess parented to QApplication and calls `eval(callback)`
   on completion. That body itself does not marshal process creation to the GUI
   thread or explicitly dispatch the result to a requesting BufferActor. Its
   end-to-end caller/callback routing needs a separate ownership audit before
   declaring this path safe; no runtime failure was reproduced in this audit.

## Removal boundary

Removing the ChatGPT Session adapter does not inherently remove AI Completion:
the latter does not call `plugin-feed` or `make-session`. Keep the shared bridge,
Codex runtime/authentication settings, `tm-codex.scm`, native completion bindings,
options/model UI and the conversion/rendering helpers used by completion.
Conversely, deleting the shared bridge would break both features. Scheme
sessions have no plugin directory, and require a separate removal decision.
