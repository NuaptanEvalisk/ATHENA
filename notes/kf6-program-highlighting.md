# Program Highlighting

KF6 Syntax Highlighting is required on every Qt platform. It was already used
by the namespace manager's C-source editor; program text in document trees now
uses the same maintained syntax-definition library through AbstractHighlighter.
The installed AbstractHighlighter interface carries an MIT license, compatible
with ATHENA's GPLv3 distribution. Syntax definitions retain their upstream
licenses and are supplied by KF6, not copied into ATHENA.

QSyntaxHighlighter alone supplies no language grammars. KSyntaxHighlighting's
QTextDocument adapter is suitable for the namespace manager, but document
typesetting requires AbstractHighlighter: ATHENA has structured trees rather
than QTextDocument blocks. There is no replacement handwritten lexer.

## Ownership And Pipeline

- Language resources, KF6 repositories and highlighters belong to the current
  font/typesetting domain, which already has an owning thread and lifetime.
- Text is projected locally from visible tree content. KF6 UTF-16 offsets map
  back to the original ATHENA byte spans, including encoded Unicode characters.
- DOCUMENT children are successive lines. CONCAT joins fragments on one line;
  formatting bodies do not introduce artificial token boundaries.
- Per-line observers retain KF6 input/output states. Tree edits discard affected
  observers; unchanged lines reuse results until their incoming state changes.
- Color observers feed the existing program-text line-item/box construction.
  Changed lexical context invalidates cached boxes before recoloring.
- No Qt widget, document text or parser state is sent between threads.

Ordinary verbatim text remains uncolored. Languages without a KF6 definition
remain plain text rather than using an approximate local lexer. Math grammars,
math semantic analysis, natural-language layout and file-format parsers remain.

Scheme completion and bracket editing remain; automatic Scheme indentation and
the Emacs Lisp keyword/indentation bridge have been removed. DraTeX and the six
historical TeX support files are removed from both tracked plugin trees.
