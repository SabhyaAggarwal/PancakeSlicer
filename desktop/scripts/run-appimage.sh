#!/usr/bin/env bash
set -euo pipefail
image=${1:?Usage: run-appimage.sh /path/to/OrcaSlicer.AppImage [arguments...]}
shift
image=$(cd "$(dirname "$image")" && pwd)/$(basename "$image")
cache=${XDG_CACHE_HOME:-$HOME/.cache}/orca
if ((${#cache} > 40)); then cache=$HOME/.cache/orca; fi
mkdir -p "$cache"
image_runtime=$(mktemp -d "$cache/run.XXXXXXXX")
trap 'rm -rf -- "$image_runtime"' EXIT
TMPDIR="$image_runtime" APPIMAGE_EXTRACT_AND_RUN=1 "$image" "$@"
