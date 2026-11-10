#!/usr/bin/env python3
"""Adapt the installed BCC memleak tool to a running mimalloc-based node.

Allocation tracking and symbolization remain owned by BCC's Apache-2.0 tool.
This adapter adds mimalloc's C++ entry points, excludes virtual mmap reservations
from heap accounting, and records bounded logs plus independent RSS telemetry.
"""

import argparse
import json
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import runpy
import signal
import sys
import threading


class LogStream:
    def __init__(self, logger):
        self.logger = logger
        self.local = threading.local()

    def write(self, value):
        pending = getattr(self.local, "pending", "") + value
        lines = pending.split("\n")
        self.local.pending = lines.pop()
        for line in lines:
            self.logger.info(line)
        return len(value)

    def flush(self):
        pending = getattr(self.local, "pending", "")
        if pending:
            self.logger.info(pending)
            self.local.pending = ""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pid", type=int)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--sample-rate", type=int, default=1024)
    parser.add_argument("--interval", type=int, default=60)
    parser.add_argument("--reports", type=int, default=1440)
    args = parser.parse_args()
    if min(args.pid, args.sample_rate, args.interval, args.reports) <= 0:
        parser.error("PID, sample rate, interval and report count must be positive")

    proc = Path(f"/proc/{args.pid}")
    maps = (proc / "maps").read_text()
    allocator_path = next(
        line.split(maxsplit=5)[5]
        for line in maps.splitlines()
        if "/libmimalloc.so." in line
    )
    allocator = str(proc / "root" / allocator_path.lstrip("/"))
    starttime = (proc / "stat").read_text().rsplit(")", 1)[1].split()[19]

    args.log.parent.mkdir(parents=True, exist_ok=True)
    logger = logging.getLogger("hodarium-memleak")
    logger.setLevel(logging.INFO)
    handler = RotatingFileHandler(args.log, maxBytes=8 * 1024 * 1024, backupCount=3)
    handler.setFormatter(logging.Formatter("%(asctime)s %(message)s"))
    logger.addHandler(handler)
    output = LogStream(logger)
    original_stdout, original_stderr = sys.stdout, sys.stderr
    sys.stdout = sys.stderr = output

    from bcc import BPF
    from bcc.table import StackTrace

    original_init = BPF.__init__
    original_uprobe = BPF.attach_uprobe
    original_uretprobe = BPF.attach_uretprobe
    original_depth = StackTrace.MAX_DEPTH
    tracker = []

    def initialize(instance, *positional, **kwargs):
        # This release lacks reliable frame chains. Keep the actual return PC,
        # not pseudo-frames made from data pointers that exhaust the stack map.
        declaration = "BPF_STACK_TRACE(stack_traces, 10240);"
        source = kwargs["text"]
        if source.count(declaration) != 1:
            raise RuntimeError("Installed BCC memleak stack declaration changed")
        kwargs["text"] = source.replace(declaration,
            "struct caller_trace { u64 ip[1]; };\n"
            "BPF_TABLE(\"stacktrace\", int, struct caller_trace, stack_traces, 16384);")
        original_init(instance, *positional, **kwargs)

    BPF.__init__ = initialize
    StackTrace.MAX_DEPTH = 1

    def attach(method, instance, kwargs):
        symbol = kwargs.get("sym")
        # mmap is allocator address-space reservation, not outstanding objects.
        # memleak otherwise requires mmap/munmap inside the selected allocator.
        if symbol in ("mmap", "munmap"):
            return instance
        result = method(instance, **kwargs)
        if symbol == "malloc":
            for extra in ("mi_new", "mi_new_nothrow"):
                method(instance, **{**kwargs, "sym": extra})
        if not tracker:
            tracker.append(instance)
        return result

    BPF.attach_uprobe = lambda instance, **kw: attach(original_uprobe, instance, kw)
    BPF.attach_uretprobe = lambda instance, **kw: attach(original_uretprobe, instance, kw)
    done = threading.Event()

    def observe():
        while not done.is_set():
            try:
                stat = (proc / "stat").read_text().rsplit(")", 1)[1].split()
                if stat[19] != starttime:
                    logger.error("Target PID was reused; stopping diagnostics")
                    os.kill(os.getpid(), signal.SIGTERM)
                    return
                fields = {}
                for name in ("status", "smaps_rollup"):
                    for line in (proc / name).read_text().splitlines():
                        key, separator, value = line.partition(":")
                        if separator and key in {
                            "VmRSS", "VmSize", "VmSwap", "RssAnon", "RssFile",
                            "Threads", "Pss_Anon", "Private_Dirty", "Anonymous",
                            "AnonHugePages", "LazyFree", "Swap",
                        }:
                            fields[key] = value.strip()
                fields.update(utime_ticks=int(stat[11]), stime_ticks=int(stat[12]))
                logger.info("MEMORY %s", json.dumps(fields, sort_keys=True))
                if tracker:
                    allocations = list(tracker[0]["allocs"].items())
                    now = BPF.monotonic_time()
                    logger.info("CAPTURE %s", json.dumps({
                        "tracked_samples": len(allocations),
                        "sample_bytes": sum(x.size for _, x in allocations),
                        "aged_samples": sum(now - x.timestamp_ns >= 60_000_000_000
                                            for _, x in allocations),
                        "unresolved_samples": sum(x.stack_id < 0 for _, x in allocations),
                        "stack_slots": len(tracker[0]["stack_traces"]),
                    }, sort_keys=True))
            except FileNotFoundError:
                logger.error("Target exited; stopping diagnostics")
                os.kill(os.getpid(), signal.SIGTERM)
                return
            done.wait(args.interval)

    observer = threading.Thread(target=observe, daemon=True)
    logger.info("START pid=%s starttime=%s sample=1/%s allocator=%s",
                args.pid, starttime, args.sample_rate, allocator_path)
    logger.info("MAPS\n%s", maps)
    logger.info("Heap samples exclude pre-attach allocations, mmap reservations, "
                "and custom heap/aligned C++ APIs not reached by standard hooks. "
                "Only the allocator return PC is captured; deeper stacks need "
                "a frame-pointer build. Sample bytes are "
                "unscaled estimates, not total live heap or proof of a leak.")
    observer.start()
    sys.argv = ["memleak-bpfcc", "-p", str(args.pid), "-O", allocator,
                "-s", str(args.sample_rate), "-o", "60000", "-T", "20",
                str(args.interval), str(args.reports)]
    try:
        runpy.run_path("/usr/sbin/memleak-bpfcc", run_name="__main__")
    except BaseException:
        logger.exception("Tracer stopped")
        raise
    finally:
        done.set()
        observer.join()
        output.flush()
        BPF.__init__ = original_init
        StackTrace.MAX_DEPTH = original_depth
        BPF.attach_uprobe, BPF.attach_uretprobe = original_uprobe, original_uretprobe
        sys.stdout, sys.stderr = original_stdout, original_stderr
        logger.info("STOP")


if __name__ == "__main__":
    main()
