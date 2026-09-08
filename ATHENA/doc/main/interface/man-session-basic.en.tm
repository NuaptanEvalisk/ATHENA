<TeXmacs|1.0.7.11>

<style|tmdoc>

<\body>
  <tmdoc-title|Creating sessions>

  Start an in-process Scheme session using <menu|Insert|Session|Scheme>.
  ATHENA does not launch external interpreters or shell sessions.

  A session consists of a sequence of input and output fields and possible
  text between them. When pressing <shortcut|(kbd-return)> inside an input
  field of a session, the text inside the environment is evaluated and the
  result is displayed in an output field.

  Commands run in the owning document's execution context. Results are
  attached to their originating fields, not to whichever field currently
  contains the cursor. Session names do not create separate OS processes.

  In order to evaluate all fields of <abbr|e.g.> a previously created
  session, you may use <menu|Session|Evaluate|Evaluate all>. Similarly,
  <menu|Session|Evaluate|Evaluate above> and <menu|Session|Evaluate|Evaluate
  below> allow you to evaluate all field above or below the current field.

  <tmdoc-copyright|1998--2002|Joris van der Hoeven>

  <tmdoc-license|Permission is granted to copy, distribute and/or modify this
  document under the terms of the GNU Free Documentation License, Version 1.1
  or any later version published by the Free Software Foundation; with no
  Invariant Sections, with no Front-Cover Texts, and with no Back-Cover
  Texts. A copy of the license is included in the section entitled "GNU Free
  Documentation License".>
</body>

<\initial>
  <\collection>
    <associate|language|english>
  </collection>
</initial>
