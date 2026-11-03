#!/usr/bin/env python3
"""Tests for the in-sandbox upload and download boundary."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tarfile
import tempfile
import unittest


HELPER = Path(__file__).with_name("session-helper.py")
SPEC = importlib.util.spec_from_file_location("athena_web_session_helper", HELPER)
assert SPEC is not None and SPEC.loader is not None
helper = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(helper)


class SessionHelperTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        home = Path(self.temporary.name)
        helper.HOME = home
        helper.UPLOAD = home / "Desktop" / "Upload"
        helper.DOWNLOAD = home / "Desktop" / "Download"
        helper.UPLOAD.mkdir(parents=True)
        helper.DOWNLOAD.mkdir()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_lists_regular_files_and_rejects_symlinks(self) -> None:
        nested = helper.DOWNLOAD / "Results"
        nested.mkdir()
        (nested / "answer.ath").write_text("answer", encoding="utf-8")
        (helper.DOWNLOAD / "escape").symlink_to("/etc/passwd")

        entries = helper.download_entries()

        self.assertEqual(
            [{"path": "Results/answer.ath", "size": 6,
              "modified": entries[0]["modified"]}],
            entries,
        )
        with self.assertRaises(ValueError):
            helper.regular_download("escape")

    def test_finishes_upload_atomically(self) -> None:
        incoming = helper.UPLOAD / ".upload-1"
        incoming.write_bytes(b"content")

        helper.finish_upload(".upload-1", "Notes.ath")

        self.assertFalse(incoming.exists())
        self.assertEqual(b"content", (helper.UPLOAD / "Notes.ath").read_bytes())

    def test_archive_contains_only_regular_downloads(self) -> None:
        (helper.DOWNLOAD / "result.txt").write_text("result", encoding="utf-8")
        output = Path("/tmp") / "athena-web-helper-test.tar.gz"
        self.addCleanup(output.unlink, missing_ok=True)

        helper.archive_downloads(str(output))

        with tarfile.open(output, "r:gz") as archive:
            self.assertEqual(["result.txt"], archive.getnames())
            self.assertEqual(b"result", archive.extractfile("result.txt").read())


if __name__ == "__main__":
    unittest.main()
