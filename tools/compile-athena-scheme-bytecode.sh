#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 10 ]]; then
  echo "usage: $0 BINARY OUTPUT ATHENA_PATH ATHENA_HOME GUILE_RUNTIME SOURCE_ROOT LLAMA_RUNTIME ADS_RUNTIME JOBS RUNTIME_ID" >&2
  exit 2
fi

binary="$1"
output="$2"
athena_path="$3"
athena_home="$4"
guile_runtime="$5"
source_root="$6"
llama_runtime="$7"
ads_runtime="$8"
jobs="$9"
runtime_id="${10}"

if ! [[ "$jobs" =~ ^[1-9][0-9]*$ ]]; then
  echo "ATHENA Scheme bytecode: invalid worker count: $jobs" >&2
  exit 2
fi

export ATHENA_PATH="$athena_path"
export ATHENA_HOME_PATH="$athena_home"
export ATHENA_GUILE_RUNTIME_ROOT="$guile_runtime"
bootstrap_cache="$athena_home/system/guile-bytecode-bootstrap"
dependency_cache="$athena_home/system/guile-bytecode-completed"
rm -rf "$bootstrap_cache"
rm -rf "$dependency_cache"
mkdir -p "$bootstrap_cache" "$dependency_cache"

# Every worker must perform the ordinary source bootstrap first: legacy ATHENA
# modules intentionally publish compatibility bindings into a shared root
# module in startup order. The worker adds completed dependency levels to
# Guile's compiled path only after that bootstrap has finished.
export ATHENA_GUILE_CACHE_PATH="$bootstrap_cache"
export ATHENA_SCHEME_DEPENDENCY_CACHE_PATH="$dependency_cache"
export ATHENA_GUILE_SOURCE_ROOT="$source_root"
export ATHENA_SCHEME_COMPILE=1
export GUILE_AUTO_COMPILE=0
export GUILE_WARN_DEPRECATED=no
export QT_QPA_PLATFORM=offscreen
export LD_LIBRARY_PATH="$llama_runtime:$ads_runtime:$guile_runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

mkdir -p "$athena_home"

# Match the normal launcher when the selected llama.cpp build uses SYCL.
if [[ -e "$llama_runtime/libggml-sycl.so.0" ]]; then
  for oneapi_setup in /opt/intel/oneapi/setvars.sh \
                      "$HOME/intel/oneapi/setvars.sh"; do
    if [[ -r "$oneapi_setup" ]]; then
      set +u
      # shellcheck disable=SC1090
      source "$oneapi_setup" --force >/dev/null 2>&1
      set -u
      break
    fi
  done
fi

