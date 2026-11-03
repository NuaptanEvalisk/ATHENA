#!/usr/bin/env bash
set -euo pipefail

cat >&2 <<'EOF'
ATHENA's Windows release assembly has not been validated for the current native
runtime. Use configure-athena-win64.sh with a dedicated MinGW-w64 dependency
prefix, then build ATHENA.bin and verify it under Wine before packaging.
The embedded Scheme runtime is retired and is not a Windows build prerequisite.
EOF
exit 1
