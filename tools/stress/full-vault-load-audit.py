#!/usr/bin/env python3
"""Load and typeset every .ath in a Vault, with isolated parallel workers.

Excludes .backup and .athena at every depth from the document selection.
The source Vault must be closed only during the initial snapshot.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import importlib.util
import json
import os
from pathlib import Path
import queue
import secrets
import subprocess
import threading
import time

spec = importlib.util.spec_from_file_location(
    'random_audit', Path(__file__).with_name('random-vault-load-audit.py'))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def documents(root):
    selected = []
    for folder, dirs, files in os.walk(root, followlinks=False):
        dirs[:] = sorted(name for name in dirs if name not in ('.backup', '.athena'))
        for name in files:
            if name.endswith('.ath'):
                path = Path(folder)/name
                if path.is_symlink():
                    raise RuntimeError(f'Refusing a document symlink: {path}')
                selected.append(path.relative_to(root))
    return sorted(selected)


def workers(output, vault, home, count):
    contexts = queue.Queue()
    for number in range(1, count + 1):
        root = output/'workers'/f'{number:03d}'
        root.mkdir(parents=True)
        for source, target in ((vault, root/'vault'), (home, root/'home')):
            subprocess.run(['cp', '-a', '--reflink=auto', '--', str(source), str(target)],
                           check=True)
        contexts.put((root/'vault', root/'home'))
        print(f'Worker snapshot {number}/{count} ready', flush=True)
    return contexts


def run_one(args, output, contexts, config, relative, index, stop):
    if stop.is_set():
        return dict(index=index, path=str(relative), status='CANCELLED', diagnostics=[])
    vault, home = contexts.get()
    try:
        entry = audit.check_one(args, output, vault, config, home, relative, index, stop)
    except Exception as error:
        entry = dict(path=str(relative), status='HARNESS_ERROR', diagnostics=[str(error)])
    finally:
        contexts.put((vault, home))
    return dict(entry, index=index)


def save_report(output, entries, total, interrupted=False):
    ordered = sorted(entries, key=lambda entry: entry['index'])
    state = dict(total=total, completed=len(entries), interrupted=interrupted,
                 findings=sum(entry['status'] != 'OK' for entry in entries),
                 results=ordered)
    lines = [f'Completed: {len(entries)}/{total}; findings: {state["findings"]}; '
             f'interrupted: {interrupted}']
    for entry in ordered:
        lines.append(f'{entry["status"]}: {entry["path"]}')
        lines.extend(f'  {message}' for message in entry['diagnostics'])
        if entry.get('result') not in (None, '#t'):
            lines.append(f'  Result: {entry["result"]}')
        if entry.get('log'):
            lines.append(f'  Log: {entry["log"]}')
    for name, content in (
            ('summary.json', json.dumps(state, ensure_ascii=False, indent=2)),
            ('summary.txt', '\n'.join(lines) + '\n')):
        temporary = output/(name + '.tmp')
        temporary.write_text(content)
        temporary.replace(output/name)


def run_checks(args, output, contexts, config, selected):
    stop = threading.Event()
    entries = []
    recorded = set()
    interrupted = False
    pool = ThreadPoolExecutor(max_workers=args.jobs)
    futures = []
    save_report(output, entries, len(selected))
    try:
        futures = [pool.submit(run_one, args, output, contexts, config, path, index, stop)
                   for index, path in enumerate(selected, 1)]
        for future in as_completed(futures):
            entry = future.result()
            recorded.add(future)
            entries.append(entry)
            print(f'[{len(entries)}/{len(selected)}] {entry["status"]}: {entry["path"]}',
                  flush=True)
            for diagnostic in entry['diagnostics']:
                print(f'  {diagnostic}', flush=True)
            if entry.get('result') not in (None, '#t'):
                print(f'  {entry["result"]}', flush=True)
            save_report(output, entries, len(selected))
    except KeyboardInterrupt:
        interrupted = True
        print('Stopping audit processes; retaining partial results...', flush=True)
    finally:
        stop.set()
        pool.shutdown(wait=True, cancel_futures=True)
        for future in futures:
            if future not in recorded and not future.cancelled():
                entries.append(future.result())
        save_report(output, entries, len(selected), interrupted)
    print(f'Report: {output / "summary.txt"}', flush=True)
    return 130 if interrupted else int(any(entry['status'] != 'OK' for entry in entries))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('vault', type=Path)
    parser.add_argument('-j', '--jobs', type=int, default=4,
                        help='Concurrent ATHENA processes (default: 4)')
    parser.add_argument('--timeout', type=int, default=180)
    parser.add_argument('--settle', type=float, default=3)
    parser.add_argument('--output', type=Path, help='New directory outside the source Vault')
    parser.add_argument('--binary', type=Path, default=audit.REPO/'ATHENA/bin/ATHENA.bin')
    parser.add_argument('--runtime', type=Path, default=audit.REPO/'build_qt6/athena-guile-runtime')
    parser.add_argument('--resources', type=Path, default=audit.REPO/'ATHENA')
    args = parser.parse_args()
    source = args.vault.resolve(strict=True)
    if args.jobs <= 0 or args.timeout <= 0 or not 0 <= args.settle < args.timeout:
        parser.error('Require jobs > 0 and 0 <= settle < timeout')
    for path in (args.binary, args.runtime, args.resources, source/'Vaultfile.json'):
        if not path.exists():
            parser.error(f'Missing required path: {path}')
    output = (args.output.resolve() if args.output else source.parent/
              f'.athena-full-audit-{time.strftime("%Y%m%d-%H%M%S")}-{secrets.token_hex(4)}')
    if output == source or source in output.parents:
        parser.error('Output must not be inside the source Vault')
    output.mkdir(parents=True, exist_ok=False)
    print(f'Output: {output}\nCreating private Vault snapshot...', flush=True)
    vault, config, home = audit.prepare(args, output)
    selected = documents(vault)
    if not selected:
        parser.error('No .ath files found outside .backup and .athena')
    args.jobs = min(args.jobs, len(selected))
    (output/'selection.json').write_text(json.dumps(dict(
        vault=str(source), excluded_directories=['.backup', '.athena'],
        jobs=args.jobs, selected=list(map(str, selected))), ensure_ascii=False, indent=2))
    print(f'Snapshot ready; original Vault may be reopened.\n'
          f'Checking {len(selected)} documents with {args.jobs} workers.', flush=True)
    contexts = workers(output, vault, home, args.jobs)
    return run_checks(args, output, contexts, config, selected)


if __name__ == '__main__':
    raise SystemExit(main())
