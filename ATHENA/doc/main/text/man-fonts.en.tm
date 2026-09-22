<TeXmacs|1.99.8>

<style|<tuple|tmdoc|english>>

<\body>
  <tmdoc-title|The font selection system>

  In <TeXmacs>, the global document font can be specified using
  <menu|Document|Font>. It is also possible to locally use another font using
  <menu|Format|Font>. Both <menu|Document|Font> and <menu|Format|Font> open
  the native font selector. Fonts have three main characteristics:

  <\description>
    <item*|Family>Fonts are grouped together into <em|families> with a
    similar design.

    <item*|Shape>Inside the same font family, individual fonts have different
    <em|shapes>, such as bold, italic, small capitals, etc.

    <item*|Size>The font <em|size> in points.
  </description>

  The selector stores the concrete system font family and style selected by
  the user and displays sample text for that face. Additional text, math and
  CJK fallback roles may be configured explicitly. The following physical
  properties are used when matching a concrete face:

  <\description>
    <item*|Weight>The font <em|weight> corresponds to the \Pthickness\Q of
    the font:

    <center|<block|<tformat|<table|<row|<cell|<with|font-series|thin|Thin>>|<cell|<with|font-series|light|Light>>|<cell|Medium>|<cell|<with|font-series|bold|Bold>>|<cell|<with|font-series|black|Black>>>>>>>

    <item*|Slant>The font <em|slant> determines the angle of the font:

    <center|<block|<tformat|<table|<row|<cell|<with|font-family|normal|Normal>>|<cell|<with|font-shape|italic|Italic>>|<cell|<with|font-shape|slanted|Oblique>>>>>>>

    <item*|Stretch>This property determines the horizontal width for a fixed
    vertical height:

    <center|<block|<tformat|<table|<row|<cell|<with|font-shape|condensed|Condensed>>|<cell|<with|font-shape|unextended|Unextended>>|<cell|<with|font-shape|wide|Wide>>>>>>>

    <item*|Case>This property determines how lowercase letters are
    capitalized:

    <center|<block|<tformat|<table|<row|<cell|<with|font-shape|mixed|Mixed>>|<cell|<with|font-shape|small-caps|Small
    capitals>>>>>>>

    <item*|Serif>This feature corresponds to the projecting features called
    \Pserifs\Q at the end of strokes:

    <center|<block|<tformat|<table|<row|<cell|<with|font-family|rm|Serif>>|<cell|<with|font-family|ss|Sans
    Serif>>>>>>>

    <item*|Spacing>This feature corresponds to the horizontal spacing between
    characters:

    <center|<block|<tformat|<table|<row|<cell|<with|font-family|rm|Proportional>>|<cell|<with|font-family|tt|Monospaced>>>>>>>

  </description>

  Weight, slant, width, capitalization and spacing describe concrete font
  faces. The native font selector stores the selected system family directly;
  optional sans-serif, typewriter, mathematics and CJK fonts are configured as
  explicit roles rather than inferred from a synthetic superfamily. In that
  case, the rendering may change when selecting another global document font
  (for instance).

  It should be noticed that <TeXmacs> comes with a limited number of
  preinstalled fonts, such as the <with|font|Stix|Stix> fonts and several
  fonts prefixed by \PTeXmacs\Q. Documents which only use these fonts will be
  rendered the same on different systems (assuming the same version of
  <TeXmacs>). When your documents contain other fonts as well, then these
  fonts may be replaced by closest matches when opening your document under a
  different operating system.

  <tmdoc-copyright|1998--2014|Joris van der Hoeven>

  <tmdoc-license|Permission is granted to copy, distribute and/or modify this
  document under the terms of the GNU Free Documentation License, Version 1.1
  or any later version published by the Free Software Foundation; with no
  Invariant Sections, with no Front-Cover Texts, and with no Back-Cover
  Texts. A copy of the license is included in the section entitled "GNU Free
  Documentation License".>
</body>

<initial|<\collection>
</collection>>