# Standalone AUDMAP clients

The SDKs connect to a running ATHENA instance over authenticated local IPC.
Neither SDK links or loads the editor, Guile, Qt, the rendering engine, or the
native resource resolvers. They share the v1 protocol and CURVE identity format.
The server still applies its connection and operation authorization policies.

## C++

Build only the SDK from this repository:

```sh
cmake -S clients/cpp -B build-client -DCMAKE_BUILD_TYPE=Release
cmake --build build-client -j
cmake --install build-client --prefix "$HOME/.local"
```

Dependencies: C++17, nlohmann JSON (MIT), cppzmq (MIT), libzmq (MPL-2.0),
msgpack-cxx (BSL-1.0), and platform threads. The SDK is GPL-3.0-or-later.
Its standalone CMake project does not configure ATHENA's build or its dependencies.

A consumer uses `find_package(ATHENAAudmap CONFIG REQUIRED)` and links
`ATHENA::audmap`. Only `<athena/audmap/client.hpp>` is public.

```cpp
#include <athena/audmap/client.hpp>
athena::audmap::client client;
auto selection = client.resolve("@/vaults/@", true);
auto resolved = selection.result.get(); // data: [handles, truncation reasons]
for (const auto& h : resolved.data.at(0)) {
  auto operation = client.operate(selection.ticket, h.get<uint64_t>(), "get");
  auto response = operation.result.get(); // status and semantic data
  client.release(operation.ticket, operation.operation).result.get();
}
client.finish(selection.ticket).result.get();
```

Set `options.endpoint` when several ATHENA instances are running.
Set `options.identity` for an application's own persistent key and `options.name`
for its reported client name. The default identity is
`$XDG_CONFIG_HOME/athena-audmap/cpp.json` (fallback `~/.config`).

## Python

```sh
python3 -m pip install ./clients/python
```

The Python package requires Python 3.10+, PyZMQ (BSD-3-Clause) and
msgpack (Apache-2.0). It does not invoke or embed the C++ client.

```python
from athena_audmap import Client

with Client(name="My tool") as client:
    selection = client.resolve("@/vaults/@", leaves=True)
    handles, truncated = selection.result(timeout=30).data
    for handle in handles:
        operation = client.operate(selection.ticket, handle, "get")
        response = operation.result(timeout=30)
        print(response.status, response.data)
        client.release(operation.ticket, operation.operation).result()
    client.finish(selection.ticket).result()
```

`Client(endpoint=..., identity=...)` overrides automatic discovery and the
default `$XDG_CONFIG_HOME/athena-audmap/python.json` identity. Python `bytes`
maps to MessagePack BIN, not a lossy UTF-8 string.

## Lifecycle and concurrency

Each client has one socket-owner thread. Authentication and one-second
heartbeats keep running while application threads process results or wait.
Use a separate identity for each simultaneously active client connection.

Both SDKs expose asynchronous `resolve`, `operate`, `lineage`, `ask`,
`release`, `cancel` and `finish`. Requests expose their ticket and operation
IDs and a future. FULL resolution returns `[[handle,parent],...]`; LEAVES
returns handles, with optional positive `limit`. Truncation reasons accompany
either projection.

Protocol ERR fails the future with `protocol_error` / `ProtocolError`,
including the original frame. An RSP with non-OK status is returned as a
`response` / `Response`; callers must check status. Do not assume a command
succeeded just because the protocol delivered its result.

A future wait timeout does not cancel or repeat an operation. Use `ask` with
the same IDs to retrieve a retained result; asking while locally pending
shares its eventual result. `release` drops a terminal operation result.
`cancel` cancels resolution, not admitted operations. `finish` retires a
resolved ticket and drains accepted operations; use `cancel` if still resolving.
Wait for a terminal operation before releasing it. Operations with different
IDs can run concurrently and are not implicitly ordered.

Explicitly close C++ clients, or let their destructor join the I/O owner.
Use Python's context manager or `close()`. Closing settles outstanding futures
with errors and sends BYE best-effort; admitted server operations are not undone.
The socket has no automatic operation retry. Applications should use future
timeouts to handle an unresponsive server.

Keys and discovery files are owner-only files, and private keys persist across
restarts. A client name is a label, not an authenticated executable identity.
Never share or publish a private identity file.

Focused integration tests exercise the C++ client against the native server,
and `clients/python/tests/test_client.py SERVER_TEST_BINARY CPP_EXAMPLE_BINARY`
covers the Python package and a shared identity consumed by the standalone C++
build, including a wait longer than the server's idle expiry.
