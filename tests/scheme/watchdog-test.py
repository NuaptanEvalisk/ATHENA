#!/usr/bin/env python3
"""Exercise the standalone ATHENA watchdog with a deterministic fake child."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--watchdog", required=True, type=Path)
parser.add_argument("--child", required=True, type=Path)
args = parser.parse_args()

with tempfile.TemporaryDirectory(prefix="athena-watchdog-") as temporary:
    root = Path(temporary)

    def run(mode: str):
        output = root / mode
        result = subprocess.run([
            str(args.watchdog.resolve()),
            "--heartbeat-timeout-ms=200",
            "--capture-schedule-ms=0,150",
            "--no-gdb",
            f"--output-dir={output}",
            "--",
            str(args.child.resolve()), mode,
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=8)
        assert result.returncode == 0, (mode, result.returncode, result.stderr)
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

print("PASS: standalone watchdog detects, snapshots and records recovery")
