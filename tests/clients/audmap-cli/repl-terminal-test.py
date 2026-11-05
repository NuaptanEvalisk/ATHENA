#!/usr/bin/env python3
# Copyright (C) 2026 Nuaptan Felix Evalisk. GPL-3.0-or-later.
"""Check real Readline prompt lifecycle and waiting states through a PTY."""

import os
import pty
import select
import signal
import subprocess
import sys
import tempfile
import time


def main():
    server = subprocess.Popen(
        [sys.argv[1], "--repl-test-server"], stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, text=True,
    )
    client = None
    master = slave = None
    try:
        assert select.select([server.stdout], [], [], 5)[0], "Test server did not start"
        endpoint = server.stdout.readline().strip()
        assert endpoint, "Missing test endpoint"
        with tempfile.TemporaryDirectory(prefix="athena-repl-terminal-") as home:
            master, slave = pty.openpty()
            client = subprocess.Popen(
                [sys.argv[2], "--endpoint", endpoint, "--identity", home + "/key.json"],
                stdin=slave, stdout=slave, stderr=slave,
                env={**os.environ, "TERM": "dumb"}, start_new_session=True,
            )
            os.close(slave)
            slave = None

            def until(marker):
                output = b""
                deadline = time.monotonic() + 5
                while marker not in output:
                    assert time.monotonic() < deadline, repr(output)
                    if select.select([master], [], [], 0.1)[0]:
                        output += os.read(master, 65536)
                return output.decode("utf-8").replace("\r", "")

            def command(text, prompt):
                os.write(master, (text + "\n").encode())
                return until(prompt.encode())

            until(b"audm> ")
            output = command("help", "audm> ")
            assert "Default vault" in output and output.count("audm> ") == 1, output
            os.write(master, b"@/vaults/@\n")
            output = until(b"handle ")
            # Finish reading the prompt if its last bytes arrived separately.
            if "> " not in output:
                output += until(b"> ")
            assert "Resolving ... done." in output, output
            assert "resolving>" not in output and "audm> " not in output, output
            assert "Handle  Parent  Target" in output and "selected" in output, output
            prompt = output[output.rfind("handle "):].strip() + " "
            output = command("help", prompt)
            assert "Select a target" in output and "parameters" in output, output
            assert output.count(prompt) == 1, output
            output = command("get", prompt)
            assert "Running operation ... done." in output and "OK" in output, output
            assert output.count(prompt) == 1, output
            output = command("exit", "audm> ")
            assert output.count("audm> ") == 1, output
            os.write(master, b"@\n")
            until(b"Resolving ...")
            os.kill(client.pid, signal.SIGINT)
            output = until(b"audm> ")
            assert "cancelled." in output and "handle " not in output, output
            os.write(master, b"exit\n")
            assert client.wait(timeout=5) == 0
        print("Readline prompts, waiting status, handle help and cancellation passed")
    finally:
        if client is not None and client.poll() is None:
            client.kill()
            client.wait()
        for fd in (master, slave):
            if fd is not None:
                os.close(fd)
        server.stdin.close()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()


if __name__ == "__main__":
    main()
