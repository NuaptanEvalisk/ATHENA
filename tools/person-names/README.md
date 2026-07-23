# ATHENA Person Name Snapshot

`update_wikidata_person_names.py` regenerates the offline person-name snapshot
used by semantic person tagging.

The updater queries the public Wikidata Query Service for English labels and
Wikidata `P734` family names of people whose direct occupation is one of the
scholarly occupations listed in the script. ATHENA accepts family names only
under stricter exact-case and length rules, reducing collisions with ordinary
words while still recognizing common mathematical references such as Euler
and Noether.

Run from the repository root:

```sh
python3 tools/person-names/update_wikidata_person_names.py \
  ATHENA/misc/person-names/wikidata-person-names.tsv
```

The updater checkpoints completed queries in
`/tmp/athena-person-names-checkpoint.json`. Re-running the command resumes an
interrupted update. It uses three concurrent Wikidata partitions by default;
`--workers` may reduce that value for a constrained endpoint. Remove the
checkpoint to force a complete refresh.

The generated TSV contains one row per unique label and record kind, with one
representative Wikidata entity ID retained for provenance. ATHENA never
contacts Wikidata at runtime. Wikidata structured data is available under CC0.
