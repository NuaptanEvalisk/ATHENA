# Legacy Plugin and CAS Integration Audit

Date: 2026-09-08. Scope: the checked-in implementation, not installation or
runtime validation of every external CAS. This is an audit, not a removal.

## Inventory

There are 25 plugin directories in both `plugins/` and `ATHENA/plugins/`.
The root `plugins/CMakeLists.txt` is included by the root build; the install
rules also package the root plugin tree. The runtime discovers plugins beneath
`ATHENA_PATH`, `ATHENA_HOME_PATH`, `/etc/ATHENA`, and `/usr/share/ATHENA`.
Deleting only the runtime copy is therefore insufficient.

Each row below refers to `plugins/<directory>/progs/init-<directory>.scm`
and its corresponding runtime copy, unless noted otherwise.

| Directory | Current role and transport |
| --- | --- |
| maxima | CAS session and scripts; Maxima launcher plus Lisp adapter and math serializer |
| fricas | CAS session and scripts; `fricas -texmacs`, Scheme input converters |
| pari | CAS session/scripts/completion; `gp --texmacs` |
| maple | CAS session; `tm_maple` adapter, built from root plugin CMake |
| mathematica | CAS session; `tm_mathematica` launcher |
| sage | CAS session/scripts/completion; Python `tmpy` wrapper launched with Sage |
| sympy | CAS session/completion; Python `tmpy` wrapper |
| octave | Numerical session/completion; Octave `.m` protocol and startup helpers |
| python | Python session/scripts/completion; `tmpy` Python evaluator |
| lisp | Lisp session; available CLISP/CMUCL/SCL launcher configuration |
| shell | Shell session; `tm_shell` native adapter, built from plugin CMake |
| coq | Proof-assistant session; `tm_coq`, native XML adapter and Scheme editing code |
| jupyter | Discovers kernels and dynamically registers sessions; can override native plugin launchers |
| codex | ChatGPT session via `athena-codex-bridge`; uses the SAME plugin serializer/connection API |
| asymptote | Graphics session/scripts via `tmpy` and `asy` |
| eukleides | Graphics session/scripts via `tmpy` and Eukleides |
| gnuplot | Plotting session/scripts via `tmpy` and gnuplot |
| graph | Generic graph session via `tmpy` |
| graphviz | Registers `dot` / Graphviz session via `tmpy` |
| plantuml | PlantUML session via `tmpy` |
| texgraph | Graphics session/scripts using `tm_texgraph --texmacs` |
| tikz | LaTeX/TikZ graphics session via `tmpy` |
| xypic | XYpic graphics session via `tmpy` |
| code | Its init is now `(noop)`; legacy `code-format.scm` remains, no external session registration |
| tmpy | Shared Python protocol/session/graphics implementation, not one additional CAS |

The shipped README's old compatibility table is NOT evidence that these
programs work today. Scheme sessions are a separate built-in evaluator exposed
by `dynamic/session-menu.scm`, not a `plugins/scheme` directory. Jupyter can
register kernels beyond this list (its mapping still recognizes R and Julia).

## Execution and Ownership

1. `src/System/Boot/init_texmacs.cpp::plugin_list` scans four plugin roots,
   deduplicates names and prioritizes Jupyter. `plugin_path` extends resource
   discovery to plugin assets as well.
2. `ATHENA/progs/kernel/athena/tm-plugins.scm` initializes `init-*.scm` and
   interprets `plugin-configure`. Requirements control availability; `:launch`
   registers a pipe, `:cmdline` a one-shot command, and `:link` a native dynamic
   library. Serializer, completion, handlers, sessions and scripts are separate
   global tables. A `:socket` declaration is still accepted here, although
   `connection_start` has no corresponding socket construction branch.
3. Entry points include Insert -> Session (`generic/insert-menu.scm`,
   `dynamic/session-menu.scm`), session evaluation, selected-expression/script
   evaluation and plotting (`dynamic/scripts-edit.scm`, `scripts-plot.scm`).
   `utils/plugins/plugin-input.scm` and `plugin-cmd.scm` convert mathematical
   trees to program-specific source, then serialize text for the subprocess.
4. `src/System/Link/connection.cpp` identifies a connection by language and
   session, not BufferActor or view. It owns mutable parser/status/link state.
   The default Qt build has `QTPIPES=ON`: `qt_pipe_link.cpp` embeds a
   `QTMPipeLink`/QProcess; `QTMPipeLink.cpp` receives and buffers stdout/stderr.
   The non-Qt pipe and one-shot command backends also remain.
