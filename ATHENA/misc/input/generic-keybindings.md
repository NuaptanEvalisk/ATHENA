# Native Keybindings

ATHENA's non-math keyboard declarations live in domain-specific UTF-8 JSON
files under this directory. `generic-keybindings.json` contains global editor
bindings; `text-keybindings.json`, `prog-keybindings.json`,
`source-keybindings.json`, `table-keybindings.json`,
`graphics-keybindings.json`, `fold-keybindings.json`,
`tmdoc-keybindings.json`, and `automate-keybindings.json` preserve the former
domain boundaries. `prefix-keybindings.json` contains prefix help/partial
bindings, while `keyboard-prefixes.json` defines the shared logical-to-physical
prefix rewrites used by both these files and `math-keybindings.json`.

`generic_keyboard_bindings.cpp` validates and registers the domain files with
the existing shared keyboard registry. That registry continues to own partial
key sequences, inverse lookup, user overrides, and mode/require precedence.
Existing Scheme editing implementations remain behind ATHENA's ordinary
`lazy-keyboard` module-loading hooks; only the keymap declarations themselves
have moved out of Scheme.

Groups and bindings are ordered. Later matching declarations retain their
existing precedence. `profiles` is checked at registration using
`has-look-and-feel?`; `mode` and `require` remain live conditions evaluated in
the owning editor context. A group may also contain `unmap` entries.

Each binding has `key` and either `text` (optionally `help`) or an ordered
`commands` array. Command/argument descriptors include:

- `{"call":"name","args":[...]}`: call the current public procedure.
- `{"procedure":"name"}`: pass the current procedure as an argument.
- `{"symbol":"name"}`: pass a literal symbol.
- `{"keyword":"name"}`: pass a Scheme keyword such as `:next`.
- `{"list":[...]}`: pass a literal Scheme list.
- Strings, booleans and integers: literal arguments.
- `{"number":1.0}`: an inexact number, distinct from integer `1`.
- `{"if":condition,"then":action,"else":action}`: conditional execution.
- `{"all":[...]}`, `{"any":[...]}`, `{"not":...}`: short-circuit conditions.

There is no Scheme source text in the data. Registered callbacks enter the
native dispatcher without changing thread or actor ownership. Their original
semantic source is retained as metadata so menu shortcuts and reverse lookup do
not expose dispatcher indices.

All keymap JSON strings are UTF-8. Text actions contain the actual Unicode text
inserted into documents; legacy Cork bytes and `<#...>` character tokens are
not part of the native keymap contract.
