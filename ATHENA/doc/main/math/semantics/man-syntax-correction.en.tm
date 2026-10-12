<TeXmacs|1.99.8>

<style|<tuple|tmdoc|english>>

<\body>
  <tmdoc-title|Common errors and syntax correction>

  Mathematical expressions may contain ambiguous invisible operators,
  delimiters, or symbols. This is particularly common when importing older
  documents or converting <LaTeX> input. ATHENA preserves structured formula
  editing without imposing grammar constraints on every keystroke.

  To explicitly correct an existing formula, use
  <menu|Edit|Correct mathematical formulas|Correct all>. When a complete
  subtree is selected, this command corrects that subtree; otherwise it
  operates on the document. The correction algorithms include the following
  optional behaviors:

  <\description>
    <item*|Remove superfluous invisible operators>Remove unnecessary
    implicit multiplications and function applications.

    <item*|Insert missing invisible operators>Recognize situations where
    mathematical operators need explicit invisible structure, especially
    after importing <LaTeX>.

    <item*|Homoglyph substitutions>Correct certain visually similar but
    structurally different mathematical symbols.
  </description>

  The correction options are available from the mathematical correction
  commands and from <menu|Preferences|Editing|Maths and Programming>.
  Mathematical grammar validation is not a proof of mathematical truth;
  always verify that a corrected expression preserves the intended meaning.

  <tmdoc-copyright|2010|Joris van der Hoeven>

  <tmdoc-license|Permission is granted to copy, distribute and/or modify this
  document under the terms of the GNU Free Documentation License, Version 1.1
  or any later version published by the Free Software Foundation; with no
  Invariant Sections, with no Front-Cover Texts, and with no Back-Cover
  Texts. A copy of the license is included in the section entitled "GNU Free
  Documentation License".>
</body>

<initial|<\collection>
</collection>>
