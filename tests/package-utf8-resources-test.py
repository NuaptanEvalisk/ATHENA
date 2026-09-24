#!/usr/bin/env python3

from pathlib import Path
import re
import sys


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: package-utf8-resources-test.py ATHENA_RESOURCE_DIR")

    packages = Path(sys.argv[1]) / "packages"
    legacy = re.compile(r"\\<([^<>\\]+)\\>")
    failures: list[str] = []

    for path in sorted(packages.rglob("*.ts")):
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError as error:
            failures.append(f"{path}: invalid UTF-8 at byte {error.start}")
            continue

        for line_no, line in enumerate(text.splitlines(), 1):
            for match in legacy.finditer(line):
                token = match.group(1)
                if token == "wide-bar" and "<wide" in line:
                    continue
                failures.append(
                    f"{path}:{line_no}: legacy symbol token \\<{token}\\>"
                )

    if failures:
        print("Package UTF-8 resource validation failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print("ATHENA package resources are UTF-8; only wide-bar control descriptors remain")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
