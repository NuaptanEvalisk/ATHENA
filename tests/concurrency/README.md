# Two-buffer save ownership regressions

These checks are deliberately narrower than an ATHENA integration test. They
can run on a source-review machine with Python 3 and a C++17 compiler, without
installing Qt, Guile, or any system packages. They do not replace the real
`tests/scheme/two-buffer-save-test.py` test.

## Executable checks

From the repository root:

```sh
python3 tests/concurrency/save-source-test.py
python3 tests/concurrency/save-source-test.py --sanitize

g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -pthread \
  -I src/ATHENA tests/concurrency/save_ownership_test.cpp \
  -o /tmp/athena-save-ownership-test
/tmp/athena-save-ownership-test

g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
  -I src/ATHENA tests/concurrency/save_ownership_test.cpp \
  -o /tmp/athena-save-ownership-sanitizer-test
/tmp/athena-save-ownership-sanitizer-test
```

`save-source-test.py` extracts selected production function/class bodies and
compiles them with the C++ test doubles in `*_test.cpp.in`. It does not test a
separately reimplemented algorithm. The doubles replace native trees, the
mailbox, Scheme values, and filesystem operations; they are not the real Qt,
Guile, allocator, or document runtime. The three executables check:

* Shortcut-cache values stay on their creating thread; initialization is reused;
  backend-mode changes invalidate the local cache, while the per-call case
  preference remains effective.
* A non-string conversion result does not reach the file writer, real string
  contents are not interpreted as error messages, write failures propagate,
  and repeated saves see the actor's timestamp despite delayed UI publication.
  Actor-side buffer listing does not traverse UI-owned buffer objects.
* Callback commands preserve the originating actor/view/capabilities when the
  current caller changes, support viewless actor contexts, release retained
  handles on failed submission or a stale view, and retain global behavior for
  genuinely global commands.

`save_ownership_test.cpp` uses the actual production `actor_lifetime.hpp` and
`buffer_name_catalog.hpp` types. It exercises concurrent snapshot publication,
retained generations, eight readers, moved leases, exception unwinding, and
racing close/acquire operations. It does not exercise the full actor registry,
mailbox shutdown, or UI-endpoint lifetime in a running application.

For an expected-failure comparison against a separate pre-fix source tree:

```sh
python3 tests/concurrency/save-source-test.py --source-root /path/to/checkpoint
```

All three executables fail against the review checkpoint and pass against the
candidate. The baseline failures are cross-thread shortcut-tree access, a
failed conversion reported as success, and lost callback capabilities. Each
baseline executable stops at its first failed assertion; this is not an
independent pre-fix failure demonstration for every later assertion.

## Required host validation before calling the save bug fixed

Build and install using the existing icpx/Qt6 configuration, without replacing
or installing system packages. Then run the existing runtime checks and the
unmodified save driver from the repository:

```sh
source /opt/intel/oneapi/setvars.sh
cmake --build build_qt6 -j20
# Perform the repository's normal deployment/install step for this build.
ctest --test-dir build_qt6 --output-on-failure \
  -R '^(glue_generator_test|glue_runtime_test|anchor_confirmation_scheme_test|anchor_confirmation_test|file_chooser_test)$'

python3 tests/scheme/two-buffer-save-test.py \
  --binary ATHENA/bin/ATHENA.bin \
  --runtime build_qt6/athena-guile-runtime --resources ATHENA \
  --capture-stacks --artifacts /tmp/athena-save-ownership-validation
```

With no `--mode`, the driver runs plain, manual-decline, and manual-approve. Its
source-identity, six-save, serialized-content, and approved-anchor assertions
are unchanged. For first-SIGSEGV collection use `--gdb` instead of
`--capture-stacks`, with a separate artifact directory.

Also run the existing converter-search Scheme regression in the configured
ATHENA Guile runtime. The changed `convert-via` failure short-circuit has not
been executed by the C++ source-extraction tests.

The Qt confirmation test now includes direct dialog destruction and parent
window destruction. A live-application scenario must additionally switch away
from the requesting buffer, accept/cancel the dialog, and close its source
view/buffer before completion. The isolated C++ and Qt tests are complementary,
not proof that this whole lifecycle works together.

## Optional continuation tracing

The separate `athena-save-trace-optional.patch` is diagnostic, not part of the
functional patch. After applying it, prefix the real harness command with
`ATHENA_TRACE_SAVE=1`. Its environment is inherited by the driver. The trace
records source/current-buffer URLs, the actual modification/save timestamps,
external-change and conversion prompt entry/replies, anchor resume, the native
save result, recent-buffer hooks, and `on-saved` entry/return. It does not answer
prompts or synthesize successful completions. It has only been delimiter-checked
in the review environment, not loaded by Guile. Remove it after diagnosis.

A post-crash idle stack is not evidence that any particular prompt was entered.
The external-change-prompt explanation remains unconfirmed until the real
trace records that boundary.
