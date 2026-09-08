<TeXmacs|1.99.8>

<style|<tuple|tmdoc|english|old-spacing>>

<\body>
  <tmdoc-title|Spreadsheets>

  ATHENA provides spreadsheet-like fields evaluated with in-process
  Scheme. Select <menu|Document|Scripts|Scheme>, then insert a
  <menu|Insert|Table|Textual spreadsheet> or
  <menu|Insert|Table|Numeric spreadsheet>. Press <key|return> to
  reevaluate the cells.

  In addition, when preceding the contents of a cell by =, then cell will be
  considered as an input-output switch. More precisely, the input is a
  formula which will be evaluated using the current scripting language. After
  the evaluation, only the result of the evaluation is shown in the cell.
  After pressing <key|return> a second time in the cell, it will be possible
  switch back and edit the input. In the formulas, one may refer to the
  others using names such as <samp|c5> for the third row and the fifth
  column.

  <TeXmacs> supports a few special notations for applying operations on all
  cells in a subtable. For instance, as in <name|Excel>, one may use the
  notation <samp|c3:d5> for indicating all cells <samp|c3>, <samp|c4>,
  <samp|c5>, <samp|d3>, <samp|d4>, <samp|d5> in the block from <samp|c3> to
  <samp|d5>. An alternative notation <cell-commas> for <samp|:> can be
  entered by typing <key|, ,>. In a similar way, one may enter the special
  notation <cell-plusses> by typing <key|+ +>. For instance,
  <samp|c3<cell-plusses>d5> stands for the sum of all cells between <samp|c3>
  and <samp|d5>.

  Notice that copying and pasting of subtables works in the same way as for
  ordinary tables, with the additional features that the names of the cells
  and references to cells in the formulas are renumbered automatically.
  Similarly, automatic renumbering is used when inserting new columns or
  rows, or when removing existing columns or rows.

  We also notice that field references can be used inside spreadsheet cells
  in order to refer to some computational markup outside the table.
  Inversely, each spreadsheet also carries an invisible <samp|Ref> field
  which can be edited by deactivating the spreadsheet or from the focus bar
  (when selecting the entire spreadsheet). The <samp|Ref> field of the
  spreadsheet is used as a prefix for referring to the contents of cells
  outside the table or from within other spreadsheets. For instance, if
  <samp|Ref> equals <samp|sheet>, then <samp|sheet-c4> will refer to the
  field <samp|c4> inside the spreadsheet.

  <tmdoc-copyright|2012|Joris van der Hoeven>

  <tmdoc-license|Permission is granted to copy, distribute and/or modify this
  document under the terms of the GNU Free Documentation License, Version 1.1
  or any later version published by the Free Software Foundation; with no
  Invariant Sections, with no Front-Cover Texts, and with no Back-Cover
  Texts. A copy of the license is included in the section entitled "GNU Free
  Documentation License".>
</body>

<initial|<\collection>
</collection>>
