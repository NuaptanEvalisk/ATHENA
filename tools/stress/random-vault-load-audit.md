# Random Vault Load Audit

Run from any directory, with the Vault closed in ATHENA during the initial
snapshot:

```sh
python3 /home/felix/data/Software/TeXmacs/texmacs/tools/stress/random-vault-load-audit.py /home/felix/data/Notes
```

The default is 50 distinct randomly selected `.ath` documents under `Notes Root`.
The printed seed and `selection.json` make the selection reproducible; pass
`--seed NUMBER` to repeat it against the same file inventory. `--count`,
`--timeout` (180 seconds per document), and `--settle` (3 seconds between two
typesetting passes) are configurable.

The script takes an exclusive directory lease for the snapshot only. It copies
the complete Vault, preserving transclusion targets and styles, to a fresh
`.athena-load-audit-*` directory beside the original. GNU `cp` uses reflinks
where supported. After the copy, the original Vault may be reopened. The copy's
backup dispatchers and startup page are disabled. The test profile has no user
Hodarium membership or credentials. Symlinks are refused to prevent writes
through the copy into an external directory. Source documents are never passed
to the test processes.

Each document gets a fresh offscreen ATHENA process. It loads the copied Vault,
loads and switches to the selected document, forces typesetting on its owning
BufferActor, waits for asynchronous work, and forces typesetting again. It does
not invoke save or export. It captures both stdout/stderr and the same diagnostic
collection used by the Error messages pane. Warnings count as findings too.
This is a load/typesetting audit, not exhaustive visual, editing, or PDF testing.
An unaccepted BufferActor submission is retried on subsequent dispatcher turns
(at most 100 attempts per pass). Accepted commands are never retried. Exhausted
retries, native actor exceptions, and crashes after a successful typesetting
result still fail the audit.

Results appear as each process finishes. A crash or timeout is recorded and the
remaining documents continue. `summary.txt` and `summary.json` are updated after
every document; full output is in `logs/NNN/runtime.log`. Exit status is zero only
when every sampled document completes without diagnostics. The private Vault
and profile are retained for investigation; their location is printed first.

To detach and watch progress:

```sh
nohup python3 /home/felix/data/Software/TeXmacs/texmacs/tools/stress/random-vault-load-audit.py /home/felix/data/Notes > /tmp/athena-load-audit.log 2>&1 &
tail -f /tmp/athena-load-audit.log
```

Harness self-tests (no ATHENA instance required):

```sh
python3 /home/felix/data/Software/TeXmacs/texmacs/tools/stress/test_random_vault_load_audit.py
```
