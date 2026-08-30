#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 11 ]]; then
  echo "usage: $0 BINARY OUTPUT ATHENA_PATH ATHENA_HOME GUILE_RUNTIME SOURCE_ROOT LLAMA_RUNTIME ADS_RUNTIME RESVG_RUNTIME JOBS RUNTIME_ID" >&2
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
resvg_runtime="$9"
jobs="${10}"
runtime_id="${11}"

if ! [[ "$jobs" =~ ^[1-9][0-9]*$ ]]; then
  echo "ATHENA Scheme bytecode: invalid worker count: $jobs" >&2
  exit 2
fi

export ATHENA_PATH="$athena_path"
export ATHENA_HOME_PATH="$athena_home"
export ATHENA_GUILE_RUNTIME_ROOT="$guile_runtime"
bootstrap_cache="$athena_home/system/guile-bytecode-bootstrap"
dependency_cache="$athena_home/system/guile-bytecode-completed"
staging="$athena_home/system/guile-bytecode-staging"
rm -rf "$bootstrap_cache"
rm -rf "$dependency_cache"
rm -rf "$staging"
mkdir -p "$output" "$bootstrap_cache" "$dependency_cache" "$staging"

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
export LD_LIBRARY_PATH="$llama_runtime:$ads_runtime:$resvg_runtime:$guile_runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

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

mapfile -d '' sources < <(
  find "$source_root" -type f -name '*.scm' -print0 | sort -z
)
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
hash_file="$manifest_dir/source-hashes.tsv"
printf '%s\n' "${sources[@]}" > "$source_list"
planner="$athena_path/../tools/plan-athena-scheme-bytecode.scm"
if [[ ! -r "$planner" ]]; then
  echo "ATHENA Scheme bytecode: dependency planner not found: $planner" >&2
  exit 1
fi
"$guile_runtime/bin/guile" --no-auto-compile "$planner" "$source_list" \
  > "$plan_file"

while IFS= read -r digest_line; do
  digest="${digest_line%% *}"
  source_file="${digest_line#*  }"
  printf '%s\t%s\n' "$digest" "$source_file"
done < <(sha256sum -- "${sources[@]}") > "$hash_file"

state_dir="$athena_home/incremental-state/$runtime_id"
previous_hash_file="$state_dir/source-hashes.tsv"
previous_plan_file="$state_dir/dependency-plan.tsv"
previous_toolchain_file="$state_dir/toolchain.sha256"
toolchain_inputs=(
  "$binary"
  "$guile_runtime/lib/libathena-guile.so.1"
  "$planner"
  "${BASH_SOURCE[0]}"
)
for input in "${toolchain_inputs[@]}"; do
  if [[ ! -r "$input" ]]; then
    echo "ATHENA Scheme bytecode: missing compiler input: $input" >&2
    exit 1
  fi
done
toolchain_hash="$({
  printf '%s\n' "$runtime_id"
  sha256sum -- "${toolchain_inputs[@]}"
} | sha256sum)"
toolchain_hash="${toolchain_hash%% *}"

declare -A current_hash=()
declare -A previous_hash=()
declare -A current_component=()
declare -A current_components=()
declare -A current_dependencies=()
declare -A current_outputs=()
declare -A affected=()

while IFS=$'\t' read -r digest source_file; do
  [[ -n "$source_file" ]] || continue
  current_hash["$source_file"]="$digest"
done < "$hash_file"

while IFS=$'\t' read -r level component source_file dependencies; do
  [[ -n "$source_file" ]] || continue
  current_component["$source_file"]="$component"
  current_components["$component"]=1
  current_dependencies["$component"]="$dependencies"
  relative="${source_file#"$source_root"/}"
  current_outputs["$output/${relative%.scm}.go"]=1
done < "$plan_file"

full_rebuild=0
full_rebuild_reason=""
runtime_refresh="${ATHENA_SCHEME_RUNTIME_REFRESH:-0}"
bootstrap_existing_outputs=0
if [[ "$runtime_refresh" == 1 &&
      ( ! -r "$previous_hash_file" || ! -r "$previous_plan_file" ) ]]; then
  # A normal ATHENA startup reaches this mode only after validating the
  # bundled runtime id and finding a source newer than its corresponding .go.
  # Seed incremental state from those complete existing outputs instead of
  # recompiling the entire Scheme tree on the first runtime refresh.
  bootstrap_existing_outputs=1
