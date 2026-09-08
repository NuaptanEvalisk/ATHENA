<TeXmacs|1.99.8>

<style|<tuple|tmdoc|english|old-spacing>>

<\body>
  <tmdoc-title|Scheme as a scripting language>

  Select <menu|Document|Scripts|Scheme> to evaluate Scheme expressions in
  the document. Executable switches can be inserted from
  <menu|Insert|Fold|Executable|Scheme>. Enter an expression such as
  <verbatim|(+ 1 1)> and press <key|return> to evaluate it.

  Executable input fields can refer to other fields. Insert one with
  <menu|Insert|Link|Executable input field>, and press <key|return> to
  switch between its input and computed output.

  Contrary to executable switches, you may attach an identifier to the
  executable input field by deactivating the field or by editing the
  <samp|Ref> field in the focus bar. Inside other executable input fields,
  you may then refer to the value of the field by inserting a <em|field
  reference> using <shortcut|(make 'calc-ref)> or <menu|Insert|Link|Field
  reference>. As a variant to executable input fields, you may sometimes
  prefer to insert plain <em|input fields> using <shortcut|(make-calc-inert)>
  or <menu|Insert|Link|Input field>. These fields can only be used as inputs
  and pressing <key|return> inside such a field will only recompute those
  other fields which depend on it.

  <tmdoc-copyright|1998--2002|Joris van der Hoeven>

  <tmdoc-license|Permission is granted to copy, distribute and/or modify this
  document under the terms of the GNU Free Documentation License, Version 1.1
  or any later version published by the Free Software Foundation; with no
  Invariant Sections, with no Front-Cover Texts, and with no Back-Cover
  Texts. A copy of the license is included in the section entitled "GNU Free
  Documentation License".>
</body>

<initial|<\collection>
</collection>>