5. `src/Data/Convert/Generic/input.cpp` parses framing bytes and format/channel
   switches into trees, images, prompts and commands. `connection_rep::listen`
   directly calls Scheme notification handlers in its current execution context.
6. `utils/plugins/plugin-eval.scm` queues callbacks in shared tables keyed by
   `(language session)`. `connection-notify` invokes the first pending callback.
   Its `with-author` changes undo author metadata; it is NOT a BufferActor
   dispatch or lifetime guard. Session callbacks then edit document trees.

There is no end-to-end actor-owned request/response contract in this path.
The generic `connection-*` XML bindings do not dispatch to a connection owner.
QProcess must retain its creating thread, while document mutations must return
to the requesting BufferActor. The implementation does not establish that
boundary. This is a source-level ownership defect/risk, not a claim that every
plugin has been reproduced crashing under TSan.

## Concrete Drawbacks

- **Unbounded synchronous evaluation:** `connection_retrieve` loops until
  `WAITING_FOR_INPUT`; there is no deadline, cancellation check or break for
  `CONNECTION_DEAD`. A dead process or missing prompt can keep the loop running.
  `connection_eval`/`connection_cmd` expose this path alongside asynchronous
  `plugin-feed`, so the existence of a queue does not make all calls asynchronous.
- **Weak response correlation:** framing carries formats/channels, not unique
  request ids, buffer ids, revisions or cancellation generations. Output is
  assigned to the queue head. Interrupt/restart and document lifetime therefore
  require implicit coordination across mutable global state.
- **Executable output channel:** `texmacs_input_rep::command_flush` evaluates
  received text as `(begin ...)`. `scheme_flush` instead converts a Scheme tree;
  these are distinct capabilities. `file_flush` also reads paths supplied by
  the program. This is a trusted local extension interface, not a safe boundary
  for untrusted program output or remote services.
- **String-based launch configuration:** `QTMPipeLink::launchCmd` calls POSIX
  `wordexp(..., 0)` (not `WRDE_NOCMD`) before QProcess startup. Launch strings
  permit shell expansion/command substitution, making quoting and trust part
  of each adapter's responsibility. Native adapters, Python wrappers, Lisp,
  Octave and Scheme all maintain different serialization/escaping conventions.
- **No general resource budget:** parser/output buffers have no common size or
  execution budget. Qt startup and shutdown have blocking waits. SIGINT targets
  the immediate process; it is not request-level cancellation of a process tree.
- **Dynamic library plugins are not isolated:** `src/System/Link/dyn_link.cpp`
  invokes native `install`/`evaluate` in-process; interrupt/stop are empty and
  the destructor explicitly leaves unloading unresolved.
- **Discovery is executable configuration:** initialization runs Scheme and
  external availability checks, with persistent detection state and mutable
  alternate launchers. Removing bundled directories alone leaves user/system
  plugins and initialization paths alive.
- **Presentation is entangled with evaluation:** TeXmacs math-to-code rules,
  output simplification, session prompts, completion, plotting and document
  styles depend on the same plugin protocol. This raises removal cost beyond
  merely stopping CAS subprocesses.

## Removal Boundaries

- Remove both checked-in plugin trees and root plugin build/install hooks;
  retire generated native adapter binaries in deployed runtimes too.
- Remove discovery, initialization and plugin preferences, serializers,
  converters, session/script menu entries, `connection-*` glue declarations,
  C++ connection resources and plugin-specific transport/parser modes together.
  Inventory callers before deleting shared `tm_link` facilities.
- Make explicit decisions for the ChatGPT session bridge, Jupyter, plotting,
  Scheme sessions and old session document markup. They are collateral surfaces,
  not evidence that every generic session or image facility should disappear.
- Preserve KF6 syntax highlighting and normal code/text rendering: these are
  not external CAS evaluation. Preserve ordinary LaTeX import/export, graphics,
  images and mathematical table editing independently of removed adapters.
- Preserve current ATHENA delegation/RAG, MCP, network services and standalone
  command execution unless a concrete call graph proves they require the old
  connection API. They must not be removed just because they launch programs.
- Deletion acceptance should include startup without any plugin scan, absence
  of removed menus/commands, read/display policy for old session documents,
  and explicit tests for surviving Scheme, graphics and delegation features.

No CAS was installed or launched for this audit, and no user vault was touched.
