<TeXmacs|2.1.4>

<style|tmdoc>

<\body>
  <tmdoc-title|Scheme session input>

  In-process Scheme sessions use textual Scheme input. By default,
  <key|Return> evaluates the current input field and <key|Shift+Return> inserts
  a multiline break; the Session input-mode controls can reverse that choice
  when multiline editing is preferred.

  Evaluation is local to ATHENA's vendored, modified Guile 3 runtime. There is
  no external program-specific serializer, completion RPC, or TeXmacs framed
  output channel behind a Scheme session. Mathematical document editing remains
  available outside the field, but Scheme input itself is Scheme source rather
  than a two-dimensional CAS input protocol.

  When a result is produced asynchronously, the callback keeps the originating
  field identity and executes in the owning document context. If that field has
  been deleted or detached before the result arrives, ATHENA rejects the stale
  destination instead of writing into another field.

  <tmdoc-copyright|1998--2002|Joris van der Hoeven|2026|Nuaptan Felix Evalisk>

  <tmdoc-license|Permission is granted to copy, distribute and/or modify this
  document under the terms of the GNU Free Documentation License, Version 1.1
  or any later version published by the Free Software Foundation; with no
  Invariant Sections, with no Front-Cover Texts, and with no Back-Cover
  Texts. A copy of the license is included in the section entitled "GNU Free
  Documentation License".>
</body>

<initial|<\collection>
  <associate|language|english>
</collection>>
