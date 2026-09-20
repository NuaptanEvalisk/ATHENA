# Generic Keybindings

`generic-keybindings.json` is the source of truth for the generic keyboard
declarations. `generic_keyboard_bindings.cpp` validates and loads it; the small
`generic-kbd.scm` module supplies the existing lazy-load point and imports.
Other editing domains and user bindings continue to use the shared registry.

The loader uses Qt's JSON parser, already a required dependency, rather than a
new parser or an embedded Scheme-source format. The one-time migration used
Guile's reader and SXML serializer to read the old declarations.

Groups and bindings are ordered. Later matching declarations retain their
existing precedence. `profiles` is checked at registration using
`has-look-and-feel?`; `mode` and `require` are checked at lookup in the owning
editor context. Prefix expansion, partial key sequences, user overrides and
inverse lookup remain owned by `kbd-binding` and the shared keyboard registry.

Each binding has `key` and either `text` (optionally `help`) or an ordered
`commands` array. Command/argument descriptors are:

- `{"call":"name","args":[...]}`: call the current public procedure.
- `{"procedure":"name"}`: pass the current procedure as an argument.
- `{"symbol":"name"}`: pass a literal symbol.
- Strings, booleans and integers: literal arguments.
- `{"number":1.0}`: an inexact number, distinct from integer `1`.
- `{"if":condition,"then":action,"else":action}`: conditional execution.
- `{"all":[...]}`, `{"any":[...]}`, `{"not":...}`: short-circuit conditions.

There is no Scheme source text in the data. A native dispatcher executes the
descriptors. Small registered callbacks enter it without changing thread or
actor ownership. Their original semantic source is retained as metadata so
menu shortcuts and command reverse lookup do not expose dispatcher indices.

The JSON file is UTF-8. `string_encoding: "latin1"` describes the existing
Scheme/native keyboard interface's **byte strings**: each JSON codepoint
U+0000..U+00FF represents one original byte, including Cork bytes. It is not a
conversion of document text to Unicode. This preserves legacy non-ASCII byte
sequences exactly. Use ATHENA named symbols for other characters; the loader
rejects unrepresentable strings rather than silently replacing them with `?`.
