# Full Vault Load Audit

```sh
python3 /home/felix/data/Software/TeXmacs/texmacs/tools/stress/full-vault-load-audit.py /home/felix/data/Notes --jobs 4
```

Checks every `.ath` recursively across the supplied Vault, not only `Notes Root`.
Directories named `.backup` and `.athena` are pruned at every depth from the
document selection. They remain in the private snapshot because loading the
Vault and resolving references may require its databases and metadata.

Close this Vault in ATHENA during the initial snapshot. When the script prints
`Snapshot ready; original Vault may be reopened`, it no longer needs the source.
An already-running random audit uses its own snapshot and may continue.

The script reuses the random audit's offscreen load and two-pass typesetting
check. Every worker has a separate writable Vault copy and HOME/profile, and
every document gets a fresh ATHENA process. Backup dispatchers are disabled in
the copies; no user Hodarium credentials are copied into the isolated profiles.
The original Vault is never opened by the test processes. Symlinks are refused.

`--jobs`/`-j` controls concurrent processes (default 4); increase cautiously since
each process has its own editor, caches and background workers. Snapshots use
`cp --reflink=auto` on the source filesystem. Allow space for one base copy and
one copy per worker; without reflink support these are full physical copies.
`--timeout` defaults to 180 seconds per document; `--settle` defaults to 3 seconds
between typesetting passes. Both are adjustable. This checks loading/typesetting,
not all visual, editing or export behavior.

For a detached run:

```sh
nohup python3 /home/felix/data/Software/TeXmacs/texmacs/tools/stress/full-vault-load-audit.py /home/felix/data/Notes -j 4 > /tmp/athena-full-audit.log 2>&1 &
tail -f /tmp/athena-full-audit.log
```

The output directory is printed first; `--output PATH` selects a fresh directory
outside the source Vault. `selection.json` records the full ordered input list.
`summary.txt` and `summary.json` are atomically replaced after every completion.
Per-document logs are under `logs/NNN`, with indices matching the input list even
when completion order differs. Warnings, crashes and timeouts are findings;
other documents continue. Exit code is 0 for no findings, 1 for findings, or 130
after Ctrl+C. Ctrl+C stops only this audit's subprocess groups and retains partial
reports. Copies and logs are retained for investigation, not deleted automatically.

Harness tests:

```sh
python3 tools/stress/test_full_vault_load_audit.py
python3 tools/stress/test_random_vault_load_audit.py
```
