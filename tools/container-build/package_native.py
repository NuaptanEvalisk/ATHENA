#!/usr/bin/env python3
"""Build DEB and RPM packages from an ATHENA AppDir."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(
    0, str(Path(__file__).resolve().parents[1] / "release")
)
from runtime_policy import verify_linux_services, verify_runtime


REPO_ROOT = Path(__file__).resolve().parents[2]
NATIVE_SCHEME_CACHE = "/var/cache/athena/scheme"


def run(args: list[str], **kwargs) -> None:
    subprocess.run(args, check=True, text=True, **kwargs)


def copy_payload(appdir: Path, root: Path) -> None:
    install_root = root / "opt/ATHENA"
    shutil.copytree(appdir, install_root, symlinks=True)
    verify_runtime(install_root)
    verify_linux_services(install_root / "usr/share/ATHENA")

    tools = install_root / "usr/share/tools"
    tools.mkdir(parents=True, exist_ok=True)
    for name in (
        "compile-athena-scheme-bytecode.sh",
        "plan-athena-scheme-bytecode.scm",
    ):
        shutil.copy2(REPO_ROOT / "tools" / name, tools / name)
    compiler = tools / "compile-installed-scheme-bytecode.sh"
    shutil.copy2(
        REPO_ROOT / "tools/release/compile-installed-scheme-bytecode.sh",
        compiler,
    )
    compiler.chmod(0o755)

    # AppImages carry relocatable bytecode. Native packages deliberately build
    # it against their final source tree during installation and keep the
    # result in /var/cache instead of modifying package-owned files below /opt.
    shutil.rmtree(install_root / "usr/share/ATHENA/lib/athena-scheme")

    bindir = root / "usr/bin"
    bindir.mkdir(parents=True, exist_ok=True)
    launcher = bindir / "ATHENA"
    launcher.write_text(
        "#!/usr/bin/env sh\n"
        f"export ATHENA_GUILE_CACHE_PATH={NATIVE_SCHEME_CACHE}/"
        "athena-guile-3.0.10-native\n"
        "export GUILE_LOAD_COMPILED_PATH=$ATHENA_GUILE_CACHE_PATH\n"
        "export GUILE_AUTO_COMPILE=0\n"
        'exec /opt/ATHENA/AppRun "$@"\n'
    )
    launcher.chmod(0o755)

    applications = root / "usr/share/applications"
    applications.mkdir(parents=True, exist_ok=True)
    shutil.copy2(appdir / "ATHENA.desktop", applications / "ATHENA.desktop")

    icon = appdir / "ATHENA.png"
    if icon.exists():
        icons = root / "usr/share/icons/hicolor/512x512/apps"
        icons.mkdir(parents=True, exist_ok=True)
        shutil.copy2(icon, icons / "ATHENA.png")


def package_name(flavor: str) -> str:
    return "athena" if flavor == "rel" else f"athena-{flavor}"


def build_deb(payload: Path, outdir: Path, flavor: str, version: str) -> Path:
    name = package_name(flavor)
    work = payload.parent / "deb-root"
    shutil.copytree(payload, work, symlinks=True)
    debian = work / "DEBIAN"
    debian.mkdir()
    installed_size = sum(
        p.stat().st_size for p in work.rglob("*") if p.is_file()
    ) // 1024
    control = f"""Package: {name}
Version: {version}-1
Section: editors
Priority: optional
Architecture: amd64
Installed-Size: {installed_size}
Maintainer: ATHENA Project <nuaptan@outlook.com>
Depends: libc6 (>= 2.31), libegl1, libgl1
Homepage: https://athena.evalisk.org/
Description: Mathematical knowledge organization and writing environment
 ATHENA is a structured WYSIWYG environment for mathematical notes, Vaults,
 namespaces, transclusion, graphs, publishing, and retrieval workflows.