mapfile -d '' sources < <(rg --files -0 -g '*.scm' "$source_root" | sort -z)
if [[ ${#sources[@]} -eq 0 ]]; then
  echo "ATHENA Scheme bytecode: no Scheme sources below $source_root" >&2
  exit 1
fi

manifest_dir="$athena_home/manifests"
rm -rf "$manifest_dir"
mkdir -p "$manifest_dir"
system_state="$manifest_dir/sys_state.json"
printf '%s\n' \
  '{' \
  '  "compatibility_version": "2.1.4",' \
  '  "format": "athena-system-state",' \
  '  "tex": {' \
  '    "design_dpi": 600,' \
  '    "kpsepath": false,' \
  '    "kpsewhich": false,' \
  '    "make_pk": false,' \
  '    "make_tfm": false' \
  '  },' \
  '  "version": 1' \
  '}' > "$system_state"

source_list="$manifest_dir/sources.list"
plan_file="$manifest_dir/dependency-plan.tsv"
printf '%s\n' "${sources[@]}" > "$source_list"
planner="$athena_path/../tools/plan-athena-scheme-bytecode.scm"
if [[ ! -r "$planner" ]]; then
  echo "ATHENA Scheme bytecode: dependency planner not found: $planner" >&2
  exit 1
fi
"$guile_runtime/bin/guile" --no-auto-compile "$planner" "$source_list" \
  > "$plan_file"

export binary output athena_home system_state
max_level="$(cut -f1 "$plan_file" | tail -n 1)"
for ((level=0; level<=max_level; level++)); do
  mapfile -t level_rows < <(awk -F '\t' -v level="$level" \
    '$1 == level { print $2 "\t" $3 }' "$plan_file")
  [[ ${#level_rows[@]} -gt 0 ]] || continue
  mapfile -t level_components < <(printf '%s\n' "${level_rows[@]}" |
    cut -f1 | sort -nu)

  worker_count="$jobs"
  if (( worker_count > ${#level_components[@]} )); then
    worker_count="${#level_components[@]}"
  fi
  manifests=()
  for ((worker=0; worker<worker_count; worker++)); do
    manifest="$manifest_dir/level-$level-worker-$worker.list"
    manifests+=("$manifest")
    : > "$manifest"
  done

  declare -A component_workers=()
  next_worker=0
  for row in "${level_rows[@]}"; do
    component="${row%%$'\t'*}"
    source_file="${row#*$'\t'}"
    if [[ -z "${component_workers[$component]+set}" ]]; then
      component_workers[$component]="$next_worker"
      next_worker=$(( (next_worker + 1) % worker_count ))
    fi
    worker="${component_workers[$component]}"
    relative="${source_file#"$source_root"/}"
    compiled_file="$output/${relative%.scm}.go"
    mkdir -p "$(dirname "$compiled_file")"
    printf '%s\0%s\0' "$source_file" "$compiled_file" \
      >> "${manifests[worker]}"
  done
  unset component_workers

  for manifest in "${manifests[@]}"; do
    rm -f "${manifest}.success"
  done

  printf 'ATHENA Scheme bytecode: dependency level %d (%d modules)\n' \
    "$level" "${#level_rows[@]}"
  printf '%s\0' "${manifests[@]}" |
    xargs -0 -r -n 1 -P "$worker_count" /bin/bash -c '
      set -euo pipefail
      manifest="$1"
      worker_name="${manifest##*/}"
      worker_home="$athena_home/workers/${worker_name%.list}"
      rm -rf "$worker_home"
      mkdir -p "$worker_home/fonts" "$worker_home/server" "$worker_home/system"
      cp "$system_state" "$worker_home/system/sys_state.json"
      export ATHENA_HOME_PATH="$worker_home"
      "$binary" -compile-scheme-bytecode-worker-list "$output" "$manifest"
      : > "${manifest}.success"
    ' _

  # A worker marker proves that its entire SCC-preserving manifest completed.
  # Do not retry crashes: a failed worker indicates a runtime/compiler defect,
  # and the bytecode set must never hide one behind a successful second run.
  for manifest in "${manifests[@]}"; do
    if [[ ! -e "${manifest}.success" ]]; then
      echo "ATHENA Scheme bytecode: worker failed: ${manifest##*/}" >&2
      exit 1
    fi
  done

  # Never expose bytecode from a layer while sibling workers are still
  # producing it.  The compiler load path points at this separate symlink
  # forest, and a dependency level becomes visible only after every worker in
  # that level has completed successfully.
  for row in "${level_rows[@]}"; do
    source_file="${row#*$'\t'}"
    relative="${source_file#"$source_root"/}"
    compiled_file="$output/${relative%.scm}.go"
    dependency_file="$dependency_cache/${relative%.scm}.go"
    mkdir -p "$(dirname "$dependency_file")"
    ln -s "$compiled_file" "$dependency_file"
  done
done

compiled_count="$(rg --files -g '*.go' "$output" | wc -l)"
if [[ "$compiled_count" -ne "${#sources[@]}" ]]; then
  echo "ATHENA Scheme bytecode: expected ${#sources[@]} files, got $compiled_count" >&2
  exit 1
fi

printf '%s\n%s\n' "$runtime_id" "$compiled_count" \
  > "$output/.complete"
