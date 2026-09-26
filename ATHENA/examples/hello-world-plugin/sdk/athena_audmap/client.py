"""Authenticated AUDMAP requests, persistent identities and socket ownership.

Copyright (C) 2026 Nuaptan Felix Evalisk.
SPDX-License-Identifier: GPL-3.0-or-later
"""
from concurrent.futures import Future
from dataclasses import dataclass
from pathlib import Path
import fcntl
import json
import math
import os
import queue
import stat
import tempfile
import threading
import time

import msgpack
import zmq

WIRE_LIMIT = 8 * 1024 * 1024
ENDPOINT_DESCRIPTOR_VERSION = 2
PROTOCOL_VERSION = 2
DOCUMENT_MODEL_VERSION = 2


class ProtocolError(RuntimeError):
    def __init__(self, frame):
        self.frame = frame
        super().__init__(str(frame))


@dataclass(frozen=True)
class Response:
    status: str
    data: object


@dataclass(frozen=True)
class PendingRequest:
    ticket: int
    operation: int
    future: Future

    def result(self, timeout=None):
        """Timeout does not cancel, retry, or undo the request."""
        return self.future.result(timeout)


def _private_directory(path, create=False):
    if create:
        path.parent.mkdir(parents=True, exist_ok=True)
        try:
            path.mkdir(mode=0o700)
        except FileExistsError:
            pass
    st = path.lstat()
    if not stat.S_ISDIR(st.st_mode) or st.st_uid != os.getuid() or st.st_mode & 0o077:
        raise PermissionError("AUDMAP directory must be owned by this user with mode 0700")


def _private_open(path, flags):
    fd = os.open(path, flags | os.O_CLOEXEC | os.O_NOFOLLOW, 0o600)
    st = os.fstat(fd)
    if not stat.S_ISREG(st.st_mode) or st.st_uid != os.getuid() or st.st_mode & 0o077:
        os.close(fd)
        raise PermissionError("AUDMAP file must be private and owned by this user")
    return fd


def _private_json(path, limit=1024 * 1024):
    with os.fdopen(_private_open(path, os.O_RDONLY), "rb") as stream:
        raw = stream.read(limit + 1)
        if len(raw) > limit:
            raise ValueError("AUDMAP private file too large")
        return json.loads(raw)


def _identity(path):
    _private_directory(path.parent, create=True)
    with os.fdopen(_private_open(Path(str(path) + ".lock"), os.O_CREAT | os.O_RDWR), "r+b") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            data = _private_json(path)
        except FileNotFoundError:
            public, secret = zmq.curve_keypair()
            data = {"version": 1, "public_key": public.decode("ascii"), "secret_key": secret.decode("ascii")}
            fd, temporary = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
            try:
                with os.fdopen(fd, "w", encoding="utf-8") as stream:
                    json.dump(data, stream)
                    stream.flush()
                    os.fsync(stream.fileno())
                os.replace(temporary, path)
                directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
                try:
                    os.fsync(directory)
                finally:
                    os.close(directory)
            finally:
                if os.path.exists(temporary):
                    os.unlink(temporary)
    if data.get("version") != 1:
        raise ValueError("Unknown identity version")
    public = data["public_key"].encode("ascii")
    secret = data["secret_key"].encode("ascii")
    if len(public) != 40 or len(secret) != 40 or b"\0" in public + secret or zmq.curve_public(secret) != public:
        raise ValueError("Invalid CURVE key pair")
    return public, secret


def _descriptor(path):
    path = Path(path).absolute()
    _private_directory(path.parent)
    data = _private_json(path, 65536)
    if (data.get("version") != ENDPOINT_DESCRIPTOR_VERSION or
            data.get("protocol_version") != PROTOCOL_VERSION or
            data.get("document_model_version") != DOCUMENT_MODEL_VERSION or
            not data["endpoint"].startswith("ipc://")):
        raise ValueError("Incompatible AUDMAP endpoint/protocol/document model version")
    return data


def _discover():
    root = Path(os.environ.get("XDG_RUNTIME_DIR") or "/tmp")
    choices = []
    for directory in root.glob("athena-audmap-*"):
        try:
            path = directory / "connection.json"
            data = _descriptor(path)
            if type(data["pid"]) is int and data["pid"] > 0:
                os.kill(data["pid"], 0)
                if (directory / "socket").exists():
                    choices.append(path)
        except (OSError, ValueError, KeyError):
            continue
    if len(choices) != 1:
        raise RuntimeError("Expected one ATHENA endpoint; supply endpoint explicitly. Found: " + str(choices))
    return choices[0]


