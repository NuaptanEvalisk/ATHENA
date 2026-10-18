#!/usr/bin/env python3
"""Randomly load and typeset 50 Vault documents, with per-document diagnostics.

Close ATHENA on this Vault before starting. The script locks it only while
copying, then runs independent offscreen ATHENA processes against the copy.
No original document is loaded, edited, or saved by those processes.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import random
import re
import secrets
import shutil
import signal
import subprocess
import tempfile
import time

REPO = Path(__file__).resolve().parents[2]
DIAGNOSTIC = re.compile(
    r'\[(?:error|warning|critical)\]|\b[\w-]+-(?:error|warning)\b|'
    r'ATHENA fatal|buffer actor command failed|\bThrowing\b|Unbound variable|Backtrace:|Traceback|'
    r'(?i:segmentation fault|invalid rubber character)')


def inventory(root):
    result = {}
    for folder, dirs, files in os.walk(root, followlinks=False):
        for name in dirs + files:
            path = Path(folder)/name
            if path.is_symlink():
                raise RuntimeError(f'Refusing a snapshot with external symlink risk: {path}')
            if path.is_file():
                info = path.stat()
                result[str(path.relative_to(root))] = (info.st_size, info.st_mtime_ns)
    return result


def prepare(args, output):
    source = args.vault.resolve(strict=True)
    copied = output/'vault'
    fd = os.open(source, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RuntimeError('Vault is in use. Close ATHENA on it, then rerun.') from error
        before = inventory(source)
        subprocess.run(['cp', '-a', '--reflink=auto', '--', str(source), str(copied)], check=True)
        if before != inventory(source) or before != inventory(copied):
            raise RuntimeError('Vault changed during snapshot; refusing an inconsistent audit')
    finally:
        os.close(fd)
    config_path = copied/'Vaultfile.json'
    config = json.loads(config_path.read_text())
    if config.get('node_model_version') != 2:
        raise RuntimeError('This audit requires a native XML v3 Vault')
    config.update(backup_dispatchers=[], startup_page='', one_time_startup_page='')
    for key in ('map_path', 'namespace_db_path', 'preferences_path'):
        value = Path(config[key])
        if value.is_absolute() or '..' in value.parts:
            raise RuntimeError(f'External Vault storage is not supported by this audit: {key}')
    config_path.write_text(json.dumps(config, indent=2))
    home = output/'home'
    system = home/'profile/system'
    system.mkdir(parents=True)
    (system/'sys_state.json').write_text(json.dumps({
        'format': 'athena-system-state', 'version': 3}))
    for path in (copied/'dependencies').rglob('*'):
        if path.is_file() and path.suffix in ('.ats', '.ts'):
            target = home/'profile/styles'/path.relative_to(copied/'dependencies')
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
    return copied, config, home


def stop_owned_process(process):
    if process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
        process.wait(timeout=5)
    except ProcessLookupError:
        pass
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()


def check_one(args, output, vault, config, home, relative, index, stop_event=None):
    directory = output/'logs'/f'{index:03d}'
    directory.mkdir(parents=True)
    result = directory/'result.scm'
    messages = directory/'messages.txt'
    runtime = args.runtime.resolve()
    resources = args.resources.resolve()
    env = dict(os.environ)
    env.update({
        'HOME': str(home), 'ATHENA_HOME_PATH': str(home/'profile'),
        'XDG_CONFIG_HOME': str(home/'config'), 'XDG_CACHE_HOME': str(home/'cache'),
        'XDG_DATA_HOME': str(home/'data'), 'ATHENA_PATH': str(resources),
        'QT_QPA_PLATFORM': 'offscreen', 'GUILE_AUTO_COMPILE': '0',
        'GUILE_LOAD_PATH': str(runtime/'share/guile/3.0'),
        'GUILE_LOAD_COMPILED_PATH': str(runtime/'lib/guile/3.0/ccache'),
        'LD_LIBRARY_PATH': ':'.join((str(runtime/'lib'), str(resources/'lib'),
                                    env.get('LD_LIBRARY_PATH', ''))),
        'ATHENA_LOAD_AUDIT_RESULT': str(result),
        'ATHENA_LOAD_AUDIT_MESSAGES': str(messages),
        'ATHENA_LOAD_AUDIT_SOURCE': str(vault/relative),
        'ATHENA_LOAD_AUDIT_VAULT': str(vault),
        'ATHENA_LOAD_AUDIT_MAP': config['map_path'],
        'ATHENA_LOAD_AUDIT_NAMESPACES': config['namespace_db_path'],
        'ATHENA_LOAD_AUDIT_SETTLE_MS': str(int(args.settle*1000)),
    })
    script = Path(__file__).with_suffix('.scm')
    expression = f'(exec-global (lambda () (primitive-load {json.dumps(str(script))})))'
    started = time.monotonic()
    timed_out = False
    cancelled = False
    actor_failed = False
    before = hashlib.sha256((vault/relative).read_bytes()).hexdigest()
    with (directory/'runtime.log').open('w') as log:
        process = subprocess.Popen(
            [str(args.binary.resolve()), '-H', '-X', '-x', expression],
            cwd=home, env=env, start_new_session=True,
            stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + args.timeout
            while process.poll() is None:
                if stop_event is not None and stop_event.is_set():
                    cancelled = True
                    break
                # Native exceptions unwind past Scheme catch and are reported
                # by the actor boundary; the completion callback never runs.
                if 'buffer actor command failed' in (directory/'runtime.log').read_text(errors='replace'):
                    actor_failed = True
                    break
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    timed_out = True
                    break
                try:
                    process.wait(timeout=min(remaining, 0.2))
                except subprocess.TimeoutExpired:
                    pass
        finally:
            stop_owned_process(process)
    response = result.read_text() if result.exists() else 'No completion report'
    phase = Path(str(result) + '.phase')
    if response != '#t' and phase.exists():
        response += '\nLast phase: ' + phase.read_text()
    log = (directory/'runtime.log').read_text(errors='replace')
    diagnostics = list(dict.fromkeys(line for line in log.splitlines() if DIAGNOSTIC.search(line)))
    if messages.exists():
        for message in messages.read_text(errors='replace').splitlines():
            if message and message not in diagnostics:
                diagnostics.append(message)
    unchanged = before == hashlib.sha256((vault/relative).read_bytes()).hexdigest()
    status = ('CANCELLED' if cancelled else
              'FAILED' if actor_failed else
              'TIMEOUT' if timed_out else
              'FAILED' if process.returncode or response != '#t' else
              'CHANGED_COPY' if not unchanged else
              'DIAGNOSTICS' if diagnostics else 'OK')
    return dict(path=str(relative), status=status, returncode=process.returncode,
                seconds=round(time.monotonic()-started, 2), result=response,
                diagnostics=diagnostics, log=str(directory/'runtime.log'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('vault', type=Path)
    parser.add_argument('--count', type=int, default=50)
    parser.add_argument('--seed', type=int)
    parser.add_argument('--timeout', type=int, default=180)
    parser.add_argument('--settle', type=float, default=3)
    parser.add_argument('--output', type=Path, help='New directory outside the source Vault')
    parser.add_argument('--binary', type=Path, default=REPO/'ATHENA/bin/ATHENA.bin')
    parser.add_argument('--runtime', type=Path, default=REPO/'build_qt6/athena-guile-runtime')
    parser.add_argument('--resources', type=Path, default=REPO/'ATHENA')
    args = parser.parse_args()
    source = args.vault.resolve(strict=True)
    if args.count <= 0 or args.timeout <= 0 or not 0 <= args.settle < args.timeout:
        parser.error('Require count > 0 and 0 <= settle < timeout')
    for path in (args.binary, args.runtime, args.resources, source/'Vaultfile.json'):
        if not path.exists():
            parser.error(f'Missing required path: {path}')
    seed = args.seed if args.seed is not None else secrets.randbits(64)
    # Use the Vault's filesystem so GNU cp can reflink large assets and databases.
    output = (args.output.resolve() if args.output else
              source.parent/f'.athena-load-audit-{time.strftime("%Y%m%d-%H%M%S")}-{seed:x}')
    if output == source or source in output.parents:
        parser.error('Output must not be inside the source Vault')
    output.mkdir(parents=True, exist_ok=False)
    print(f'Output: {output}\nSeed: {seed}\nCreating private Vault snapshot...', flush=True)
    vault, config, home = prepare(args, output)
    candidates = sorted(path.relative_to(vault) for path in
                        (vault/'Notes Root').rglob('*.ath'))
    if not candidates:
        parser.error('No .ath files found under Notes Root')
    if len(candidates) < args.count:
        parser.error(f'Requested {args.count} files; only {len(candidates)} available')
    selected = random.Random(seed).sample(candidates, args.count)
    (output/'selection.json').write_text(json.dumps(dict(
        vault=str(source), seed=seed, selected=list(map(str, selected))), indent=2))
    summary = []
    for index, relative in enumerate(selected, 1):
        print(f'[{index}/{len(selected)}] {relative}', flush=True)
        try:
            entry = check_one(args, output, vault, config, home, relative, index)
        except KeyboardInterrupt:
            print(f'Interrupted. Partial results: {output}', flush=True)
            raise
        except Exception as error:
            entry = dict(path=str(relative), status='HARNESS_ERROR', diagnostics=[str(error)])
        summary.append(entry)
        print(f'  {entry["status"]}', flush=True)
        for message in entry['diagnostics']:
            print(f'    {message}', flush=True)
        if entry.get('result') not in (None, '#t'):
            print(f'    {entry["result"]}', flush=True)
        (output/'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2))
        with (output/'summary.txt').open('w') as report:
            for row in summary:
                report.write(f'{row["status"]}: {row["path"]}\n')
                for message in row['diagnostics']:
                    report.write(f'  {message}\n')
                if row.get('result') not in (None, '#t'):
                    report.write(f'  Result: {row["result"]}\n')
                if row.get('log'):
                    report.write(f'  Log: {row["log"]}\n')
    problems = sum(row['status'] != 'OK' for row in summary)
    print(f'Completed {len(summary)} documents: {problems} with diagnostics/failures.\n'
          f'Report: {output / "summary.txt"}', flush=True)
    return int(problems != 0)


if __name__ == '__main__':
    raise SystemExit(main())
