#!/usr/bin/env python3.11
"""Build DEB and openSUSE/RHEL RPM packages from an ATHENA AppDir."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def run(args: list[str], **kwargs) -> None:
    subprocess.run(args, check=True, text=True, **kwargs)


def copy_payload(appdir: Path, root: Path) -> None:
    install_root = root / "opt/ATHENA"
    shutil.copytree(appdir, install_root, symlinks=True)
    for directory in list(install_root.rglob("*")):
        if directory.is_dir() and directory.name in {
            ".venv", ".uv-cache", "__pycache__"
        }:
            shutil.rmtree(directory)

    bindir = root / "usr/bin"
    bindir.mkdir(parents=True, exist_ok=True)
    launcher = bindir / "ATHENA"
    launcher.write_text('#!/usr/bin/env sh\nexec /opt/ATHENA/AppRun "$@"\n')
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
    out = outdir / f"ATHENA-{version}-{flavor}-linux-x86_64.deb"
    run(["dpkg-deb", "--root-owner-group", "--build", str(work), str(out)])
    return out


def build_rpm(payload: Path, outdir: Path, flavor: str, version: str,
              distro: str) -> Path:
    name = package_name(flavor)
    topdir = payload.parent / f"rpmbuild-{distro}"
    sourcedir = topdir / "SOURCES"
    specs = topdir / "SPECS"
    sourcedir.mkdir(parents=True)
    specs.mkdir(parents=True)
    shutil.copytree(payload, sourcedir / "payload", symlinks=True)
    spec = specs / f"{name}.spec"
    spec.write_text(f"""Name:           {name}
Version:        {version}
Release:        1.{distro}.{flavor}
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
        # The bundled model is already compressed. zstd avoids spending many
        # minutes recompressing it with the much slower xz payload compressor.
        "--define", "_binary_payload w7.zstdio",
    ])
    built = list((topdir / "RPMS").rglob("*.rpm"))
    if len(built) != 1:
        raise RuntimeError(f"expected one RPM, found {built}")
    out = outdir / f"ATHENA-{version}-{flavor}-{distro}-x86_64.rpm"
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
    outdir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="athena-native-package-") as tmp:
        payload = Path(tmp) / "payload"
        copy_payload(appdir, payload)
        outputs = [
            build_deb(payload, outdir, flavor, version),
            build_rpm(payload, outdir, flavor, version, "opensuse"),
            build_rpm(payload, outdir, flavor, version, "rhel"),
        ]
    for output in outputs:
        run(["sha256sum", str(output)], stdout=(output.with_suffix(
            output.suffix + ".sha256")).open("w"))
        print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