def _validate(value, depth=0):
    if depth > 64:
        raise ValueError("AUDMAP nesting exceeds 64")
    if value is None or type(value) in (bool, bytes):
        return
    if type(value) is str:
        value.encode("utf-8", errors="strict")
        return
    if type(value) is int:
        if not -(2**63) <= value < 2**64:
            raise ValueError("AUDMAP integer out of range")
        return
    if type(value) is float:
        if not math.isfinite(value):
            raise ValueError("AUDMAP numbers must be finite")
        return
    if type(value) is list:
        if len(value) > 65536:
            raise ValueError("AUDMAP array too large")
        for child in value:
            _validate(child, depth + 1)
        return
    if type(value) is dict:
        if len(value) > 65536:
            raise ValueError("AUDMAP map too large")
        for key, child in value.items():
            if type(key) is not str:
                raise ValueError("AUDMAP map keys must be strings")
            key.encode("utf-8", errors="strict")
            _validate(child, depth + 1)
        return
    raise TypeError("Unsupported AUDMAP value: " + type(value).__name__)


def _unique_map(pairs):
    result = {}
    for key, value in pairs:
        if type(key) is not str or key in result:
            raise ValueError("Duplicate or non-string AUDMAP map key")
        result[key] = value
    return result


def _encode(frame):
    _validate(frame)
    raw = msgpack.packb(frame, use_bin_type=True)
    if len(raw) > WIRE_LIMIT:
        raise ValueError("AUDMAP message too large")
    return raw


def _decode(raw):
    if len(raw) > WIRE_LIMIT:
        raise ValueError("AUDMAP message too large")
    frame = msgpack.unpackb(raw, raw=False, object_pairs_hook=_unique_map,
                           max_array_len=65536, max_map_len=65536,
                           max_str_len=WIRE_LIMIT, max_bin_len=WIRE_LIMIT)
    try:
        _validate(frame)
    except TypeError as error:
        raise ValueError(str(error)) from error
    if not isinstance(frame, list) or not frame or type(frame[0]) is not int or not 0 <= frame[0] <= 255:
        raise ValueError("Invalid AUDMAP frame")
    return frame


