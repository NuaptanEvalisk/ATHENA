#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

ssh_target="${ATHENA_IPADOS_SSH_TARGET:-felix@127.0.0.1}"
ssh_port="${ATHENA_IPADOS_SSH_PORT:-2222}"
remote_developer_root="${ATHENA_IPADOS_REMOTE_DEVELOPER_ROOT:-/Users/felix/Developer}"
remote_source="${ATHENA_IPADOS_REMOTE_SOURCE:-${remote_developer_root}/ATHENA}"
remote_marker="${remote_developer_root}/.athena-linux-source-mirror"
remote_manifest="${remote_developer_root}/.athena-linux-source-manifest"
remote_state="${remote_developer_root}/.athena-linux-source-state"

die () {
  printf 'sync-source-to-vm: %s\n' "$*" >&2
  exit 1
}

case "${remote_source}" in
  "${remote_developer_root}"/ATHENA) ;;
  *) die "refusing to manage unexpected remote source path: ${remote_source}" ;;
esac

command -v git >/dev/null 2>&1 || die "git is required on the Linux source host"
command -v rsync >/dev/null 2>&1 || die "rsync is required on the Linux source host"
command -v python3 >/dev/null 2>&1 || die "python3 is required on the Linux source host"

git -C "${repo_root}" rev-parse --is-inside-work-tree >/dev/null 2>&1 || \
  die "${repo_root} is not a Git working tree"

manifest="$(mktemp)"
trap 'rm -f "${manifest}"' EXIT

# The mirror contains exactly the current working-tree source set: tracked files
# plus untracked files that Git does not ignore.  Build trees, model weights,
# deployed binaries and other ignored local state therefore never enter the VM
# source mirror.
git -C "${repo_root}" ls-files -co --exclude-standard -z >"${manifest}"
[[ -s "${manifest}" ]] || die "source manifest is empty"

head_commit="$(git -C "${repo_root}" rev-parse HEAD)"

tree_sha256="$({
  cd "${repo_root}"
  python3 - "${manifest}" <<'PY'
import hashlib
import os
import stat
import sys

manifest_path = sys.argv[1]
with open(manifest_path, "rb") as handle:
    raw_paths = [entry for entry in handle.read().split(b"\0") if entry]

digest = hashlib.sha256()
for raw_path in sorted(raw_paths):
    rel = os.fsdecode(raw_path)
    info = os.lstat(rel)
    digest.update(raw_path)
    digest.update(b"\0")
    if stat.S_ISLNK(info.st_mode):
        digest.update(b"L\0")
        digest.update(os.fsencode(os.readlink(rel)))
    elif stat.S_ISREG(info.st_mode):
        digest.update(b"F\0")
        digest.update(b"X" if info.st_mode & 0o111 else b"-")
        with open(rel, "rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
    else:
        raise SystemExit(f"unsupported source entry type: {rel}")
    digest.update(b"\0")

print(digest.hexdigest())
PY
})"

ssh_cmd=(ssh -o BatchMode=yes -o ConnectTimeout=10 -p "${ssh_port}")
rsync_ssh="ssh -o BatchMode=yes -o ConnectTimeout=10 -p ${ssh_port}"

"${script_dir}/bootstrap-vm.sh" --quiet

printf 'Preparing dedicated VM paths and source-mirror guard...\n'
"${ssh_cmd[@]}" "${ssh_target}" sh -s -- \
  "${remote_developer_root}" "${remote_source}" "${remote_marker}" <<'REMOTE_PREP'
set -eu

developer_root=$1
source_root=$2
marker=$3

mkdir -p \
  "$developer_root" \
  "$developer_root/athena-deps/host" \
  "$developer_root/athena-deps/ipados" \
  "$developer_root/athena-build/ipados" \
  "$developer_root/athena-artifacts"

if [ -e "$marker" ]; then
  recorded=$(cat "$marker")
  [ "$recorded" = "$source_root" ] || {
    echo "source mirror marker names '$recorded', refusing '$source_root'" >&2
    exit 1
  }
elif [ -d "$source_root" ] && [ -n "$(find "$source_root" -mindepth 1 -print -quit)" ]; then
  echo "unclaimed non-empty source directory exists at $source_root" >&2
  exit 1
else
  printf '%s\n' "$source_root" >"$marker"
fi

mkdir -p "$source_root"

# A completed mirror is deliberately read-only.  Make only ordinary entries
# owner-writable for the duration of this controlled synchronization.  Symlinks
# are never followed.
python3 - "$source_root" <<'PY'
import os
import stat
import sys

root = sys.argv[1]
for current, dirs, files in os.walk(root, topdown=True, followlinks=False):
    for name in dirs + files:
        path = os.path.join(current, name)
        if os.path.islink(path):
            continue
        mode = os.stat(path, follow_symlinks=False).st_mode
        os.chmod(path, mode | stat.S_IWUSR, follow_symlinks=False)
mode = os.stat(root, follow_symlinks=False).st_mode
os.chmod(root, mode | stat.S_IWUSR, follow_symlinks=False)
PY
REMOTE_PREP

# Keep the current manifest outside the source tree.  It is used to remove files
# deleted on Linux and to reject any VM-only source residue.
printf -v remote_manifest_q '%q' "${remote_manifest}"
cat "${manifest}" | "${ssh_cmd[@]}" "${ssh_target}" \
  "cat > ${remote_manifest_q}"

