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

  <assign|cent|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|\<#00A2\>>|<with|mode|text|\<#00A2\>>>>>>

  <assign|currency|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|\<#00A4\>>|<with|mode|text|\<#00A4\>>>>>>

  <assign|yen|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|\<#00A5\>>|<with|mode|text|\<#00A5\>>>>>>

  <assign|copyright|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|\<#00A9\>>|<with|mode|text|\<#00A9\>>>>>>

  <assign|copyleft|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|\<#1F12F\>>|<with|mode|text|\<#1F12F\>>>>>>

  <assign|registered|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|\<#00AE\>>|<with|mode|text|\<#00AE\>>>>>>

  <assign|degreesign|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|\<#00B0\>>|<with|mode|text|\<#00B0\>>>>>>

  <assign|twosuperior|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|\<#00B2\>>|<with|mode|text|\<#00B2\>>>>>>

  <assign|threesuperior|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|\<#00B3\>>|<with|mode|text|\<#00B3\>>>>>>

  <assign|onesuperior|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|\<#00B9\>>|<with|mode|text|\<#00B9\>>>>>>

  <assign|mu|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|\<#00B5\>>|<with|mode|text|\<#00B5\>>>>>>

  <assign|paragraphsign|<active*|<macro|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|\<#00B6\>>|<with|mode|text|\<#00B6\>>>>>>

  <assign|onequarter|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|\<#00BC\>>|<with|mode|text|\<#00BC\>>>>>>

  <assign|onehalf|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|\<#00BD\>>|<with|mode|text|\<#00BD\>>>>>>

  <assign|threequarters|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|\<#00BE\>>|<with|mode|text|\<#00BE\>>>>>>

  <assign|euro|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|\<#20AC\>>|<with|mode|text|\<#20AC\>>>>>>

  <assign|trademark|<macro|<active*|<if|<equal|<value|mode>|math>|<with|mode|text|mode|text|\<#2122\>>|<with|mode|text|\<#2122\>>>>>>

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