#!/usr/bin/env python3

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from runtime_policy import (
    copy_runtime,
    verify_linux_services,
    verify_runtime,
)


class RuntimePolicyTest(unittest.TestCase):
    def add_scheme_bytecode(self, runtime: Path) -> None:
        generation = (
            runtime / "lib/athena-scheme/athena-guile-3.0.10-native"
        )
        generation.mkdir(parents=True, exist_ok=True)
        (generation / "init-athena.go").write_text("bytecode")
        (generation / ".complete").write_text(
            "athena-guile-3.0.10-native\n1\n"
        )

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
            for relative in (
                "lib/athena-guile/bin/guile",
                "lib/athena-guile/lib/libathena-guile.so.1",
                "lib/athena-guile/share/guile/3.0/ice-9/boot-9.scm",
                "lib/athena-guile/lib/guile/3.0/ccache/ice-9/boot-9.go",
            ):
                dependency = runtime / relative
                dependency.parent.mkdir(parents=True, exist_ok=True)
                dependency.write_text("runtime")
            self.add_scheme_bytecode(runtime)
            verify_runtime(runtime)

    def test_verify_rejects_missing_application_bytecode(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            runtime = Path(temporary)
            binary = runtime / "bin/ATHENA.bin"
            binary.parent.mkdir()
            binary.write_text("executable")
            for relative in (
                "lib/athena-guile/bin/guile",
                "lib/athena-guile/lib/libathena-guile.so.1",
                "lib/athena-guile/share/guile/3.0/ice-9/boot-9.scm",
                "lib/athena-guile/lib/guile/3.0/ccache/ice-9/boot-9.go",
            ):
                dependency = runtime / relative
                dependency.parent.mkdir(parents=True, exist_ok=True)
                dependency.write_text("runtime")
            with self.assertRaisesRegex(RuntimeError, "bytecode generation"):
                verify_runtime(runtime)

    def test_verify_allows_the_packaged_handwriting_model(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            runtime = Path(temporary)
            model = runtime / "misc/models/handwriting/handtex.ncnn.bin"
            model.parent.mkdir(parents=True)
            model.write_text("model")
            verify_runtime(runtime)

    def test_verify_allows_the_packaged_handwriting_model_in_appdir(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            appdir = Path(temporary)
            model = (
                appdir /
                "usr/share/ATHENA/misc/models/handwriting/handtex.ncnn.bin"
            )
            model.parent.mkdir(parents=True)
            model.write_text("model")
            verify_runtime(appdir)

    def test_copy_can_retain_an_executable_runtime(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            destination = root / "destination"
            (source / "bin").mkdir(parents=True)
            (source / "lib").mkdir()
            (source / "bin/ATHENA.bin").write_text("executable")
            (source / "lib/libathena.so").write_text("library")

            copy_runtime(
                source,
                destination,
                keep_source_libraries=True,
                keep_athena_binary=True,
            )

            self.assertTrue((destination / "bin/ATHENA.bin").is_file())
            self.assertTrue((destination / "lib/libathena.so").is_file())

    def test_source_service_binaries_are_never_reused(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            destination = root / "destination"
            (source / "bin").mkdir(parents=True)
            (source / "bin/athena-materials-engine").write_text("stale")
            (source / "bin/athena-transmitter").write_text("stale")
            (source / "bin/athena-web-server").write_text("stale")

            copy_runtime(source, destination)

            self.assertFalse(
                (destination / "bin/athena-materials-engine").exists()
            )
            self.assertFalse(
                (destination / "bin/athena-transmitter").exists()
            )
            self.assertFalse(
                (destination / "bin/athena-web-server").exists()
            )

    def test_linux_service_validation_requires_complete_layout(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            runtime = Path(temporary)
            (runtime / "bin").mkdir()
            (runtime / "share/ATHENA/web").mkdir(parents=True)
            for name in (
                "athena-materials-engine",
                "athena-transmitter",
                "athena-web-server",
            ):
                executable = runtime / "bin" / name
                executable.write_text("executable")
                executable.chmod(0o755)
            (runtime / "share/ATHENA/web/index.html").write_text("index")
            (runtime / "share/ATHENA/web/app.js").write_text("app")

            verify_linux_services(runtime)
            (runtime / "share/ATHENA/web/app.js").unlink()
            with self.assertRaisesRegex(RuntimeError, "app.js"):
                verify_linux_services(runtime)


if __name__ == "__main__":
    unittest.main()