rsync_args=(
  -a
  --no-owner
  --no-group
  --checksum
  --from0
  --files-from="${manifest}"
  --stats
  -e "${rsync_ssh}"
)

printf 'Previewing incremental source transfer...\n'
rsync --dry-run "${rsync_args[@]}" "${repo_root}/" \
  "${ssh_target}:${remote_source}/"

printf 'Synchronizing current Linux working tree...\n'
rsync "${rsync_args[@]}" "${repo_root}/" \
  "${ssh_target}:${remote_source}/"

printf 'Removing stale VM-only entries, verifying content, and locking the mirror read-only...\n'
remote_result="$(${ssh_cmd[@]} "${ssh_target}" python3 - \
  "${remote_source}" "${remote_manifest}" "${remote_state}" \
  "${head_commit}" "${tree_sha256}" <<'REMOTE_FINALIZE'
import hashlib
import json
import os
import shutil
import stat
import sys
from pathlib import Path

root = os.path.abspath(sys.argv[1])
manifest_path = sys.argv[2]
state_path = sys.argv[3]
head_commit = sys.argv[4]
expected_digest = sys.argv[5]

with open(manifest_path, "rb") as handle:
    raw_paths = [entry for entry in handle.read().split(b"\0") if entry]

desired = set()
desired_dirs = {""}
for raw_path in raw_paths:
    rel = os.fsdecode(raw_path)
    if os.path.isabs(rel) or rel in ("", "."):
        raise SystemExit(f"unsafe manifest path: {rel!r}")
    normalized = os.path.normpath(rel)
    if normalized != rel or normalized == ".." or normalized.startswith("../"):
        raise SystemExit(f"unsafe manifest path: {rel!r}")
    desired.add(rel)
    parent = os.path.dirname(rel)
    while parent:
        desired_dirs.add(parent)
        parent = os.path.dirname(parent)

removed = []

def clean_directory(abs_dir, rel_dir):
    with os.scandir(abs_dir) as iterator:
        entries = list(iterator)
    for entry in entries:
        rel = entry.name if not rel_dir else rel_dir + "/" + entry.name
        path = entry.path
        if entry.is_symlink():
            if rel not in desired:
                os.unlink(path)
                removed.append(rel)
            continue
        if entry.is_dir(follow_symlinks=False):
            if rel not in desired_dirs:
                shutil.rmtree(path)
                removed.append(rel + "/")
            else:
                clean_directory(path, rel)
            continue
        if rel not in desired:
            os.unlink(path)
            removed.append(rel)

clean_directory(root, "")

missing = [rel for rel in sorted(desired) if not os.path.lexists(os.path.join(root, rel))]
if missing:
    raise SystemExit("mirror is missing source entries: " + ", ".join(missing[:20]))

digest = hashlib.sha256()
for raw_path in sorted(raw_paths):
    rel = os.fsdecode(raw_path)
    path = os.path.join(root, rel)
    info = os.lstat(path)
    digest.update(raw_path)
    digest.update(b"\0")
    if stat.S_ISLNK(info.st_mode):
        digest.update(b"L\0")
        digest.update(os.fsencode(os.readlink(path)))
    elif stat.S_ISREG(info.st_mode):
        digest.update(b"F\0")
        digest.update(b"X" if info.st_mode & 0o111 else b"-")
        with open(path, "rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
    else:
        raise SystemExit(f"unsupported mirrored entry type: {rel}")
    digest.update(b"\0")

actual_digest = digest.hexdigest()
if actual_digest != expected_digest:
    raise SystemExit(
        f"mirror digest mismatch: expected {expected_digest}, got {actual_digest}"
    )

# Remove write permission from all ordinary source entries without following
# symlinks.  Build/configure work must happen outside this mirror.
for current, dirs, files in os.walk(root, topdown=False, followlinks=False):
    for name in files + dirs:
        path = os.path.join(current, name)
        if os.path.islink(path):
            continue
        mode = os.stat(path, follow_symlinks=False).st_mode
        os.chmod(path, mode & ~0o222, follow_symlinks=False)
mode = os.stat(root, follow_symlinks=False).st_mode
os.chmod(root, mode & ~0o222, follow_symlinks=False)

state = {
    "linux_git_head": head_commit,
    "source_tree_sha256": actual_digest,
    "manifest_entries": len(desired),
    "remote_source": root,
    "vm_source_writable": False,
}
with open(state_path, "w", encoding="utf-8") as handle:
    json.dump(state, handle, indent=2, sort_keys=True)
    handle.write("\n")

print(f"TREE_SHA256={actual_digest}")
print(f"MANIFEST_ENTRIES={len(desired)}")
print(f"REMOVED_ENTRIES={len(removed)}")
REMOTE_FINALIZE
)"

printf '%s\n' "${remote_result}"
grep -qx "TREE_SHA256=${tree_sha256}" <<<"${remote_result}" || \
  die "remote mirror verification did not return the expected tree digest"

printf 'VM source mirror synchronized and locked: %s:%s\n' \
  "${ssh_target}" "${remote_source}"
printf 'Linux HEAD: %s\n' "${head_commit}"
printf 'Tree SHA-256: %s\n' "${tree_sha256}"
