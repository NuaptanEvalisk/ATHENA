<TeXmacs|1.0.2.9>

<style|tmdoc>

<\body>
  <tmdoc-title|Keyboard shortcuts for text mode>

  Natural-language text is entered through the operating system keyboard
  layout or input method. ATHENA receives the resulting Unicode text directly;
  it does not implement its own accent composition, transliteration, or
  language-specific keyboard layouts.

  Text mode still provides editing shortcuts for document structure, markup,
  spacing, scripts, and symbol commands. These shortcuts operate on document
  structure rather than replacing the operating system input method.

  When you press the <key|"> key, ATHENA can insert an appropriate typographic
  quote according to the current language and surrounding text. Configure this
  behavior in <menu|Edit|Preferences|Keyboard|Automatic quotes>. Use the
  shortcut variant key when a binding offers an explicit literal alternative.

  <tmdoc-copyright|1998--2003|Joris van der
  Hoeven<tmdoc-copyright-extra|David Allouche>>

  <tmdoc-license|Permission is granted to copy, distribute and/or modify this
  document under the terms of the GNU Free Documentation License, Version 1.1
  or any later version published by the Free Software Foundation; with no
  Invariant Sections, with no Front-Cover Texts, and with no Back-Cover
  Texts. A copy of the license is included in the section entitled "GNU Free
  Documentation License".>
</body>

<\initial>
  <\collection>
    <associate|page-even|30mm>
    <associate|page-reduce-bot|15mm>
    <associate|page-reduce-right|25mm>
    <associate|page-reduce-left|25mm>
    <associate|sfactor|4>
    <associate|page-top|30mm>
    <associate|page-type|a4>
    <associate|page-right|30mm>
    <associate|par-width|150mm>
    <associate|page-odd|30mm>
    <associate|page-bot|30mm>
    <associate|language|english>
    <associate|page-reduce-top|15mm>
  </collection>
</initial>

<\references>
  <\collection>
    <associate|gly-1|<tuple|1|?>>
    <associate|idx-1|<tuple|2|?>>
    <associate|idx-2|<tuple|3|?>>
    <associate|gly-2|<tuple|2|?>>
    <associate|idx-3|<tuple|3|?>>
    <associate|gly-3|<tuple|3|?>>
    <associate|gly-4|<tuple|4|?>>
  </collection>
</references>

<\auxiliary>
  <\collection>
    <\associate|idx>
      <tuple|<tuple|<with|font-family|<quote|ss>|Edit>|<with|font-family|<quote|ss>|Preferences>|<with|font-family|<quote|ss>|Keyboard>|<with|font-family|<quote|ss>|Automatic
      quotes>>|<pageref|idx-1>>

      <tuple|<tuple|<with|font-family|<quote|ss>|Document>|<with|font-family|<quote|ss>|Language>>|<pageref|idx-2>>

      <tuple|<tuple|<with|font-family|<quote|ss>|Format>|<with|font-family|<quote|ss>|Language>>|<pageref|idx-3>>
    </associate>
    <\associate|table>
      <tuple|normal|Typing accented characters.|<pageref|gly-1>>

      <tuple|normal|Typing special characters.|<pageref|gly-2>>

      <tuple|normal|Typing raw quotes.|<pageref|gly-3>>

      <tuple|normal|Language-specific text shorthands.|<pageref|gly-4>>
    </associate>
  </collection>
</auxiliary>
