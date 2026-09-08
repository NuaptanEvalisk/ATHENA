#!/usr/bin/env python3
"""Exercise the standalone ATHENA watchdog with a deterministic fake child."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--watchdog", required=True, type=Path)
parser.add_argument("--child", required=True, type=Path)
args = parser.parse_args()

with tempfile.TemporaryDirectory(prefix="athena-watchdog-") as temporary:
    root = Path(temporary)

    def run(mode: str, extra=(), expected=0):
        output = root / mode
        result = subprocess.run([
            str(args.watchdog.resolve()),
            "--heartbeat-timeout-ms=200",
            "--capture-schedule-ms=0,150",
            "--no-gdb",
            f"--output-dir={output}",
            "--",
            str(args.child.resolve()), mode, *extra,
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=8)
        assert result.returncode == expected, (mode, result.returncode, result.stderr)
        return output, result.stderr

    healthy, healthy_log = run("healthy")
    assert not list(healthy.glob("*")), (healthy_log, list(healthy.glob("*")))

    recovered, recovered_log = run("recover")
    recovered_incidents = [p for p in recovered.glob("*") if p.is_dir()]
    assert len(recovered_incidents) == 1, (recovered_log, recovered_incidents)
    incident = recovered_incidents[0]
    assert (incident / "meta.json").exists()
    assert (incident / "proc-000.txt").exists()
    assert (incident / "recovered.json").exists()
    assert "recovered" in recovered_log

    stalled, stalled_log = run("stall")
    stalled_incidents = [p for p in stalled.glob("*") if p.is_dir()]
    assert len(stalled_incidents) == 1, (stalled_log, stalled_incidents)
    incident = stalled_incidents[0]
    assert (incident / "meta.json").exists()
    assert (incident / "proc-000.txt").exists()
    assert (incident / "terminated.json").exists()
    proc_text = (incident / "proc-000.txt").read_text(errors="replace")
    assert "TID" in proc_text and "wchan" in proc_text, proc_text[-4000:]

    closed, closed_log = run("close-channel")
    closed_incidents = [p for p in closed.glob("*") if p.is_dir()]
    assert len(closed_incidents) == 1, (closed_log, closed_incidents)
    incident = closed_incidents[0]
    assert (incident / "proc-000.txt").exists()
    assert (incident / "terminated.json").exists()

    for mode in ("restart-ui", "restart-startup", "restart-failure"):
        record = root / f"{mode} child pids.txt"
        output, log = run(mode, (str(record),), 7 if mode == "restart-failure" else 0)
        processes = [tuple(map(int, row.split())) for row in record.read_text().splitlines()]
        if mode == "restart-failure":
            assert len(processes) == 1, (processes, log)
            continue
        assert len(processes) == 2, (processes, log)
        assert processes[0][0] != processes[1][0], processes
        assert processes[0][1] == processes[1][1], processes
        incidents = list(output.glob("*/meta.json"))
        assert len(incidents) == 1, (incidents, log)
        metadata = json.loads(incidents[0].read_text())
        assert metadata["pid"] == processes[1][0], metadata
        assert metadata["heartbeat_timeout_ms"] == 200, metadata
        assert (incidents[0].parent / "proc-000.txt").exists()

print("PASS: watchdog detects stalls, records recovery and supervises restarts")
