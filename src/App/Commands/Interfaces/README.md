# ATHENA native command interfaces

Edit the XML interfaces to add or change named command signatures. Native
implementations are ordinary C++ functions. Do not add handwritten wrappers,
per-command generator cases, or a second registration table.

## Build pipeline

`generate-commands.py` uses Python's standard XML parser and emits native C++
adapters, argument checks and registration. CMake's `athena_commands` target
generates `build*/generated/athena-commands/commands_GROUP.cpp` before compiling
the application. All inputs are dependencies; validation failures stop the
build before publishing new output. No interpreter is used at build or runtime.

The basic, editor, server and native groups share this pipeline.
`native_commands.cpp` includes their generated units.
`initialize_native_commands()` installs them once. Unknown command names return
false from `native_dispatch_call`; invalid arity or argument types throw
`std::invalid_argument`. Native exceptions follow normal C++ stack unwinding.

## Schema

```xml
<commands version="1" prefix="" initializer="initialize_commands_example">
  <binding name="open-document" native="application_open_document_url" returns="void">
    <arg type="url"/>
  </binding>
</commands>
```

- The filename determines the generated group. Names, wrapper names and
  initializers must be unique across all groups.
- `prefix` is empty for free functions, or a nullary receiver accessor followed
  by `->`, such as `get_current_editor()->`.
- Types are finite: scalars, text, bytes, native trees, URLs, paths, modifications,
  patches, tree labels and explicitly typed arrays/lists. `value` is the bounded
  `command_value` variant for predicates; it cannot contain procedures, pairs,
  heterogeneous recursive lists or an executable syntax tree.
- `tree` and `content` preserve source handles and node metadata. `content`
  additionally accepts text. Atomic document tree arguments can supply declared
  text or URL parameters, so ACTION/SCRIPT markup passes data without encoding
  a command expression.
- `bytes` uses `command_bytes` and remains distinct from UTF-8 text.
  `scheme_tree` uses `command_syntax` only for existing serialized data formats;
  it is not an executable value or a replacement for native document trees.
- `passing="move"` transfers the converted local value to the native call.
- Trailing boolean parameters may use `default="false"` or `default="true"`.
  There are no rest arguments, procedures, raw VM values or interpreter exception
  policies.
- `dispatch="ui"` is limited to free nullary void functions and delegates to
  `athena_dispatch_ui`. Other ownership and asynchronous lifetime rules belong
  in the native implementation.

A generated adapter does not grant editor access or change ownership. Values
stay on the calling thread. BufferActor owns document state; Qt owns widgets.
Mailboxes carry IDs and transport payloads. Native callbacks are resolved and
released on their owner, including cancellation and stale-view paths.

## Focused verification

```sh
python3 tests/src/App/Commands/command-generator-test.py
cmake --build build_qt6 --target ATHENA.bin -j16
cmake --build build_qt6 --target buffer_actor_test execution_context_test native_context_test -j16
ctest --test-dir build_qt6 -R '^(buffer_actor_test|execution_context_test|native_context_test)$' --output-on-failure
```

The generator test compiles and executes generated adapters. The actor test
checks real command values and generated commands, metadata identity, callback
ownership, selector behavior, and transformation backup/stale-source handling.