class Client:
    """Thread-safe requests with a private socket thread and automatic heartbeat.

    Use as a context manager, or call close(). Blocking on a request's result is
    optional. Non-OK operation statuses are returned, protocol ERR raises
    ProtocolError. An operation's result can be recovered with ask before release.
    """
    def __init__(self, endpoint=None, identity=None, name="AUDMAP Python client", authorization_timeout=300):
        config = Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config")
        self._endpoint = endpoint
        self._identity = Path(identity or config / "athena-audmap" / "python.json").absolute()
        self._name = name
        self._timeout = authorization_timeout
        self._queue = queue.Queue()
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._ready = Future()
        self._failure = None
        self._next_ticket = 1
        self._next_operation = 1
        self._thread = threading.Thread(target=self._run, name="AUDMAP-client", daemon=True)
        self._thread.start()
        try:
            self._ready.result()
        except BaseException:
            self.close()
            raise

    def __enter__(self):
        return self

    def __exit__(self, *unused):
        self.close()

    def close(self):
        with self._lock:
            self._stop.set()
        self._thread.join()

    def _enqueue(self, op, ticket, operation, tail):
        future = Future()
        future.set_running_or_notify_cancel()
        frame = [op, ticket]
        if operation:
            frame.append(operation)
        frame.extend(tail)
        # Freeze caller-owned containers now, before the I/O thread observes them.
        raw = _encode(frame)
        frame = _decode(raw)
        with self._lock:
            if self._stop.is_set():
                future.set_exception(self._failure or RuntimeError("AUDMAP client is closed"))
            else:
                self._queue.put((frame, raw, future))
        return PendingRequest(ticket, operation, future)

    def resolve(self, selector, *, leaves=False, limit=None):
        if type(selector) is not str or (limit is not None and (not leaves or type(limit) is not int or limit <= 0)):
            raise ValueError("A positive result limit requires leaves projection")
        with self._lock:
            ticket = self._next_ticket
            self._next_ticket += 1
        projection = [int(leaves)]
        if limit is not None:
            projection.append(limit)
        return self._enqueue(1, ticket, 0, [selector, projection])

    def operate(self, ticket, handle, command, parameters=None):
        if type(parameters) not in (dict, type(None)) or type(command) is not str:
            raise ValueError("OPR requires a command and parameter map")
        self._ids(ticket, handle)
        with self._lock:
            operation = self._next_operation
            self._next_operation += 1
        return self._enqueue(5, ticket, operation, [handle, command, {} if parameters is None else parameters])

    def lineage(self, ticket, handle):
        self._ids(ticket, handle)
        with self._lock:
            operation = self._next_operation
            self._next_operation += 1
        return self._enqueue(9, ticket, operation, [handle])

    def ask(self, ticket, operation=0):
        self._ids(ticket)
        if type(operation) is not int or operation < 0:
            raise ValueError("Invalid operation ID")
        return self._enqueue(3, ticket, operation, [])

    def release(self, ticket, operation):
        self._ids(ticket, operation)
        return self._enqueue(8, ticket, operation, [])

    def cancel(self, ticket):
        self._ids(ticket)
        return self._enqueue(10, ticket, 0, [])

    def finish(self, ticket):
        self._ids(ticket)
        return self._enqueue(11, ticket, 0, [])

    @staticmethod
    def _ids(*ids):
        if any(type(i) is not int or not 0 < i < 2**64 for i in ids):
            raise ValueError("IDs must be positive uint64")

    def _run(self):
        context = zmq.Context()
        socket = context.socket(zmq.DEALER)
        pending = {}
        admitted = set()
        deferred = []
        try:
            descriptor = _descriptor(self._endpoint or _discover())
            public, secret = _identity(self._identity)
            socket.setsockopt(zmq.IDENTITY, public)
            socket.curve_publickey = public
            socket.curve_secretkey = secret
            socket.curve_serverkey = descriptor["server_key"].encode("ascii")
            socket.linger = 100
            socket.sndtimeo = 1000
            socket.maxmsgsize = WIRE_LIMIT
            socket.connect(descriptor["endpoint"])
            socket.send(_encode([100, PROTOCOL_VERSION, DOCUMENT_MODEL_VERSION,
                                 self._name]))
            start = heartbeat = time.monotonic()
            while not self._stop.is_set():
                now = time.monotonic()
                if now - heartbeat >= 1:
                    socket.send(_encode([103]))
                    heartbeat = now
                if not self._ready.done() and now - start >= self._timeout:
                    raise TimeoutError("ATHENA authorization timed out")
                if self._ready.done():
                    batch, deferred = deferred, []
                    while True:
                        try:
                            batch.append(self._queue.get_nowait())
                        except queue.Empty:
                            break
                    for frame, raw, future in batch:
                        op, ticket = frame[:2]
                        oid = frame[2] if op in (5, 8, 9) or (op == 3 and len(frame) == 3) else 0
                        terminal = (ticket, oid, False)
                        key = (ticket, oid, op in (8, 10, 11))
                        if op == 3 and terminal in pending:
                            pending[terminal].append(future)
                            continue
                        if (key[2] and key in pending) or (op == 10 and terminal in pending and (ticket, 0) not in admitted):
                            deferred.append((frame, raw, future))
                            continue
                        if op in (8, 11) and terminal in pending:
                            future.set_exception(ValueError("Wait for terminal result before REL/FIN; use CNL for resolution"))
                            continue
                        pending.setdefault(key, []).append(future)
                        socket.send(raw)
                        if op == 10:
                            for original in pending.pop(terminal, []):
                                original.set_exception(RuntimeError("Resolution cancelled"))
                if not socket.poll(50):
                    continue
                frame = _decode(socket.recv())
                op = frame[0]
                if op == 105:
                    raise ProtocolError(frame)
                if op == 102:
                    continue
                if op == 101:
                    if (len(frame) != 2 or not isinstance(frame[1], dict) or
                            frame[1].get("protocol_version") != PROTOCOL_VERSION or
                            frame[1].get("document_model_version") != DOCUMENT_MODEL_VERSION):
                        raise ProtocolError(frame)
                    if not self._ready.done():
                        self._ready.set_result(None)
                    continue
                ticket = frame[1]
                if op == 7 and len(frame) == 3:
                    for key in list(pending):
                        if key[0] == ticket:
                            for future in pending.pop(key):
                                future.set_exception(ProtocolError(frame))
                    continue
                oid = frame[2] if op in (6, 7) or (op == 2 and len(frame) == 3) else 0
                if op == 2:
                    admitted.add((ticket, oid))
                key = (ticket, oid, op == 2)
                if op == 7 and key not in pending:
                    key = (ticket, oid, True)
                for future in pending.pop(key, []):
                    if op == 7:
                        future.set_exception(ProtocolError(frame))
                    else:
                        future.set_result(Response(frame[3] if op == 6 else "OK",
                                                   frame[4] if op == 6 else frame[2] if op == 4 else None))
            raise RuntimeError("AUDMAP client closed")
        except BaseException as error:
            with self._lock:
                self._failure = error
                self._stop.set()
            if not self._ready.done():
                self._ready.set_exception(error)
            for promises in pending.values():
                for future in promises:
                    if not future.done():
                        future.set_exception(error)
            # Include the active send batch if transport failed midway through it.
            for _, _, future in deferred + locals().get("batch", []):
                if not future.done():
                    future.set_exception(error)
            while True:
                try:
                    _, _, future = self._queue.get_nowait()
                    if not future.done():
                        future.set_exception(error)
                except queue.Empty:
                    break
        finally:
            try:
                socket.send(_encode([104]), flags=zmq.DONTWAIT)
            except zmq.ZMQError:
                pass
            socket.close()
            context.term()
