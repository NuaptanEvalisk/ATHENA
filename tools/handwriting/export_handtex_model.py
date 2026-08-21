#!/usr/bin/env python3
"""Convert the pinned Hand TeX classifier to ATHENA's ncnn runtime assets."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path

import pnnx
import torch
import torch.nn.functional as F
from safetensors.torch import load_file
from torch import nn


WEIGHTS_SHA256 = "f3fcb8004e67826590e2b7860ccf835efa56031bc4b3b236d6aeecfd7e6a2eef"
ENCODINGS_SHA256 = "47adad1c942f3245b934d229cbb533d262bb4c3da4a81b57db15c68f072e9bd1"


class HandTexCnn(nn.Module):
    def __init__(self, classes: int) -> None:
        super().__init__()
        channels = (31, 52, 92, 122, 178)
        self.conv1 = nn.Conv2d(1, channels[0], 3, padding=1)
        self.bn1 = nn.BatchNorm2d(channels[0])
        self.conv2 = nn.Conv2d(channels[0], channels[1], 3, padding=1)
        self.bn2 = nn.BatchNorm2d(channels[1])
        self.conv3 = nn.Conv2d(channels[1], channels[2], 3, padding=1)
        self.bn3 = nn.BatchNorm2d(channels[2])
        self.conv4 = nn.Conv2d(channels[2], channels[3], 3, padding=1)
        self.bn4 = nn.BatchNorm2d(channels[3])
        self.conv5 = nn.Conv2d(channels[3], channels[4], 3, padding=1)
        self.bn5 = nn.BatchNorm2d(channels[4])
        self.fc1 = nn.Linear(channels[4], classes)

    def forward(self, value: torch.Tensor) -> torch.Tensor:
        for conv, batch_norm in (
            (self.conv1, self.bn1),
            (self.conv2, self.bn2),
            (self.conv3, self.bn3),
            (self.conv4, self.bn4),
            (self.conv5, self.bn5),
        ):
            value = F.max_pool2d(F.relu(batch_norm(conv(value))), 2)
        value = F.adaptive_avg_pool2d(value, (1, 1))
        return self.fc1(value.reshape(-1, 178))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require_hash(path: Path, expected: str) -> None:
    actual = sha256(path)
    if actual != expected:
        raise SystemExit(f"unexpected SHA-256 for {path}: {actual}")


def symbol_key(entry: dict[str, object]) -> str:
    key = entry.get("key")
    if isinstance(key, str):
        return key
    package = entry.get("package", "latex2e")
    command = str(entry["command"])
    return f"{package}-{command.replace(chr(92), '_')}"


def write_metadata(source: Path, output: Path) -> None:
    metadata = source / "handtex/data/symbol_metadata"
    symbols = json.loads((metadata / "symbols.json").read_text(encoding="utf-8"))
    with (output / "symbols.tsv").open("w", encoding="utf-8", newline="\n") as target:
        for entry in symbols:
            target.write(f"{symbol_key(entry)}\t{entry['command']}\n")

    groups: list[str] = []
    for path in sorted(metadata.glob("similar_*.txt")):
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.strip():
                groups.append("\t".join(line.split()))
    (output / "similarity-groups.tsv").write_text(
        "\n".join(groups) + "\n", encoding="utf-8", newline="\n"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--weights", type=Path, required=True)
    parser.add_argument("--encodings", type=Path, required=True)
    parser.add_argument("--handtex-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    require_hash(args.weights, WEIGHTS_SHA256)
    require_hash(args.encodings, ENCODINGS_SHA256)
    labels = args.encodings.read_text(encoding="utf-8").splitlines()
    if len(labels) != 1775:
        raise SystemExit(f"expected 1775 Hand TeX classes, got {len(labels)}")

    args.output.mkdir(parents=True, exist_ok=True)
    model = HandTexCnn(len(labels))
    model.load_state_dict(load_file(args.weights))
    model.eval()

    temporary = args.output / "handtex.pt"
    pnnx.export(
        model,
        str(temporary),
        (torch.zeros(1, 1, 64, 64),),
        ncnnparam=str(args.output / "handtex.ncnn.param"),
        ncnnbin=str(args.output / "handtex.ncnn.bin"),
        fp16=False,
    )
    temporary.unlink(missing_ok=True)
    for suffix in (".pnnx.param", ".pnnx.bin", ".pnnx.py", ".pnnx.onnx", ".ncnn.py"):
        temporary.with_suffix(suffix).unlink(missing_ok=True)
    (args.output / "handtex_pnnx.py").unlink(missing_ok=True)
    (args.output / "handtex_ncnn.py").unlink(missing_ok=True)
    shutil.rmtree(args.output / "__pycache__", ignore_errors=True)

    shutil.copyfile(args.encodings, args.output / "encodings.txt")
    write_metadata(args.handtex_source, args.output)
    print(f"wrote Hand TeX runtime assets to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
