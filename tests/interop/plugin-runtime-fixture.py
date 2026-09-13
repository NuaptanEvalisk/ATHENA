#!/usr/bin/env python3
"""Isolated subprocess fixture using the independent AUDMAP Python SDK."""
import os
import signal
import subprocess
import sys
import time

from athena_audmap import Client

stopping = False


def stop(signum, frame):
    global stopping
    stopping = True


signal.signal(signal.SIGTERM, stop)
guid = os.environ["ATHENA_SUBSCRIPTION_GUID"]
with Client(endpoint=os.environ["ATHENA_AUDMAP_ENDPOINT"],
            identity=os.environ["ATHENA_AUDMAP_IDENTITY"],
            name="Untrusted self-declared plugin name") as client:
    selected = client.resolve("@/subscription/" + guid, leaves=True)
    handle = selected.result(timeout=5).data[0][0]
    print("CONNECTED " + guid, flush=True)
    while not stopping:
        op = client.operate(selected.ticket, handle, "get")
        response = op.result(timeout=5)
        client.release(op.ticket, op.operation).result(timeout=5)
        if response.status != "OK":
            break
        for command in response.data["commands"]:
            data = {"pid": os.getpid(), "guid": guid, "parameters": command["parameters"]}
            if command["command"] == "probe":
                root = client.resolve("@", leaves=True)
                rh = root.result(timeout=5).data[0][0]
                try:
                    reply = client.operate(root.ticket, rh, "write").result(timeout=5)
                    data["allowed"] = reply.status == "OK"
                except Exception:
                    data["allowed"] = False
                client.finish(root.ticket).result(timeout=5)
            elif command["command"] == "child":
                child = subprocess.Popen([sys.executable, "-c",
                    "import signal,time; signal.signal(signal.SIGTERM, signal.SIG_IGN); time.sleep(60)"])
                data["child"] = child.pid
            elif command["command"] == "ignore-stop":
                signal.signal(signal.SIGTERM, signal.SIG_IGN)
            reply = client.operate(selected.ticket, handle, "reply",
                {"id": command["id"], "status": "OK", "result": data})
            assert reply.result(timeout=5).status == "OK"
            client.release(reply.ticket, reply.operation).result(timeout=5)
            if command["command"] == "ignore-stop":
                print("IGNORING TERM", flush=True)
                while True:
                    time.sleep(1)
        time.sleep(0.05)
