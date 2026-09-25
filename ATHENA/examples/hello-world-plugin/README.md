# Hello World Plugin

An installable Python AUDMAP v2 example of an ATHENA subprocess plugin. The manifest
registers **Insert Hello World**. Each invocation adds a new paragraph containing
`Hello, World!` at the end of the active document. It does not save the
document. No vault is required.

## Install

The ZIP includes the repository's current Python AUDMAP v2 SDK unchanged. The executable
uses `/usr/bin/python3`, which must have `zmq` and `msgpack` available. It does
not use the shell's Conda interpreter. No dependencies are installed at startup.

1. Install the ZIP through **Plugins -> Manage plugins**.
2. Set **Full API access**, or use **Custom commands** with resource type `node`
   allowing `get` and `insert`. The launch subscription permissions are provided by ATHENA.
3. Choose the desired confirmation mode, apply the settings, and start the plugin.
4. Activate a document and choose **Insert Hello World** in the plugin's menu.

The default Read only policy cannot insert text. Denials, read-only documents,
or the absence of an active buffer are reported as command errors in the plugin
manager. A connection failure stops the plugin rather than risking a duplicate
insertion by automatically retrying it.

## Build the ZIP

From the repository root, using a new output path:

```sh
python3 ATHENA/examples/hello-world-plugin/package.py build_qt6/examples/hello-world-plugin.zip
```

Installing the source directory directly instead requires the SDK to be
installed for `/usr/bin/python3`; the ZIP avoids this requirement.

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
