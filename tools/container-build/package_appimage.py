#!/usr/bin/env python3.11
import os
import re
import shutil
import stat
import subprocess
import sys
from pathlib import Path

sys.path.insert(
    0, str(Path(__file__).resolve().parents[1] / "release")
)
from runtime_policy import verify_linux_services, verify_runtime


GLIBC_EXCLUDE = {
    "ld-linux-x86-64.so.2",
    "libBrokenLocale.so.1",
    "libanl.so.1",
    "libc.so.6",
    "libdl.so.2",
    "libm.so.6",
    "libmvec.so.1",
    "libnsl.so.1",
    "libnss_compat.so.2",
    "libnss_dns.so.2",
    "libnss_files.so.2",
    "libpthread.so.0",
    "libresolv.so.2",
    "librt.so.1",
    "libthread_db.so.1",
    "libutil.so.1",
}

SYSTEM_GRAPHICS_PREFIXES = (
    "libEGL.so",
    "libGL.so",
    "libGLESv2.so",
    "libGLX.so",
    "libGLdispatch.so",
    "libdrm.so",
    "libgbm.so",
    "libvulkan.so",
)


def run(args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)


def output(args, **kwargs):
    return subprocess.check_output(args, text=True, **kwargs)


def is_elf(path: Path) -> bool:
    try:
        with path.open("rb") as f:
            return f.read(4) == b"\x7fELF"
    except OSError:
        return False


def soname(path: Path) -> str | None:
    try:
        text = output(["readelf", "-d", str(path)], stderr=subprocess.DEVNULL)
    except subprocess.CalledProcessError:
        return None
    m = re.search(r"Library soname: \[(.+?)\]", text)
    return m.group(1) if m else None


