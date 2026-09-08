# TeXmacs Plugin Removal

Date: 2026-09-08. This supersedes the implementation described in
`legacy-plugin-audit-20260908.md` and `session-and-codex-audit-20260908.md`.

## Removed

- Both plugin trees (`plugins/` and `ATHENA/plugins/`), including Python,
  ChatGPT, CAS, shell, proof-assistant, graphics, and Jupyter adapters.
- Plugin build targets, installation rules, SDK header, examples, and the
  dedicated plugin/protocol developer manuals.
- Startup discovery in the application, profile, `/etc/ATHENA`, and
  `/usr/share/ATHENA`; plugin resource-path injection and plugin cache handling.
- Scheme plugin configuration, lazy initialization, registration tables,
  serializers, input converters, completion RPC, and plugin menus/preferences.
- Native connection management, command-line/dynamic-library adapters, and
  the framed TeXmacs output parser, including editor glue bindings.
- External Session/script evaluation, Gnuplot plotting tools, and the
  Mathemagix graphics-extents callbacks.
- The Codex bridge's stdin Session mode and TeXmacs framing. It now accepts
  exactly one of `--one-shot` and `--list-models`.

## Preserved Boundaries

- `tools/codex-bridge/` and `ATHENA/progs/athena/athena/tm-codex.scm` retain AI
  Completion: one-shot requests, streamed output assembly, image attachments,
  model listing, and per-request model/effort/service-tier/web-search options.
  Completion's document-context and insertion implementation is unchanged.
- In-process Scheme Sessions, program fields, scripts, and calculations remain.
  `ATHENA/progs/dynamic/scheme-runtime.scm` schedules evaluation in the caller's
  execution context; it has no external connection registry or shared transport
  queue. Field callbacks track live output/next/busy nodes, reject detached
  destinations, and detach their tree pointers after execution.
- Source-file conversion moved out of the deleted code/Python plugins into
  `ATHENA/progs/prog/{code,python}-format.scm`. Source editing and KF6 syntax
  highlighting do not depend on an interpreter plugin.
- Generic pipes remain for the spell checker. Generic subprocess, socket, and
  native library integrations remain for their non-plugin consumers.
- Qt platform/image plugins are Qt deployment dependencies, not TeXmacs
  plugins, and remain supported.
- Existing user plugin directories are not erased, but ATHENA no longer
  discovers or loads them. Older external Session markup cannot execute.

## Focused Validation

- Normal `build_qt6` Debug build, including Scheme bytecode compilation and
  deployment through `athena_local_runtime`; no TSan build or GUI stress run.
- `tests/scheme/builtin-scheme-test.scm`: full lazy-menu initialization, absence
  of plugin APIs, Scheme scalar/tree results, delayed silent output, field
  results and deleted-field handling, source converters, rejection of external
  Sessions without document changes, and PDF export.
- `tools/codex-bridge/tests/test_bridge.py` with `fake_codex.py`: one-shot
  output, image forwarding, model catalog, request options, and rejection of
  the removed Session mode before spawning a backend. No real AI requests.

The older audits are retained as historical records. Mentions of plugins in
historical credits or general documentation do not restore runtime support.
