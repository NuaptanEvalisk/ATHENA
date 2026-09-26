# Hello World Plugin

An installable Python AUDMAP v2 example of an ATHENA subprocess plugin. The manifest
registers **Insert Hello World**. Each invocation adds a new paragraph containing
`Hello, World!` at the end of the active document. It does not save the
document. No vault is required.

## Install

`package.py` uses `uv` to build a self-contained package with a pinned standalone
CPython runtime plus binary-wheel installs of `pyzmq` and `msgpack`. The ATHENA
AUDMAP v2 SDK is bundled under `sdk/`. Runtime execution uses the packaged Python
in isolated mode (`-I`), so no system or user Python package is required or read.
No dependency is downloaded or installed when the plugin starts.

1. Install the ZIP through **Plugins -> Manage plugins**.
2. Review the bundled GPL license and the requested ATHENA permissions. This
   example requests no filesystem or network access. Its required AUDMAP grants
   are `buffer:resolve`, `document:resolve`, and `node:resolve/get/insert`.
3. Accept the license and required permissions, choose the desired confirmation
   mode, then start the plugin.
4. Activate a document and choose **Insert Hello World** in the plugin's menu.

If a required permission is later revoked, the plugin cannot start until it is
granted again. Denials, read-only documents, or the absence of an active buffer
are reported as command errors in the plugin manager. A connection failure stops
the plugin rather than risking a duplicate insertion by automatically retrying it.

## Build the ZIP

From the repository root:

```sh
python3 ATHENA/examples/hello-world-plugin/package.py build_qt6/examples/hello-world-plugin.zip
```

The packager replaces that destination if it already exists, so rebuilding does
not retain an older plugin ZIP beside the current package.

The source directory is a build source, not an installable runtime package. Use
the generated ZIP; it contains the standalone Python runtime and all Python
dependencies.

## API Flow

The plugin receives manifest commands through its private
`@/subscription/GUID` mailbox. For each command it resolves
`@/buffers/@/document/body/[0]`, reads its tree with `get`, and inserts after its
existing children. For a body with three paragraphs, the `insert` parameters are:

```json
{"index": 3, "children": [{"text": "Hello, World!"}]}
```

The operation is `insert`. In a body `document`, each child is a paragraph.
Resolution pins the target buffer; changing focus during confirmation does not
redirect the edit. ATHENA performs the mutation on the buffer's owning actor.
The plugin releases operation results and finishes tickets after use.

The current API uses an explicit child index: this example samples the end
position with `get`, then inserts there. Avoid editing the body while a command
is awaiting confirmation; these two operations are not an atomic append.