def ldd_paths(path: Path) -> list[Path]:
    try:
        text = output(["ldd", str(path)], stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as e:
        text = e.output
    out: list[Path] = []
    for line in text.splitlines():
        line = line.strip()
        if "not found" in line or "linux-vdso" in line:
            continue
        m = re.search(r"=>\s+(/[^ ]+)", line)
        if not m:
            m = re.match(r"(/[^ ]+)", line)
        if m:
            out.append(Path(m.group(1)))
    return out


def copy_file(src: Path, dst: Path):
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists() or dst.is_symlink():
        dst.unlink()
    shutil.copy2(src, dst)


def is_inside(path: Path, root: Path) -> bool:
    try:
        path.resolve(strict=False).relative_to(root.resolve(strict=False))
        return True
    except ValueError:
        return False


def skip_library_name(base: str) -> bool:
    return (
        base in GLIBC_EXCLUDE
        or base.startswith("libnss_")
        or any(base.startswith(prefix) for prefix in SYSTEM_GRAPHICS_PREFIXES)
    )


def copy_library(src: Path, libdir: Path, queue: list[Path], copied: set[Path]):
    real = src.resolve()
    base = real.name
    if skip_library_name(base):
        return
    if real in copied:
        return
    copied.add(real)
    dst = libdir / base
    copy_file(real, dst)
    so = soname(dst)
    if so and so != base:
        link = libdir / so
        if link.exists() or link.is_symlink():
            link.unlink()
        link.symlink_to(base)
    queue.append(dst)


def copy_dependencies(appdir: Path):
    libdir = appdir / "usr/lib"
    libdir.mkdir(parents=True, exist_ok=True)
    queue = [p for p in appdir.rglob("*") if p.is_file() and is_elf(p)]
    copied: set[Path] = set()
    seen_queue: set[Path] = set(queue)
    while queue:
        elf = queue.pop(0)
        for dep in ldd_paths(elf):
            if is_inside(dep, appdir):
                continue
            before = len(copied)
            copy_library(dep, libdir, queue, copied)
            if len(copied) != before:
                for p in list(queue):
                    if p in seen_queue:
                        continue
                    seen_queue.add(p)


def copy_qt_plugins(appdir: Path):
    try:
        plugin_root = Path(output(["qtpaths6", "--plugin-dir"]).strip())
    except Exception:
        return
    wanted = [
        "generic",
        "iconengines",
        "imageformats",
        "networkinformation",
        "platforminputcontexts",
        "platforms",
        "platformthemes",
        "printsupport",
        "styles",
        "tls",
        "wayland-decoration-client",
        "wayland-graphics-integration-client",
        "wayland-shell-integration",
        "xcbglintegrations",
    ]
    dst_root = appdir / "usr/plugins"
    for name in wanted:
        src = plugin_root / name
        if src.exists():
            shutil.copytree(src, dst_root / name, dirs_exist_ok=True, symlinks=True)


def copy_optional_tree(src: Path, dst: Path):
    if src.exists():
        shutil.copytree(src, dst, dirs_exist_ok=True, symlinks=True)


def guile_version() -> str | None:
    try:
        raw = output(["guile", "-c", "(display (version))"]).strip()
    except Exception:
        return None
    parts = raw.split(".")
    if len(parts) >= 2:
        return ".".join(parts[:2])
    return raw


def guile_pkgconfig_prefix(ver: str) -> Path | None:
    try:
        raw = output(["pkg-config", "--variable=prefix", f"guile-{ver}"]).strip()
    except Exception:
        return None
    return Path(raw) if raw else None


def copy_guile(appdir: Path):
    ver = guile_version()
    if not ver:
        return
    prefix = guile_pkgconfig_prefix(ver) or Path("/usr")
    copy_optional_tree(prefix / "share/guile" / ver,
                       appdir / "usr/share/guile" / ver)
    copy_optional_tree(prefix / "share/guile/site",
                       appdir / "usr/share/guile/site")
    copy_optional_tree(prefix / "share/guile/site" / ver,
                       appdir / "usr/share/guile/site" / ver)
    copy_optional_tree(prefix / "share/guile",
                       appdir / "usr/share/guile")
    copy_optional_tree(prefix / "lib64/guile" / ver,
                       appdir / "usr/lib64/guile" / ver)
    copy_optional_tree(prefix / "lib/guile" / ver,
                       appdir / "usr/lib/guile" / ver)


def copy_imagemagick(appdir: Path):
    for base in [Path("/usr/lib64"), Path("/usr/share")]:
        if not base.exists():
            continue
        for src in base.glob("ImageMagick*"):
            copy_optional_tree(src, appdir / src.relative_to("/"))


def write_apprun(appdir: Path):
    gv = guile_version() or "2.0"
    apprun = appdir / "AppRun"
    apprun.write_text("""#!/usr/bin/env bash
set -euo pipefail

appdir="$(cd -- "$(dirname -- "${{BASH_SOURCE[0]}}")" && pwd)"
athena_dir="$appdir/usr/share/ATHENA"

export ATHENA_PATH="$athena_dir"
export LD_LIBRARY_PATH="$appdir/usr/lib:$athena_dir/lib${{LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}}"
export QT_PLUGIN_PATH="$appdir/usr/plugins${{QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}}"
export QT_QPA_PLATFORM_PLUGIN_PATH="$appdir/usr/plugins/platforms"
export GUILE_LOAD_PATH="$appdir/usr/share/guile/site:$appdir/usr/share/guile/{guile_version}:$appdir/usr/share/guile${{GUILE_LOAD_PATH:+:$GUILE_LOAD_PATH}}"
export GUILE_LOAD_COMPILED_PATH="$appdir/usr/lib64/guile/{guile_version}/ccache:$appdir/usr/lib/guile/{guile_version}/ccache${{GUILE_LOAD_COMPILED_PATH:+:$GUILE_LOAD_COMPILED_PATH}}"
export MAGICK_CONFIGURE_PATH="$appdir/usr/share/ImageMagick-7:$appdir/usr/lib64/ImageMagick-7.1.1/config-Q16HDRI${{MAGICK_CONFIGURE_PATH:+:$MAGICK_CONFIGURE_PATH}}"
export MAGICK_CODER_MODULE_PATH="$appdir/usr/lib64/ImageMagick-7.1.1/modules-Q16HDRI/coders${{MAGICK_CODER_MODULE_PATH:+:$MAGICK_CODER_MODULE_PATH}}"

athena_locale_charmap="$(locale charmap 2>/dev/null || true)"
case "$athena_locale_charmap" in
  UTF-8|utf8|UTF8)
    ;;
  *)
    athena_utf8_locale=""
    while IFS= read -r athena_locale_name; do
      case "$athena_locale_name" in
        C.UTF-8|C.utf8|en_US.UTF-8|en_US.utf8)
          athena_utf8_locale="$athena_locale_name"
          break
          ;;
      esac
    done < <(locale -a 2>/dev/null || true)
    [ -n "$athena_utf8_locale" ] || athena_utf8_locale="C.UTF-8"
    unset LC_ALL
    export LANG="$athena_utf8_locale"
    export LC_CTYPE="$athena_utf8_locale"
    ;;
esac
unset athena_locale_charmap
unset athena_utf8_locale
unset athena_locale_name

unset QT_IM_MODULE
unset GTK_IM_MODULE
unset XMODIFIERS
export QT_AUTO_SCREEN_SCALE_FACTOR=0
unset QT_SCALE_FACTOR
unset QT_SCREEN_SCALE_FACTORS
unset QT_FONT_DPI
unset QT_SCALE_FACTOR_ROUNDING_POLICY

platform_args=()
has_platform_arg=0
for arg in "$@"; do
  case "$arg" in
    --platform|-platform|--platform=*|-platform=*)
      has_platform_arg=1
      ;;
  esac
done

if [ "$has_platform_arg" -eq 0 ]; then
  platform_args=(--platform "${{ATHENA_QT_PLATFORM:-wayland}}")
fi

cd "$athena_dir"
exec "$athena_dir/bin/ATHENA.bin" "${{platform_args[@]}}" "$@"
""".format(guile_version=gv))
    apprun.chmod(apprun.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def write_desktop_and_icon(appdir: Path, runtime: Path):
    desktop = appdir / "ATHENA.desktop"
    desktop.write_text("""[Desktop Entry]
Type=Application
Name=ATHENA
GenericName=Advanced Typesetting and Hypertext Environment for Notes and Archives
Comment=A structured WYSIWYG scientific editing platform
Exec=ATHENA %f
Icon=ATHENA
Terminal=false
Categories=Education;Science;Office;
MimeType=application/x-athena;text/x-texmacs.doc;text/x-texmacs.sty;text/plain;text/x-tex;
""")
    icon_src = runtime / "misc/images/ATHENA-512.png"
    if icon_src.exists():
        copy_file(icon_src, appdir / "ATHENA.png")
        copy_file(icon_src, appdir / ".DirIcon")
        copy_file(icon_src, appdir / "usr/share/icons/hicolor/512x512/apps/ATHENA.png")
    copy_file(desktop, appdir / "usr/share/applications/ATHENA.desktop")


def fail_on_new_glibc(appdir: Path):
    offenders: list[str] = []
    for path in (appdir / "usr/lib").glob("*.so*"):
        if path.is_symlink() or not is_elf(path):
            continue
        try:
            text = output(["readelf", "--version-info", str(path)],
                          stderr=subprocess.DEVNULL)
        except subprocess.CalledProcessError:
            continue
        versions = sorted(set(re.findall(r"GLIBC_2\.(\d+)", text)))
        high = [v for v in versions if int(v) >= 42]
        if high:
            offenders.append(f"{path.name}: GLIBC_2.{max(high, key=int)}")
    if offenders:
        raise SystemExit("Packaged libraries require too-new glibc:\n" +
                         "\n".join(offenders))


def main():
    if len(sys.argv) != 5:
        print("usage: package_appimage.py RUNTIME_DIR OUT.AppImage APPDIR APPIMAGETOOL",
              file=sys.stderr)
        return 2

    runtime = Path(sys.argv[1]).resolve()
    output_appimage = Path(sys.argv[2]).resolve()
    appdir = Path(sys.argv[3]).resolve()
    appimagetool = Path(sys.argv[4]).resolve()
    gv = guile_version()
    if gv != "1.8":
        raise SystemExit(f"container AppImage builds must use Guile 1.8, got {gv}")
    verify_runtime(runtime)
    verify_linux_services(runtime)

    if appdir.exists():
        shutil.rmtree(appdir)
    appdir.mkdir(parents=True)
    (appdir / "usr/share").mkdir(parents=True)
    shutil.copytree(runtime, appdir / "usr/share/ATHENA", symlinks=True)

    write_apprun(appdir)
    write_desktop_and_icon(appdir, runtime)
    copy_qt_plugins(appdir)
    copy_guile(appdir)
    copy_imagemagick(appdir)
    copy_dependencies(appdir)
    verify_runtime(appdir)
    verify_linux_services(appdir / "usr/share/ATHENA")
    fail_on_new_glibc(appdir)

    output_appimage.parent.mkdir(parents=True, exist_ok=True)
    if output_appimage.exists():
        output_appimage.unlink()
    env = os.environ.copy()
    env["ARCH"] = "x86_64"
    env["APPIMAGE_EXTRACT_AND_RUN"] = "1"
    run([str(appimagetool), str(appdir), str(output_appimage)], env=env)
    output_appimage.chmod(output_appimage.stat().st_mode | stat.S_IXUSR)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
