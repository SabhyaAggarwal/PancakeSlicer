#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd -P)"
build_dir=""
profiles_dir=""
config=""
build_tool=1
log_level=2
prune_source=0

args=()
for arg in "$@"; do
    if [ "$arg" = "--prune-source" ]; then prune_source=1; else args+=("$arg"); fi
done
set -- ${args+"${args[@]}"}

while getopts "b:p:c:l:nh" opt; do
    case $opt in
        b) build_dir="$OPTARG" ;;
        p) profiles_dir="$OPTARG" ;;
        c) config="$OPTARG" ;;
        n) build_tool=0 ;;
        l) log_level="$OPTARG" ;;
        h) sed -n '2,${/^#/!q;p;}' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) exit 1 ;;
    esac
done
shift $((OPTIND - 1))

if [ -z "$build_dir" ]; then
    for candidate in "$repo_root/build/arm64" "$repo_root/build/x86_64" "$repo_root/build"; do
        if [ -d "$candidate" ]; then build_dir="$candidate"; break; fi
    done
fi
if [ -z "$build_dir" ] || [ ! -d "$build_dir" ]; then
    echo "ERROR: build tree not found (pass -b <build_dir>)" >&2
    exit 1
fi

find_tool() {
    local best="" f
    while IFS= read -r f; do
        [ -n "$f" ] || continue
        if [ -z "$best" ] || [ "$f" -nt "$best" ]; then best="$f"; fi
    done < <(find "$build_dir" -name generate_system_cache -type f 2>/dev/null)
    printf '%s' "$best"
}

tool=$(find_tool)
if [ -z "$config" ]; then
    case "$tool" in
        */Debug/*)          config=Debug ;;
        */Release/*)        config=Release ;;
        */RelWithDebInfo/*) config=RelWithDebInfo ;;
        */MinSizeRel/*)     config=MinSizeRel ;;
        *) config=$(sed -n 's/^CMAKE_BUILD_TYPE:[A-Z]*=\(.\+\)$/\1/p' "$build_dir/CMakeCache.txt" 2>/dev/null | head -1 || true) ;;
    esac
fi

if [ "$build_tool" = 1 ]; then
    echo "Building generate_system_cache in $build_dir${config:+ ($config)}"
    build_args=(--build "$build_dir" --target generate_system_cache)
    if [ -n "$config" ]; then build_args+=(--config "$config"); fi
    if ! cmake "${build_args[@]}"; then
        echo "ERROR: could not build generate_system_cache — configure the build tree with -DORCA_TOOLS=ON:" >&2
        echo "       cmake -S \"$repo_root\" -B \"$build_dir\" -DORCA_TOOLS=ON" >&2
        exit 1
    fi
    tool=$(find_tool)
fi

if [ -z "$tool" ]; then
    echo "ERROR: generate_system_cache not found under $build_dir — build with -DORCA_TOOLS=ON" >&2
    exit 1
fi

if [ -z "$profiles_dir" ]; then profiles_dir="$repo_root/resources/profiles"; fi
if [ ! -d "$profiles_dir" ]; then
    echo "ERROR: profiles directory not found: $profiles_dir" >&2
    exit 1
fi
profiles_dir=$(cd "$profiles_dir" && pwd -P)

echo "Generating per-vendor preset caches in $profiles_dir"
rm -f "$profiles_dir"/*.opc "$profiles_dir"/*.cache
"$tool" --path "$profiles_dir" --log_level "$log_level"

for target in "$@"; do
    resolved=$(cd "$target" 2>/dev/null && pwd -P) || {
        echo "ERROR: profiles directory not found: $target" >&2
        exit 1
    }
    if [ "$resolved" = "$profiles_dir" ] && [ "$prune_source" -eq 0 ]; then
        echo "$resolved: skipped - this is where the caches were generated."
        echo "  Pass --prune-source to prune it; that deletes this checkout's preset JSONs."
        continue
    fi
    if [ "$resolved" != "$profiles_dir" ]; then
        cp "$profiles_dir"/*.opc "$resolved"/
    fi

    pruned=0
    shipped=0
    for cache in "$profiles_dir"/*.opc; do
        vendor=$(basename "$cache" .opc)
        shipped=$(( shipped + 1 ))
        if [ -f "$resolved/$vendor.json" ]; then
            rm -f "$resolved/$vendor.json"
            pruned=$(( pruned + 1 ))
        fi
        [ -d "$resolved/$vendor" ] || continue
        n=$(find "$resolved/$vendor" -name '*.json' | wc -l)
        find "$resolved/$vendor" -name '*.json' -delete
        find "$resolved/$vendor" -type d -empty -delete
        pruned=$(( pruned + n ))
    done
    echo "$resolved: $shipped caches, dropped $pruned preset JSONs"
done
