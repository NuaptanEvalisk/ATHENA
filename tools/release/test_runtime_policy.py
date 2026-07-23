#!/usr/bin/env python3

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from runtime_policy import copy_runtime, verify_runtime


class RuntimePolicyTest(unittest.TestCase):
    def test_copy_keeps_runtime_code_but_excludes_release_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            destination = root / "destination"
            (source / "bin").mkdir(parents=True)
            (source / "lib").mkdir()
            (source / "tools/artifacts").mkdir(parents=True)
            (source / "tools/formula-cleaner/.venv").mkdir(parents=True)
            (source / "progs").mkdir()

            (source / "bin/ATHENA.bin").write_text("development binary")
            (source / "bin/helper").write_text("helper")
            (source / "lib/libdevelopment.so").write_text("development lib")
            (source / "tools/artifacts/model.gguf").write_text("model")
            (source / "tools/artifacts/model.onnx").write_text("model")
            (source / "tools/artifacts/pytorch_model.bin").write_text("model")
            (source / "tools/artifacts/README.md").write_text("instructions")
            (source / "tools/formula-cleaner/model.safetensors").write_text(
                "model"
            )
            (source / "tools/formula-cleaner/.venv/python").write_text("cache")
            (source / "progs/runtime.scm").write_text("(display \"runtime\")")

            copy_runtime(source, destination)

            self.assertTrue((destination / "bin/helper").is_file())
            self.assertTrue(
                (destination / "tools/artifacts/README.md").is_file()
            )
            self.assertTrue((destination / "progs/runtime.scm").is_file())
            self.assertFalse((destination / "bin/ATHENA.bin").exists())
            self.assertFalse((destination / "lib/libdevelopment.so").exists())
            self.assertFalse((destination / "tools/artifacts/model.gguf").exists())
            self.assertFalse((destination / "tools/artifacts/model.onnx").exists())
            self.assertFalse(
                (destination / "tools/artifacts/pytorch_model.bin").exists()
            )
            self.assertFalse(
                (destination / "tools/formula-cleaner/model.safetensors").exists()
            )
            self.assertFalse(
                (destination / "tools/formula-cleaner/.venv").exists()
            )
            verify_runtime(destination)

    def test_verify_rejects_new_model_weights_anywhere(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            runtime = Path(temporary)
            nested = runtime / "new-feature/models"
            nested.mkdir(parents=True)
            (nested / "future-model.gguf").write_text("model")

            with self.assertRaisesRegex(RuntimeError, "future-model.gguf"):
                verify_runtime(runtime)

    def test_verify_allows_the_installed_athena_executable(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            runtime = Path(temporary)
            binary = runtime / "bin/ATHENA.bin"
            binary.parent.mkdir()
            binary.write_text("executable")
            verify_runtime(runtime)


if __name__ == "__main__":
    unittest.main()
