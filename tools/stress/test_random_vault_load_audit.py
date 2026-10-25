#!/usr/bin/env python3
"""Harness-only tests; never start ATHENA or open a user's Vault."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    'audit', Path(__file__).with_name('random-vault-load-audit.py'))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class AuditTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root/'source'
        (self.source/'Notes Root').mkdir(parents=True)
        (self.source/'Notes Root/example.ath').write_text('original')
        self.config = dict(node_model_version=2, backup_dispatchers=[{'destination': '/forbidden'}],
                           map_path='map.sqlite', namespace_db_path='ns.sqlite',
                           preferences_path='vprefs.json', startup_page='user-page')
        (self.source/'Vaultfile.json').write_text(json.dumps(self.config))
        self.output = self.root/'output'
        self.output.mkdir()
        self.args = argparse.Namespace(vault=self.source, runtime=self.root,
            resources=self.root, settle=0, timeout=1, binary=self.root/'fake-athena')

    def test_snapshot_keeps_source_and_removes_backup_targets(self):
        original = (self.source/'Vaultfile.json').read_bytes()
        vault, config, home = audit.prepare(self.args, self.output)
        self.assertEqual((self.source/'Vaultfile.json').read_bytes(), original)
        self.assertEqual(config['backup_dispatchers'], [])
        self.assertEqual(config['startup_page'], '')
        self.assertEqual((vault/'Notes Root/example.ath').read_text(), 'original')
        self.assertTrue((home/'profile/system/sys_state.json').is_file())

    def test_snapshot_refuses_symlinks(self):
        (self.source/'external').symlink_to(self.root)
        with self.assertRaisesRegex(RuntimeError, 'symlink'):
            audit.prepare(self.args, self.output)

    def run_fake(self, body):
        self.args.binary.write_text('#!/usr/bin/env python3\n'
            'import os, pathlib, time\n' + body)
        self.args.binary.chmod(0o755)
        home = self.root/'home'
        home.mkdir()
        return audit.check_one(self.args, self.output, self.source,
            self.config, home, Path('Notes Root/example.ath'), 1)

    def test_success_requires_completion_report(self):
        entry = self.run_fake('pass\n')
        self.assertEqual(entry['status'], 'FAILED')

    def test_success(self):
        entry = self.run_fake(
            'pathlib.Path(os.environ["ATHENA_LOAD_AUDIT_RESULT"]).write_text("#t")\n')
        self.assertEqual(entry['status'], 'OK')

    def test_error_pane_diagnostics_are_reported(self):
        entry = self.run_fake(
            'pathlib.Path(os.environ["ATHENA_LOAD_AUDIT_RESULT"]).write_text("#t")\n'
            'pathlib.Path(os.environ["ATHENA_LOAD_AUDIT_MESSAGES"]).write_text('
            '"typeset-warning: invalid rubber character\\n")\n')
        self.assertEqual(entry['status'], 'DIAGNOSTICS')
        self.assertIn('invalid rubber', entry['diagnostics'][0])

    def test_timeout(self):
        entry = self.run_fake('time.sleep(30)\n')
        self.assertEqual(entry['status'], 'TIMEOUT')

    def test_native_actor_failure_does_not_wait_for_timeout(self):
        self.args.timeout = 10
        entry = self.run_fake('print("[2026-10-25 10:30:00.000] [error] buffer actor command failed: probe", flush=True)\n'
                              'time.sleep(30)\n')
        self.assertEqual(entry['status'], 'FAILED')
        self.assertLess(entry['seconds'], 3)
        self.assertIn('command failed: probe', entry['diagnostics'][0])

    def test_crash(self):
        entry = self.run_fake('os._exit(17)\n')
        self.assertEqual(entry['status'], 'FAILED')
        self.assertEqual(entry['returncode'], 17)


if __name__ == '__main__':
    unittest.main()
