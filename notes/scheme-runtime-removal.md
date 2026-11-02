# Complete Native Runtime

## Completion Contract

The active goal is complete removal of Scheme execution from ATHENA, not a
single migration batch. Batches are commit and verification boundaries only.
Completion requires no production Scheme execution, no Guile runtime/build or
packaging dependency, and preserved supported editor behavior and actor
ownership. Do not replace Guile with another Scheme interpreter or retain
dead compatibility APIs. Explicit Scheme script execution necessarily ends
with runtime removal; native automation must use the existing interop API.

## Dependency Order

1. Replace keyboard registry closures and user-shortcut wrappers with native
   actions and predicates. Preserve context precedence, prefixes, Unicode text,
   command ordering, inverse lookup and asynchronous loading.
2. Replace name-based native business dispatch and JSON action evaluation with
   typed native interfaces. Reuse the XML interface source where generation
   is needed; do not maintain a second handwritten procedure registry.
3. Move interactive metadata, argument defaults and callback composition off
   Guile objects. Preserve thread ownership and completion/error behavior.
4. Replace actor/UI/observer Scheme handles and delayed callbacks with owned
   native payloads. Remove the exception path that unwinds C++ through Guile.
5. Make style extern functions native; remove retired export hooks. Keep
   document source trees separate from Scheme runtime values.
6. Delete unused bindings, object/marshalling helpers, embedded runtime code,
   script entry points, private Guile build/deployment and platform packaging.
   Update retained checks and developer documentation with their actual native
   replacement, rather than retaining Scheme solely to run obsolete tests.
7. Build with -j16, deploy using the official helper, and verify the affected
   uncertain runtime boundaries. Audit production source/resources/build files
   again before marking the overall goal complete.

## Completed

- `649eb5b4f`: native application startup; application `progs` removed.
- `1614e060e`: direct native calls for several editor/Qt services; dead generic
  module load and buffer notifier API removed. Build/deployment/startup passed.
- Keyboard storage and dispatch now use native callbacks and context identities,
  including user shortcut prefixes and search/spelling/completion dispatch.
  Removed the Scheme closure-registration/catalog APIs. JSON command expressions
  and inverse-lookup values still use Scheme and are not yet migrated.
  The -j16 build and deployed actor-owned text/prefix/inverse checks passed.
  The check deliberately does not claim physical cursor-motion coverage:
  direct low-level key calls after source replacement omit `apply_changes()`;
  the real `handle_keypress` entry performs that step.
- The XML generator now emits direct native command entries as well as the
  remaining Scheme-facing wrappers. Named C++ calls prefer native entries;
  receiver, UI dispatch, move/rest arguments and C++ exception unwinding are
  preserved without evaluating a Scheme procedure name. The deployed keyboard
  check overrides Scheme `kbd-space` with an error and still inserts a space,
  demonstrating that the real input path uses native dispatch. Generated
  arity/type, UI scheduling and exception-boundary checks also passed.
  `object` marshalling and the fallback for runtime builtins still remain;
  this is not completion of the value/runtime migration.

## Known Outstanding Failures

- `gnutls.cpp` still invokes missing certificate-interaction callbacks and
  overwrites the handshake error with a status-formatting result.
- A C++ exception escaping a Scheme VM call can leave the dynamic-wind stack
  inconsistent. See the concrete core evidence in `native-application-ui.md`.
- `env-math.ats` still declares the retired HTML eqnarray extern hook.

Do not describe these as fixed by removal of `progs`. They remain explicit
parts of the complete runtime-removal work.
