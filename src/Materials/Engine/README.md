# ATHENA Materials engine

This small Rust sidecar gives ATHENA a mature BibLaTeX importer and CSL
citation processor without retaining the legacy TeXmacs bibliography runtime.
It exchanges JSON with the C++ Materials subsystem and is installed beside the
main ATHENA executable.

The implementation uses Hayagriva 0.10.1, pinned by Git revision in
`Cargo.toml` and transitively locked by `Cargo.lock`. Hayagriva is distributed
under `MIT OR Apache-2.0`; ATHENA distributes the resulting sidecar under
GPL-3.0-or-later. Update the revision and lock file deliberately and rerun the
Materials import/render tests when changing it.
