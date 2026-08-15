# ATHENA document-open profiling

`profile-open-document.sh` measures a reproducible ATHENA document-open
scenario. It activates the requested vault before loading the document, so
vault-relative transclusions and other vault services follow their normal path.
No profiler GUI or manual menu interaction is involved.

The default `perf` backend starts ATHENA with counters disabled. After normal
startup settles, ATHENA enables collection, runs `load-vault-dir`, opens the
document, waits for the GUI event loop to repaint and settle, disables
collection, and exits. Intel VTune's command-line collector can be selected in
the same way. The profiling home snapshot disables the delayed update checker
and Google Tasks refresh, preventing their network and event-loop work from
entering a long document-open sample.

## Example

```bash
tools/perf/profile-open-document.sh \
  --vault "/home/felix/data/Notes-sandbox" \
  --document "Notes Root/Sources/Works/Differential Geometry/Work - DG - Concise Differential Geometry.ath" \
  --backend perf \
  --runs 5
```

Use `--backend vtune` for VTune CLI hotspots, or `--backend both` to perform a
separate set of runs with each profiler. `--binary` selects a particular
build. The script otherwise selects the newest available binary among
`build_rel`, `build_qt6`, and the locally installed binary. A current
RelWithDebInfo build gives the most representative timings and useful symbols.

For realistic measurements, close other ATHENA processes and use the normal
ATHENA home. The script makes a copy-on-write snapshot below the result
directory, preserving caches and preferences without writing profiling state
back to the real home. `--isolated-home` instead starts from empty caches and
default preferences and is mainly useful for testing the harness.

Each run contains:

- `timing.tsv`: raw vault activation, `load-buffer`, post-load, and completion
  markers. `summary.tsv` subtracts the fixed post-render guard interval from
  the reported open time;
- `trace.json`: the same phase boundaries in Perfetto/Chrome trace format;
- `athena.log` and `stdout-stderr.log`: ATHENA diagnostics and existing
  `-debug-bench` measurements;
- `perf.data`, `perf-stat.txt`, and `perf-report.txt` for the `perf` backend;
- a VTune result directory plus CSV hotspot and summary reports for the VTune
  backend.

The result root also contains `metadata.txt`, `summary.tsv`, and
`medians.tsv`. By default it is created below `/tmp`.
