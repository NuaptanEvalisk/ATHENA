#!/usr/bin/env bash
set -euo pipefail

cat >&2 <<'EOF'
ATHENA's native Windows build is temporarily unavailable.

ATHENA now requires its vendored, native module-aware Guile 3 runtime. The
MinGW-w64 port of that private runtime has not been completed, and the removed
Guile 1.8 target must not be used as a fallback.
EOF
exit 1