elif [[ ! -r "$previous_hash_file" || ! -r "$previous_plan_file" ||
        ! -r "$previous_toolchain_file" ]]; then
  full_rebuild=1
  full_rebuild_reason="incremental state is unavailable"
else
  while IFS=$'\t' read -r digest source_file; do
    [[ -n "$source_file" ]] || continue
    previous_hash["$source_file"]="$digest"
  done < "$previous_hash_file"
  previous_toolchain="$(<"$previous_toolchain_file")"
  if [[ "$runtime_refresh" != 1 &&
        "$previous_toolchain" != "$toolchain_hash" ]]; then
    full_rebuild=1
    full_rebuild_reason="compiler semantics changed"
  fi
fi

if (( full_rebuild )); then
  echo "ATHENA Scheme bytecode: full rebuild: $full_rebuild_reason"
  for component in "${!current_components[@]}"; do
    affected["$component"]=1
  done
elif (( bootstrap_existing_outputs )); then
  for source_file in "${sources[@]}"; do
    relative="${source_file#"$source_root"/}"
    compiled_file="$output/${relative%.scm}.go"
    if [[ ! -f "$compiled_file" || "$source_file" -nt "$compiled_file" ]]; then
      affected["${current_component[$source_file]}"]=1
    fi
  done
else
  for source_file in "${sources[@]}"; do
    if [[ -z "${previous_hash[$source_file]+set}" ||
          "${previous_hash[$source_file]}" != "${current_hash[$source_file]}" ]]; then
      affected["${current_component[$source_file]}"]=1
    fi
  done

  # A removed module can invalidate callers even though it is absent from the
  # new graph.  Follow reverse dependencies in the previous graph, then map
  # surviving callers into the current graph.
  declare -A old_component=()
  declare -A old_components=()
  declare -A old_dependencies=()
  declare -A old_component_sources=()
  declare -A old_affected=()
  while IFS=$'\t' read -r level component source_file dependencies; do
    [[ -n "$source_file" ]] || continue
    old_component["$source_file"]="$component"
    old_components["$component"]=1
    old_dependencies["$component"]="$dependencies"
    old_component_sources["$component"]+="$source_file"$'\n'
  done < "$previous_plan_file"
  for source_file in "${!previous_hash[@]}"; do
    if [[ -z "${current_hash[$source_file]+set}" &&
          -n "${old_component[$source_file]+set}" ]]; then
      old_affected["${old_component[$source_file]}"]=1
    fi
  done
  changed=1
  while (( changed )); do
    changed=0
    for component in "${!old_components[@]}"; do
      [[ -z "${old_affected[$component]+set}" ]] || continue
      IFS=',' read -r -a dependencies <<< "${old_dependencies[$component]}"
      for dependency in "${dependencies[@]}"; do
        if [[ -n "$dependency" && -n "${old_affected[$dependency]+set}" ]]; then
          old_affected["$component"]=1
          changed=1
          break
        fi
      done
    done
  done
  for component in "${!old_affected[@]}"; do
    while IFS= read -r source_file; do
      [[ -n "$source_file" ]] || continue
      if [[ -n "${current_component[$source_file]+set}" ]]; then
        affected["${current_component[$source_file]}"]=1
      fi
    done <<< "${old_component_sources[$component]}"
  done
fi

# Missing output is an invalid cache entry, regardless of source hashes.
for source_file in "${sources[@]}"; do
  relative="${source_file#"$source_root"/}"
  compiled_file="$output/${relative%.scm}.go"
  if [[ ! -f "$compiled_file" ||
        ( "$runtime_refresh" == 1 && "$source_file" -nt "$compiled_file" ) ]]; then
    affected["${current_component[$source_file]}"]=1
  fi
done

# Compile the complete reverse dependency closure.  This is required for
# imported macros and for SCC changes, even when downstream source text did
# not change.
changed=1
while (( changed )); do
  changed=0
  for component in "${!current_components[@]}"; do
    [[ -z "${affected[$component]+set}" ]] || continue
    IFS=',' read -r -a dependencies <<< "${current_dependencies[$component]}"
    for dependency in "${dependencies[@]}"; do
      if [[ -n "$dependency" && -n "${affected[$dependency]+set}" ]]; then
        affected["$component"]=1
        changed=1
        break
      fi
    done
  done
