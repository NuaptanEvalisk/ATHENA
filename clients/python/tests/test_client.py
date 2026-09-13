"""Real IPC and independent C++/Python SDK interoperability tests.

Copyright (C) 2026 Nuaptan Felix Evalisk.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

from athena_audmap import Client, ProtocolError
from athena_audmap.client import _decode, _encode


class CodecTests(unittest.TestCase):
    def test_values_and_validation(self):
        frame = [5, 1, 1, 1, "echo", {"unicode": "\u03b1\u4e2d", "binary": b"\0\xff", "nul": "a\0b"}]
        self.assertEqual(_decode(_encode(frame)), frame)
        for bad in ({"x": float("nan")}, {1: "not a string key"}, {"x": 2**64}):
            with self.assertRaises((TypeError, ValueError)):
                _encode([5, 1, 1, 1, "echo", bad])
        with self.assertRaises(ValueError):
            _decode(b"\x81\xa1x\xd4\x01\x00")
        with self.assertRaises(ValueError):
            _decode(b"\x82\xa1x\x01\xa1x\x02")


class ClientTests(unittest.TestCase):
    def test_real_server(self):
        server = subprocess.Popen([sys.argv[1], "--repl-test-server"],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            endpoint = server.stdout.readline().strip()
            self.assertTrue(endpoint)
            with tempfile.TemporaryDirectory(prefix="athena-sdk-test-") as directory:
                identity = Path(directory) / "shared.json"
                with Client(endpoint, identity, name="Python SDK test") as client:
                    ticket = client.resolve("@/fork/leaf", leaves=True)
                    handles, truncations = ticket.result(5).data
                    self.assertEqual(len(handles), 4)
                    self.assertEqual(truncations, [])
                    parameters = {"text": "\u4e2d\u03b1", "binary": b"\0\xff"}
                    requests = [client.operate(ticket.ticket, handles[0], "echo", {"index": n, **parameters})
                                for n in range(12)]
                    parameters["text"] = "caller mutated its copy"
                    for n, request in enumerate(requests):
                        response = request.result(5)
                        self.assertEqual(response.data["index"], n)
                        self.assertEqual(response.data["text"], "\u4e2d\u03b1")
                        self.assertEqual(client.ask(request.ticket, request.operation).result(5), response)
                        client.release(request.ticket, request.operation).result(5)
                        with self.assertRaises(ProtocolError):
                            client.ask(request.ticket, request.operation).result(5)
                    time.sleep(16)
                    lineage = client.lineage(ticket.ticket, handles[0]).result(5).data
                    self.assertEqual(len(lineage), 3)
                    client.finish(ticket.ticket).result(5)
                    cancelled = client.resolve("@/fork/leaf")
                    client.cancel(cancelled.ticket).result(5)
                    with self.assertRaises(RuntimeError):
                        cancelled.result(5)
                    client.close()
                    with self.assertRaises(RuntimeError):
                        client.resolve("@").result(5)
                original_key = json.loads(identity.read_text())["public_key"]
                # The standalone C++ example consumes exactly the same identity file.
                subprocess.run([sys.argv[2], endpoint, str(identity), "@/fork/leaf"],
                               check=True, timeout=10, capture_output=True, text=True)
                self.assertEqual(json.loads(identity.read_text())["public_key"], original_key)
                os.chmod(identity, 0o644)
                with self.assertRaises(PermissionError):
                    Client(endpoint, identity)
        finally:
            server.communicate("\n", timeout=10)
            self.assertEqual(server.returncode, 0)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
