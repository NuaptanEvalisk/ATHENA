# Concurrency regressions

`save_ownership_test.cpp` is a normal CTest target. It exercises the production
`actor_lifetime` and `buffer_name_catalog` types directly, including concurrent
snapshot publication, retained generations, actor lifetime pins, close races,
and menu metadata publication.

Run it through the configured build so it uses the same compiler and test
environment as the rest of ATHENA:

```sh
cmake --build build_qt6 --target save_ownership_test -j20
ctest --test-dir build_qt6 -R '^save_ownership_test$' --output-on-failure
```

Historical source-extraction harnesses that compiled selected production
function bodies against ad-hoc doubles were intentionally removed. They were
review-time diagnostics for a fixed save-ownership bug, were not registered in
CTest, and duplicated coverage now provided by normal C++ and runtime tests.
