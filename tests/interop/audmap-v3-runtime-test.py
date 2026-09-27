#!/usr/bin/env python3
"""Exercise document-model v3 over a real isolated ATHENA AUDMAP connection."""

import argparse
import ctypes
import ctypes.util
import json
import os
from pathlib import Path
import select
import shlex
import signal
import shutil
import subprocess
import sys
import tempfile
import time


BODY_ID = "11111111-1111-4111-8111-111111111111"
PARAGRAPH_ID = "22222222-2222-4222-8222-222222222222"


def fixture():
    return f'''<?xml version="1.0" encoding="UTF-8"?>
<athena-document version="2" text-model="utf-8"><node tag="document"><node tag="style"><text><value>generic</value></text></node><node tag="body"><node tag="document" id="{BODY_ID}"><node tag="concat" id="{PARAGRAPH_ID}"><text><value>Alpha </value></text><node tag="em"><text><value>inline</value></text></node></node></node></node></node></athena-document>
'''.encode()


def private_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    os.chmod(path.parent, 0o700)
    path.write_text(json.dumps(data), encoding="utf-8")
    os.chmod(path, 0o600)


def curve_keypair():
    library = ctypes.util.find_library("zmq")
    if not library:
        raise RuntimeError("libzmq is unavailable")
    zmq = ctypes.CDLL(library)
    public = ctypes.create_string_buffer(41)
    secret = ctypes.create_string_buffer(41)
    if zmq.zmq_curve_keypair(public, secret):
        raise RuntimeError("CURVE key generation failed")
    return public.value.decode("ascii"), secret.value.decode("ascii")


