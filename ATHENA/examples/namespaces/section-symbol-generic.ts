<TeXmacs|2.1.4>

<style|source>

<\body>
  <\active*>
    <\src-title>
      <src-style-file|section-symbol-generic|1.0>

      <\src-purpose>
        Generic style with section numbers prefixed by a section sign.
      </src-purpose>
    </src-title>
  </active*>

  <use-package|generic>

  <assign|display-section|<macro|nr|<if|<sectional-short-style>|<#A7><arg|nr>|<#A7><chapter-prefix><arg|nr>>>>

  <assign|display-appendix|<macro|nr|<style-with|src-compact|none|<if|<sectional-short-style>|<#A7><number|<arg|nr>|Alpha>|<#A7><display-chapter|<number|<arg|nr>|Alpha>>>>>>
</body>

<\initial>
  <\collection>
    <associate|preamble|true>
  </collection>
</initial>
