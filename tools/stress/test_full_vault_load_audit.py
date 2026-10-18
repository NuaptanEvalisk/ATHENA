#!/usr/bin/env python3
"""Parallel audit harness tests using fake ATHENA processes and private files."""
import argparse
import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import unittest

spec = importlib.util.spec_from_file_location(
    'full_audit', Path(__file__).with_name('full-vault-load-audit.py'))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class FullAuditTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.vault = self.root/'vault'
        self.home = self.root/'home'
        self.output = self.root/'output'
        for path in (self.vault, self.home, self.output):
            path.mkdir()
        self.args = argparse.Namespace(jobs=2, runtime=self.root, resources=self.root,
            settle=0, timeout=5, binary=self.root/'fake-athena')
        self.config = dict(map_path='map.sqlite', namespace_db_path='ns.sqlite')

    def document(self, name):
        path = self.vault/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('original')

    def test_traverses_whole_vault_and_prunes_excluded_directories(self):
        for name in ('root.ath', 'Notes Root/a.ath', 'outside/b.ath',
                     '.athena/cache.ath', '.backup/old.ath',
                     'outside/.athena/cache.ath', 'outside/.backup/old.ath',
                     'outside/not-ath.txt'):
            self.document(name)
        self.assertEqual(audit.documents(self.vault), list(map(Path,
            ('Notes Root/a.ath', 'outside/b.ath', 'root.ath'))))

    def test_parallel_processes_have_distinct_vaults_and_homes(self):
        for name in ('a.ath', 'b.ath', 'c.ath'):
            self.document(name)
        self.args.binary.write_text('''#!/usr/bin/env python3
import json, os, pathlib, time
result = pathlib.Path(os.environ['ATHENA_LOAD_AUDIT_RESULT'])
result.with_suffix('.started').write_text(str(time.monotonic()))
logs = result.parent.parent
deadline = time.monotonic() + 3
while len(list(logs.glob('*/result.started'))) < 2:
    if time.monotonic() > deadline:
        raise RuntimeError('Workers did not run concurrently')
    time.sleep(.02)
result.with_suffix('.context').write_text(json.dumps([
    os.environ['HOME'], os.environ['ATHENA_LOAD_AUDIT_VAULT']]))
source = pathlib.Path(os.environ['ATHENA_LOAD_AUDIT_SOURCE'])
if source.name == 'a.ath':
    os._exit(17)
if source.name == 'b.ath':
    pathlib.Path(os.environ['ATHENA_LOAD_AUDIT_MESSAGES']).write_text('typeset-warning: probe')
result.write_text('#t')
''')
        self.args.binary.chmod(0o755)
        contexts = audit.workers(self.output, self.vault, self.home, 2)
        result = audit.run_checks(self.args, self.output, contexts, self.config,
                                  audit.documents(self.vault))
        self.assertEqual(result, 1)
        summary = json.loads((self.output/'summary.json').read_text())
        self.assertEqual(summary['completed'], 3)
        self.assertEqual([row['status'] for row in summary['results']],
                         ['FAILED', 'DIAGNOSTICS', 'OK'])
        first = json.loads((self.output/'logs/001/result.context').read_text())
        second = json.loads((self.output/'logs/002/result.context').read_text())
        self.assertNotEqual(first[0], second[0])
        self.assertNotEqual(first[1], second[1])
        for name in ('a.ath', 'b.ath', 'c.ath'):
            self.assertEqual((self.vault/name).read_text(), 'original')

    def test_cancel_stops_active_process(self):
        self.document('a.ath')
        self.args.binary.write_text('#!/usr/bin/env python3\nimport time\ntime.sleep(60)\n')
        self.args.binary.chmod(0o755)
        stop = threading.Event()
        timer = threading.Timer(0.3, stop.set)
        timer.start()
        try:
            entry = audit.audit.check_one(self.args, self.output, self.vault,
                self.config, self.home, Path('a.ath'), 1, stop)
        finally:
            timer.cancel()
            timer.join()
        self.assertEqual(entry['status'], 'CANCELLED')
        self.assertLess(entry['seconds'], 3)


if __name__ == '__main__':
    unittest.main()
