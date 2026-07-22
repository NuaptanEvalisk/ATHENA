#!/usr/bin/env python3
import json
import os
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
    elif method == "model/list":
        send({"jsonrpc": "2.0", "id": request_id, "result": {
            "data": [{
                "id": "gpt-test", "model": "gpt-test",
                "displayName": "GPT Test", "description": "Test model",
                "hidden": False, "isDefault": True,
                "defaultReasoningEffort": "medium",
                "supportedReasoningEfforts": [
                    {"reasoningEffort": "low", "description": "Low"},
                    {"reasoningEffort": "medium", "description": "Medium"}],
                "serviceTiers": [{"id": "priority", "name": "Fast",
                                  "description": "Fast tier"}]
            }], "nextCursor": None}})
    elif method == "thread/start":
        params = message.get("params", {})
        if os.environ.get("ATHENA_FAKE_EXPECT_CUSTOM") == "1":
            assert params.get("model") == "gpt-test"
            assert params.get("serviceTier") == "priority"
            assert params.get("config", {}).get("web_search") == "live"
        else:
            assert "model" not in params
            assert "serviceTier" not in params
            assert "config" not in params
        send({"jsonrpc": "2.0", "id": request_id,
              "result": {"thread": {"id": "thread-1"}}})
    elif method == "turn/start":
        params = message.get("params", {})
        if os.environ.get("ATHENA_FAKE_EXPECT_CUSTOM") == "1":
            assert params.get("effort") == "low"
        expected_image = os.environ.get("ATHENA_FAKE_EXPECT_IMAGE")
        if expected_image:
            image_name = os.path.basename(expected_image)
            assert params.get("input") == [
                {"type": "text", "text": "Continue $x$"},
                {"type": "text",
                 "text": f"Attached asset <{image_name}>:"},
                {"type": "localImage", "path": expected_image},
            ]
        else:
            assert params.get("input") == [
                {"type": "text", "text": "Continue $x$"}]
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
