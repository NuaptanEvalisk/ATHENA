# ATHENA Scheme Interfaces

**Do not write glue wrappers or binding declarations manually in C++ or Scheme.
Edit the XML interface instead.** Generated files are build products, not a
second source of truth. Native implementations remain ordinary C++ functions.

## Pipeline

`generate-glue.py` parses XML with Python's standard ElementTree parser and
directly emits C++ type checks, argument conversion, native calls, result
conversion and registration. The same XML produces the Scheme
`all-glued-symbols` inventory and an API reference document. There is no
intermediate Scheme generator; the old `build-glue.scm` has been removed.

CMake's `athena_glue` target runs this preprocessor before `athena_body` can
compile. Outputs live in `build*/generated/athena-glue/`. XML and the generator
are explicit dependencies. Missing outputs or changed inputs regenerate
the interface. Generation errors stop the build, rather than falling back to
stale checked-in wrappers.

The preprocessor requires only Python 3. It does not launch Guile or ATHENA and
does not need an editor, display, user profile, or vault. CMake deploys the
generated symbol inventory into the local runtime at
`ATHENA/progs/prog/glue-symbols.scm` before compiling Scheme bytecode. This
ignored resource copy is an output, never an authoritative source file.
Configuration seeds it before CMake enumerates Scheme modules; the build rule
then regenerates on input changes and restores missing outputs.

## Interface Format

```xml
<?xml version="1.0" encoding="utf-8"?>
<glue version="1" prefix="" initializer="initialize_glue_example">
  <binding name="exec-buffer" native="exec_buffer" returns="bool">
    <arg type="url" />
    <arg type="object" />
  </binding>
</glue>
```

- The XML filename identifies the generated group (`example.xml` becomes
  `glue_example.cpp`).
- `prefix` is empty for free functions or a nullary receiver accessor followed
  by `->`, such as `get_current_editor()->` for editor methods. Accessor names
  are not hardcoded in the generator. The accessor enforces ownership.
- `returns` and argument `type` use the marshalling types validated in
  `generate-glue.py`. `void` is a return type only.
- `<arg type="string" passing="move" />` transfers the converted local value
  into the native call. Use it when the native API consumes that value.
- `dispatch="ui"` is available for free nullary functions returning `void`.
  It delegates actor-to-UI scheduling to `athena_dispatch_ui`, preserving the
  originating editor's UI-effect queue. Other signatures must implement their
  ownership and lifetime contract in the native API.
- `procedure` validates a Scheme procedure and passes a rooted `object`.
  `tmscm` passes a raw value without validation, for generic type predicates.
- Names, translated C++ wrapper names and initializers must be unique.
  Unknown tags, attributes, types and calling policies are errors.

Do not put C++ snippets, widgets, locks, or scheduling code in XML. Keep actual
behavior and ownership in the native implementation. A generated wrapper does
not change a call's thread or grant it editor access. Actor/UI dispatch and
asynchronous continuation lifetime must remain explicit in the native API.
Never add function-name cases to the generator. Model general calling policies
declaratively; place feature-specific behavior in ordinary compiled C++ files.
The implementations formerly embedded in `glue.cpp` now live in
`src/Scheme/Scheme/native_interfaces.cpp` with a typed header. `glue.cpp`
contains marshalling primitives and includes generated wrappers, not dialogs or
Vault/Materials implementations.

## Verification

```sh
python3 tests/scheme/glue-generator-test.py -v
cmake --build build_qt6 --target athena_glue -j20
```

After adding a binding, also build the binary and exercise its runtime call.
Reader tests or a generated source diff alone cannot prove that the running
ATHENA has installed a procedure.

## Implementation Choice

The frontend is built on the maintained Python standard XML parser (PSF license),
not a handwritten XML or Scheme parser. The direct emitter preserves ATHENA's
existing marshalling contracts. SWIG supports Guile but uses a different interface and marshalling model;
Shiboken uses XML but targets Python. Neither is a drop-in replacement for
ATHENA's Scheme object representation and ownership contracts.

References: [ElementTree](https://docs.python.org/3/library/xml.etree.elementtree.html),
[SWIG language support](https://www.swig.org/compat.html),
[Shiboken type system](https://doc.qt.io/qtforpython-6/shiboken6/typesystem.html).

The basic, editor, server and native groups share this pipeline. API documentation
can be generated with `src/Scheme/Glue/build-auto-doc /tmp/athena-glue-docs`.
