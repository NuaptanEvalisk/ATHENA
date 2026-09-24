<TeXmacs|1.99.9>

<style|<tuple|source|std>>

<\body>
  <active*|<\src-title>
    <src-package|std-symbol|1.0>

    <\src-purpose>
      Some additional symbols for text mode. This file should become obsolete
      when better support for Unicode will be implemented.
    </src-purpose>

    <src-copyright|1998--2004|Joris van der Hoeven>

    <\src-license>
      This software falls under the <hlink|GNU general public license,
      version 3 or later|$ATHENA_PATH/LICENSE>. It comes WITHOUT ANY
      WARRANTY WHATSOEVER. You should have received a copy of the license
      which the software. If not, see <hlink|http://www.gnu.org/licenses/gpl-3.0.html|http://www.gnu.org/licenses/gpl-3.0.html>.
    </src-license>
  </src-title>>

  <assign|cent|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|¢>|<with|mode|text|¢>>>>>

  <assign|currency|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|¤>|<with|mode|text|¤>>>>>

  <assign|yen|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|¥>|<with|mode|text|¥>>>>>

  <assign|copyright|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|©>|<with|mode|text|©>>>>>

  <assign|copyleft|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|🄯>|<with|mode|text|🄯>>>>>

  <assign|registered|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|®>|<with|mode|text|®>>>>>

  <assign|degreesign|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|°>|<with|mode|text|°>>>>>

  <assign|twosuperior|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|²>|<with|mode|text|²>>>>>

  <assign|threesuperior|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|³>|<with|mode|text|³>>>>>

  <assign|onesuperior|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|¹>|<with|mode|text|¹>>>>>

  <assign|mu|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|µ>|<with|mode|text|µ>>>>>

  <assign|paragraphsign|<active*|<macro|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|¶>|<with|mode|text|¶>>>>>

  <assign|onequarter|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|¼>|<with|mode|text|¼>>>>>

  <assign|onehalf|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|½>|<with|mode|text|½>>>>>

  <assign|threequarters|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|¾>|<with|mode|text|¾>>>>>

  <assign|euro|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|€>|<with|mode|text|€>>>>>

  <assign|trademark|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|™>|<with|mode|text|™>>>>>

  <assign|emdash|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|font|roman|\V>|<with|font|roman|\V>>>>>

  <assign|masculine|<active*|<rsup|<wide*|o|\<wide-bar\>>>>>

  <assign|ordfeminine|<active*|<rsup|<wide*|a|\<wide-bar\>>>>>

  <assign|varmasculine|<active*|<rsup|o>>>

  <assign|varordfeminine|<active*|<rsup|a>>>

  \;

  <assign|nbsp|<macro| <no-break><specific|screen|<resize|<move|<with|color|#A0A0FF|->|-0.3em|>|0em||0em|>>>>

  <assign|nbhyph|<macro|-<no-break>>>

  \;
</body>

<\initial>
  <\collection>
    <associate|preamble|true>
  </collection>
</initial>