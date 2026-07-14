#!/usr/bin/env python3
import json
import sys


def send(message):
    print(json.dumps(message, separators=(",", ":")), flush=True)


for line in sys.stdin:
    message = json.loads(line)
    method = message.get("method")
    request_id = message.get("id")
    if method == "initialize":
        send({"jsonrpc": "2.0", "id": request_id, "result": {
            "codexHome": "/tmp/fake-codex", "platformFamily": "unix",
            "platformOs": "linux", "userAgent": "fake-codex"}})
    elif method == "thread/start":
        send({"jsonrpc": "2.0", "id": request_id,
              "result": {"thread": {"id": "thread-1"}}})
    elif method == "turn/start":
        send({"jsonrpc": "2.0", "id": request_id,
              "result": {"turn": {"id": "turn-1"}}})
        send({"jsonrpc": "2.0", "method": "item/agentMessage/delta",
              "params": {"threadId": "thread-1", "turnId": "turn-1",
                         "itemId": "item-1", "delta": "continued "}})
        send({"jsonrpc": "2.0", "method": "item/agentMessage/delta",
              "params": {"threadId": "thread-1", "turnId": "turn-1",
                         "itemId": "item-1", "delta": "formula"}})
        send({"jsonrpc": "2.0", "method": "turn/completed",
              "params": {"threadId": "thread-1",
                         "turn": {"id": "turn-1", "status": "completed",
                                  "items": []}}})
