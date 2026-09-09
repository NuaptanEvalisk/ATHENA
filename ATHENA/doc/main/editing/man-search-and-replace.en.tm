<TeXmacs|1.99.5>

<style|<tuple|tmdoc|english>>

<\body>
  <tmdoc-title|Search and replace>

  Press <key|C-f> or choose <menu|Edit|Search> to open the document search
  bar. Matches are highlighted in the document. The counter shows the current
  match and the total number of matches. Use the arrow buttons to select the
  first, previous, next, or last match. <key|return> advances to the next
  match; <key|S-return> moves to the previous one. Enable Match case to
  distinguish uppercase and lowercase letters.

  Press <key|C-h> or choose <menu|Edit|Replace> to show the replacement field
  below the search field. The Replace match button replaces the current match
  and selects the next one. Pressing <key|return> in the replacement field
  has the same effect. An empty replacement removes the matched text.

  The Replace all matches button, or <key|C-return>, replaces all matches in
  the document, including those before the cursor. It processes the matches
  found before the operation, without repeatedly matching newly inserted text.
  All changes from this operation form one undo step. Read-only documents
  cannot be modified.

  These fields accept text, not document markup. Text within formatting is
  searchable; the old structured search and replacement panels are no longer
  available. Global search remains a separate command.

  Press <key|C-f> to hide the replacement row while keeping the search bar.
  Press <key|escape> to close the bar and return focus to the document. To undo
  a document replacement, return to the document and use its Undo command.

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