done

# Make unchanged bytecode immediately available to compiler workers.  Outputs
# selected for rebuilding remain hidden until their dependency level succeeds.
while IFS=$'\t' read -r level component source_file dependencies; do
  [[ -n "$source_file" ]] || continue
  [[ -z "${affected[$component]+set}" ]] || continue
  relative="${source_file#"$source_root"/}"
  compiled_file="$output/${relative%.scm}.go"
  dependency_file="$dependency_cache/${relative%.scm}.go"
  mkdir -p "$(dirname "$dependency_file")"
  ln -s "$compiled_file" "$dependency_file"
done < "$plan_file"

affected_count=0
for component in "${!affected[@]}"; do
  while IFS=$'\t' read -r level candidate source_file dependencies; do
    if [[ "$candidate" == "$component" ]]; then
      affected_count=$((affected_count + 1))
    fi
  done < "$plan_file"
done

if (( affected_count == 0 )); then
  echo "ATHENA Scheme bytecode: all modules are up to date"
else
  printf 'ATHENA Scheme bytecode: recompiling %d of %d modules\n' \
    "$affected_count" "${#sources[@]}"
fi

export binary output athena_home system_state
max_level="$(cut -f1 "$plan_file" | tail -n 1)"
for ((level=0; level<=max_level; level++)); do
  level_rows=()
  while IFS=$'\t' read -r candidate_level component source_file dependencies; do
    if [[ "$candidate_level" == "$level" &&
          -n "${affected[$component]+set}" ]]; then
      level_rows+=("$component"$'\t'"$source_file")
    fi
  done < "$plan_file"
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
    compiled_file="$staging/${relative%.scm}.go"
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
  # The single-quoted worker body intentionally expands only in the child.
  # shellcheck disable=SC2016
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
    compiled_file="$staging/${relative%.scm}.go"
    dependency_file="$dependency_cache/${relative%.scm}.go"
    mkdir -p "$(dirname "$dependency_file")"
    ln -s "$compiled_file" "$dependency_file"
  done
done

# Publish only after every affected component compiled successfully.  Until
# this point, the previous output tree remains a complete usable generation.
while IFS=$'\t' read -r level component source_file dependencies; do
  [[ -n "$source_file" ]] || continue
  [[ -n "${affected[$component]+set}" ]] || continue
  relative="${source_file#"$source_root"/}"
  staged_file="$staging/${relative%.scm}.go"
  compiled_file="$output/${relative%.scm}.go"
  if [[ ! -f "$staged_file" ]]; then
    echo "ATHENA Scheme bytecode: missing staged output: $staged_file" >&2
    exit 1
  fi
  mkdir -p "$(dirname "$compiled_file")"
  mv -f "$staged_file" "$compiled_file"
done < "$plan_file"

# Remove outputs for deleted or renamed sources, including stale files left by
# builds predating the incremental state format.
while IFS= read -r relative; do
  [[ -n "$relative" ]] || continue
  compiled_file="$output/$relative"
  if [[ -z "${current_outputs[$compiled_file]+set}" ]]; then
    rm -f "$compiled_file"
  fi
done < <(cd "$output" && find . -type f -name '*.go' -printf '%P\n' | sort)

compiled_count="$(find "$output" -type f -name '*.go' -printf '.\n' | wc -l)"
if [[ "$compiled_count" -ne "${#sources[@]}" ]]; then
  echo "ATHENA Scheme bytecode: expected ${#sources[@]} files, got $compiled_count" >&2
  exit 1
fi

mkdir -p "$state_dir"
cp "$hash_file" "$state_dir/source-hashes.tsv.tmp"
cp "$plan_file" "$state_dir/dependency-plan.tsv.tmp"
printf '%s\n' "$toolchain_hash" > "$state_dir/toolchain.sha256.tmp"
mv -f "$state_dir/source-hashes.tsv.tmp" "$previous_hash_file"
mv -f "$state_dir/dependency-plan.tsv.tmp" "$previous_plan_file"
mv -f "$state_dir/toolchain.sha256.tmp" "$previous_toolchain_file"
printf '%s\n%s\n' "$runtime_id" "$compiled_count" > "$output/.complete.tmp"
mv -f "$output/.complete.tmp" "$output/.complete"
rm -rf "$staging"