"""
    (debian / "control").write_text(control)
    postinst = debian / "postinst"
    postinst.write_text(
        "#!/bin/sh\n"
        "set -e\n"
        "if [ \"${1:-}\" = configure ]; then\n"
        "  ATHENA_SCHEME_SYSTEM_CACHE=/var/cache/athena/scheme \\\n"
        "  ATHENA_SCHEME_COMPILE_HOME=/var/cache/athena/scheme-compile-home \\\n"
        "    /opt/ATHENA/usr/share/tools/compile-installed-scheme-bytecode.sh\n"
        "fi\n"
    )
    postinst.chmod(0o755)
    postrm = debian / "postrm"
    postrm.write_text(
        "#!/bin/sh\n"
        "set -e\n"
        "if [ \"${1:-}\" = purge ]; then\n"
        "  rm -rf /var/cache/athena\n"
        "fi\n"
    )
    postrm.chmod(0o755)
    out = outdir / f"ATHENA-{version}-{flavor}-linux-x86_64.deb"
    run(["dpkg-deb", "--root-owner-group", "--build", str(work), str(out)])
    return out


def build_rpm(payload: Path, outdir: Path, flavor: str, version: str) -> Path:
    name = package_name(flavor)
    topdir = payload.parent / "rpmbuild"
    sourcedir = topdir / "SOURCES"
    specs = topdir / "SPECS"
    sourcedir.mkdir(parents=True)
    specs.mkdir(parents=True)
    shutil.copytree(payload, sourcedir / "payload", symlinks=True)
    spec = specs / f"{name}.spec"
    spec.write_text(f"""Name:           {name}
Version:        {version}
Release:        1
Summary:        Mathematical knowledge organization and writing environment
License:        GPL-3.0-or-later
URL:            https://athena.evalisk.org/
BuildArch:      x86_64
AutoReqProv:    no

%description
ATHENA is a structured WYSIWYG environment for mathematical notes, Vaults,
namespaces, transclusion, graphs, publishing, and retrieval workflows.

%prep

%build

%install
mkdir -p %{{buildroot}}
cp -a %{{_sourcedir}}/payload/. %{{buildroot}}/

%post
ATHENA_SCHEME_SYSTEM_CACHE=/var/cache/athena/scheme \\
ATHENA_SCHEME_COMPILE_HOME=/var/cache/athena/scheme-compile-home \\
  /opt/ATHENA/usr/share/tools/compile-installed-scheme-bytecode.sh

%postun
if [ "$1" -eq 0 ]; then
  rm -rf /var/cache/athena
fi

%files
/opt/ATHENA
/usr/bin/ATHENA
/usr/share/applications/ATHENA.desktop
/usr/share/icons/hicolor/512x512/apps/ATHENA.png

%changelog
* Sun Jul 12 2026 ATHENA Project <nuaptan@outlook.com> - {version}-1
- Container-built ATHENA {flavor} package.
""")
    run([
        "rpmbuild", "-bb", str(spec),
        "--define", f"_topdir {topdir}",
        "--define", f"_sourcedir {sourcedir}",
        "--define", f"_rpmdir {topdir / 'RPMS'}",
        "--define", "_binary_payload w7.zstdio",
    ])
    built = list((topdir / "RPMS").rglob("*.rpm"))
    if len(built) != 1:
        raise RuntimeError(f"expected one RPM, found {built}")
    out = outdir / f"ATHENA-{version}-{flavor}-linux-x86_64.rpm"
    shutil.copy2(built[0], out)
    return out


def main() -> int:
    if len(sys.argv) != 5:
        print("usage: package_native.py APPDIR OUTDIR FLAVOR VERSION",
              file=sys.stderr)
        return 2
    appdir = Path(sys.argv[1]).resolve()
    outdir = Path(sys.argv[2]).resolve()
    flavor = sys.argv[3]
    version = sys.argv[4]
    if flavor not in {"dev", "rel"}:
        raise SystemExit("FLAVOR must be dev or rel")
    if not (appdir / "AppRun").is_file():
        raise SystemExit(f"invalid AppDir: {appdir}")
    verify_runtime(appdir)
    verify_linux_services(appdir / "usr/share/ATHENA")
    outdir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="athena-native-package-") as tmp:
        payload = Path(tmp) / "payload"
        copy_payload(appdir, payload)
        outputs = [
            build_deb(payload, outdir, flavor, version),
            build_rpm(payload, outdir, flavor, version),
        ]
    for output in outputs:
        run(["sha256sum", str(output)], stdout=(output.with_suffix(
            output.suffix + ".sha256")).open("w"))
        print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
