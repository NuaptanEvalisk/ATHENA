<TeXmacs|2.1.4>

<style|tmdoc>

<\body>
  <tmdoc-title|Editing Scheme sessions>

  <ATHENA> keeps in-process Scheme sessions, executable fields, and related
  structured session markup. The inherited external TeXmacs Session transport
  has been removed: session markup no longer launches Maxima, Python, shells,
  Jupyter kernels, proof assistants, or other plug-in processes.

  Inside a Scheme session, input and output fields remain structured document
  nodes. Cursor movement, insertion, removal, folding, and undo therefore use
  the same document-editing machinery as the rest of <ATHENA>. Evaluation runs
  in the owning buffer's Scheme execution context and results are attached to
  the field that requested them rather than to whichever field currently has
  the cursor.

  Use <menu|Session|Field> to insert or remove input, output, and text fields.
  The structured insertion and removal shortcuts work as before. Session fields
  can also be folded to hide output while retaining the input and result in the
  document tree.

  <\example>
    A normal in-process Scheme session may contain definitions and later calls:

    <\session|scheme|default>
      <\folded-io|scheme] >
        (define (square x) (* x x))
      </folded-io|>

      <\folded-io|scheme] >
        (map square '(1 2 3 4))
      </folded-io|>
    </session>
  </example>

  Older documents may still contain external Session markup for historical or
  presentation purposes. ATHENA preserves the document structure, but it does
  not reconnect that markup to the retired plug-in execution framework.

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