def build_stdio_client(repository, build_dir, output):
    """Compile only the current client/connection sources as a disposable probe.

    ATHENA.bin is the only CMake build target used by this integration block.
    This keeps the protocol probe current without building another project
    target or installing an SDK/runtime binary.
    """
    commands = json.loads((build_dir / "compile_commands.json").read_text())
    wanted = {
        str((repository / "tools/interop/client.cpp").resolve()): output.with_suffix(".client.o"),
        str((repository / "tools/interop/connection.cpp").resolve()): output.with_suffix(".connection.o"),
    }
    for source, object_path in wanted.items():
        entry = next((item for item in commands if str(Path(item["file"]).resolve()) == source), None)
        if not entry:
            raise RuntimeError(f"Missing compile command for {source}")
        argv = shlex.split(entry["command"])
        argv[argv.index("-o") + 1] = str(object_path)
        argv[argv.index("-c") + 1] = source
        subprocess.run(argv, cwd=entry["directory"], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

    link_dir = build_dir / "src/ATHENA/Interop"
    argv = shlex.split((link_dir / "CMakeFiles/athena-audmap.dir/link.txt").read_text())
    argv = [arg for arg in argv if not arg.startswith("-Wl,--dependency-file=")]
    if argv and argv[0] == "ccache":
        argv.pop(0)
    old_client = next(i for i, arg in enumerate(argv) if arg.endswith("client.cpp.o"))
    argv[old_client] = str(wanted[str((repository / "tools/interop/client.cpp").resolve())])
    old_connection = argv.index("libathena_audmap_client.a")
    argv[old_connection] = str(wanted[str((repository / "tools/interop/connection.cpp").resolve())])
    argv[argv.index("-o") + 1] = str(output)
    subprocess.run(argv, cwd=link_dir, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


class StdioClient:
    def __init__(self, executable, endpoint, identity, log_path):
        self._log = log_path.open("w")
        self.process = subprocess.Popen(
            [str(executable), "--stdio", "--endpoint", str(endpoint),
             "--identity", str(identity)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self._log,
            text=True, bufsize=1)
        welcome = self.receive(20)
        if welcome[0] != 101 or welcome[1].get("protocol_version") != 2 or \
                welcome[1].get("document_model_version") != 3:
            raise RuntimeError(f"Unexpected AUDMAP welcome: {welcome}")

    def close(self):
        if self.process.stdin:
            self.process.stdin.close()
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self._log.close()

    def send(self, frame):
        self.process.stdin.write(json.dumps(frame, separators=(",", ":")) + "\n")
        self.process.stdin.flush()

    def receive(self, timeout=20):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                self._log.flush()
                raise RuntimeError(f"AUDMAP probe exited {self.process.returncode}")
            ready, _, _ = select.select([self.process.stdout], [], [], 0.1)
            if not ready:
                continue
            line = self.process.stdout.readline()
            if not line:
                continue
            return json.loads(line)
        raise TimeoutError("Timed out waiting for AUDMAP frame")

    def wait(self, predicate, timeout=20):
        deadline = time.monotonic() + timeout
        seen = []
        while time.monotonic() < deadline:
            frame = self.receive(max(0.1, deadline - time.monotonic()))
            seen.append(frame)
            if predicate(frame):
                return frame
        raise TimeoutError(f"Expected AUDMAP frame; saw {seen}")


def wait_descriptor(runtime, process, log_path):
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        choices = list(runtime.glob("athena-audmap-*/connection.json"))
        if choices:
            data = json.loads(choices[0].read_text())
            if data.get("protocol_version") != 2 or data.get("document_model_version") != 3:
                raise RuntimeError(f"Wrong AUDMAP versions in descriptor: {data}")
            return choices[0]
        if process.poll() is not None:
            raise RuntimeError(f"ATHENA exited before AUDMAP startup:\n{log_path.read_text(errors='replace')}")
        time.sleep(0.05)
    raise RuntimeError(f"Timed out waiting for AUDMAP descriptor:\n{log_path.read_text(errors='replace')}")


def operation(client, ticket, oid, handle, command, parameters=None):
    client.send([5, ticket, oid, handle, command, {} if parameters is None else parameters])
    client.wait(lambda f: f[0] == 2 and len(f) == 3 and f[1:3] == [ticket, oid])
    response = client.wait(lambda f: f[0] in (6, 7) and f[1:3] == [ticket, oid])
    client.send([8, ticket, oid])
    client.wait(lambda f: f[0] == 2 and len(f) == 3 and f[1:3] == [ticket, oid])
    if response[0] == 7:
        raise RuntimeError(f"AUDMAP operation protocol error: {response}")
    return {"status": response[3], "data": response[4]}


def resolve_one(client, selector):
    ticket = resolve_one.next_ticket
    resolve_one.next_ticket += 1
    client.send([1, ticket, selector, [1]])
    client.wait(lambda f: f[0] == 2 and len(f) == 2 and f[1] == ticket)
    response = client.wait(lambda f: f[0] in (4, 7) and f[1] == ticket)
    if response[0] == 7:
        raise RuntimeError(f"Resolution failed: {response}")
    handles, truncated = response[2]
    if truncated or len(handles) != 1:
        raise RuntimeError(f"Expected one untruncated result for {selector}: {response}")
    return ticket, handles[0]


resolve_one.next_ticket = 1


def finish(client, ticket):
    client.send([11, ticket])
    client.wait(lambda f: f[0] == 2 and len(f) == 2 and f[1] == ticket)


def properties(tree):
    return {entry["name"]: entry["value"] for entry in tree.get("properties", [])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()

    repository = Path(__file__).resolve().parents[2]
    # SDK constants are part of the public integration surface even though this
    # machine intentionally has no third-party Python msgpack/zmq packages.
    python_sdk = (repository / "clients/python/src/athena_audmap/client.py").read_text()
    if "PROTOCOL_VERSION = 2" not in python_sdk or "DOCUMENT_MODEL_VERSION = 3" not in python_sdk:
        raise RuntimeError("Python SDK does not advertise AUDMAP 2 / document model 3")

    artifacts = args.artifacts.resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="audmap-v3-", dir=artifacts))
    profile = root / "profile"
    system = profile / "system"
    system.mkdir(parents=True, mode=0o700)
    (system / "sys_state.json").write_text(json.dumps({
        "format": "athena-system-state", "version": 2,
        "compatibility_version": "2.1.4",
    }))
    source = root / "source.ath"
    source.write_bytes(fixture())

    public, secret = curve_keypair()
    identity = root / "client/identity.json"
    private_json(identity, {"version": 1,
                            "public_key": public,
                            "secret_key": secret})
    private_json(system / "audmap/clients.json", {
        "version": 1,
        "clients": {public: {"allow": True, "trust": 0}},
    })
    # ZeroMQ IPC uses sockaddr_un and therefore needs a short filesystem path;
    # keep protocol evidence under the artifact root, but bind the socket below
    # /tmp rather than below the already-long build artifact path.
    runtime_dir = Path(tempfile.mkdtemp(prefix="a3-", dir="/tmp"))
    os.chmod(runtime_dir, 0o700)

    runtime = args.runtime.resolve()
    resources = args.resources.resolve()
    env = dict(os.environ)
    env.update({
        "HOME": str(root), "ATHENA_HOME_PATH": str(profile),
        "XDG_CONFIG_HOME": str(root / "config"),
        "XDG_CACHE_HOME": str(root / "cache"),
        "XDG_DATA_HOME": str(root / "data"),
        "XDG_RUNTIME_DIR": str(runtime_dir),
        "ATHENA_PATH": str(resources), "QT_QPA_PLATFORM": "offscreen",
        "GUILE_AUTO_COMPILE": "0",
        "GUILE_LOAD_PATH": str(runtime / "share/guile/3.0"),
        "GUILE_LOAD_COMPILED_PATH": str(runtime / "lib/guile/3.0/ccache"),
        "LD_LIBRARY_PATH": ":".join((str(runtime / "lib"),
                                     str(resources / "lib"),
                                     env.get("LD_LIBRARY_PATH", ""))),
    })
    quoted = json.dumps(str(source))
    expression = (
        '(exec-global (lambda () '
        f'(let ((name (string->url {quoted}))) '
        '(when (buffer-load name) (error "AUDMAP v3 fixture load failed")) '
        '(switch-to-buffer name))))')
    log_path = root / "runtime.log"
    probe = root / "athena-audmap-v3-probe"
    build_stdio_client(repository, args.binary.resolve().parents[1], probe)
    with log_path.open("w") as log:
        process = subprocess.Popen(
            [str(args.binary.resolve()), "-X", "-no-splash-screen", "-x", expression],
            cwd=root, env=env, start_new_session=True,
            stdout=log, stderr=subprocess.STDOUT)
        try:
            endpoint = wait_descriptor(runtime_dir, process, log_path)
            # The startup command and interop server are both scheduled around
            # the GUI loop; wait until the selected buffer becomes resolvable.
            client = StdioClient(probe, endpoint, identity, root / "client-1.log")
            try:
                inline = "@/buffers/@/document/body/[0]/[0]/[1]"
                deadline = time.monotonic() + 30
                while True:
                    try:
                        ticket, handle = resolve_one(client, inline)
                        break
                    except Exception:
                        if time.monotonic() >= deadline:
                            raise
                        time.sleep(0.05)

                oid = 1
                before = operation(client, ticket, oid, handle, "get"); oid += 1
                if before["status"] != "OK" or "id" in before["data"]["tree"]:
                    raise RuntimeError(f"Inline node should begin anonymous: {before}")
                buffer_id = before["data"].get("buffer_id")
                if type(buffer_id) is not int or buffer_id <= 0 or \
                        Path(before["data"].get("url", "")).resolve() != source.resolve():
                    raise RuntimeError(f"AUDMAP target is not the fixture buffer: {before['data']}")
                stable = f"@/buffers/[{buffer_id}]/document/body/[0]"
                inspected = operation(client, ticket, oid, handle, "inspect"); oid += 1
                for command in ("assign_id", "update_properties", "set"):
                    if command not in inspected["data"]:
                        raise RuntimeError(f"v3 inspect omitted {command}: {inspected['data']}")

                assigned = operation(client, ticket, oid, handle, "assign_id"); oid += 1
                if assigned["status"] != "OK" or not assigned["data"].get("id"):
                    raise RuntimeError(f"assign_id failed: {assigned}")
                persistent = assigned["data"]["id"]
                again = operation(client, ticket, oid, handle, "assign_id"); oid += 1
                if again["data"].get("id") != persistent or again["data"].get("changed"):
                    raise RuntimeError(f"assign_id was not idempotent: {again}")

                edited = operation(client, ticket, oid, handle, "update_properties", {
                    "set": [
                        {"name": "example:note",
                         "value": {"type": "string", "value": "hello"}},
                        {"name": "example:count",
                         "value": {"type": "int64", "value": "42"}},
                    ],
                    "remove": [],
                }); oid += 1
                if edited["status"] != "OK" or edited["data"].get("id") != persistent:
                    raise RuntimeError(f"update_properties failed: {edited}")
                current = operation(client, ticket, oid, handle, "get"); oid += 1
                if current["data"]["tree"].get("id") != persistent:
                    raise RuntimeError("Persistent UUID was not returned by document-model-v3 get")
                props = properties(current["data"]["tree"])
                if props.get("example:note") != {"type": "string", "value": "hello"} or \
                        props.get("example:count") != {"type": "int64", "value": "42"}:
                    raise RuntimeError(f"Typed properties did not round-trip: {props}")

                arbitrary = operation(client, ticket, oid, handle, "set", {
                    "tree": {"tag": "em", "id": "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
                             "children": [{"text": "bad"}]},
                }); oid += 1
                if arbitrary["status"] != "INVALID_ARGUMENT":
                    raise RuntimeError(f"Structural set accepted arbitrary UUID: {arbitrary}")
                finish(client, ticket)

                paragraph_ticket, paragraph_handle = resolve_one(
                    client, stable + "/[0]")
                inserted = operation(client, paragraph_ticket, oid, paragraph_handle,
                                     "insert_after", {"siblings": [{"text": "Inserted over AUDMAP"}]})
                oid += 1
                if inserted["status"] != "OK":
                    raise RuntimeError(f"insert_after failed: {inserted}")
                finish(client, paragraph_ticket)

                new_ticket, new_handle = resolve_one(
                    client, stable + "/[1]")
                new_node = operation(client, new_ticket, oid, new_handle, "get"); oid += 1
                generated = new_node["data"]["tree"].get("id")
                if not generated or new_node["data"]["tree"].get("text") != "Inserted over AUDMAP":
                    raise RuntimeError(f"Live v2 structural edit did not finalize identity: {new_node}")
                replaced = operation(client, new_ticket, oid, new_handle, "set",
                                     {"tree": {"text": "Replaced over AUDMAP"}})
                oid += 1
                if replaced["status"] != "OK":
                    raise RuntimeError(f"set failed: {replaced}")
                after_set = operation(client, new_ticket, oid, new_handle, "get"); oid += 1
                if after_set["status"] != "STALE":
                    raise RuntimeError(f"Replaced node handle did not become stale: {after_set}")
                finish(client, new_ticket)

                replaced_ticket, replaced_handle = resolve_one(client, stable + "/[1]")
                replacement = operation(client, replaced_ticket, oid, replaced_handle, "get"); oid += 1
                if replacement["status"] != "OK" or \
                        replacement["data"]["tree"].get("id") != generated or \
                        replacement["data"]["tree"].get("text") != "Replaced over AUDMAP":
                    raise RuntimeError(
                        f"Re-resolved replacement lost content or persistent UUID: {replacement}")
                finish(client, replaced_ticket)
            finally:
                client.close()

            # A fresh logical connection gets fresh ticket/handle scope but the
            # source UUID/property state belongs to the document, not the connection.
            client = StdioClient(probe, endpoint, identity, root / "client-2.log")
            try:
                ticket, handle = resolve_one(client, stable + "/[0]/[1]")
                reloaded = operation(client, ticket, 1000, handle, "get")
                if reloaded["data"]["tree"].get("id") != persistent or \
                        properties(reloaded["data"]["tree"]).get("example:note") != \
                        {"type": "string", "value": "hello"}:
                    raise RuntimeError(f"Reconnect lost persistent node metadata: {reloaded}")
                finish(client, ticket)
            finally:
                client.close()

            print(f"ATHENA-AUDMAP-V3-PASS; negotiation/node metadata/structural edit/reconnect; {root}")
        finally:
            try:
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=15)
            except ProcessLookupError:
                pass
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
            shutil.rmtree(runtime_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
