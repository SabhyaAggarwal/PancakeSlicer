#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd -P)
case "$here/" in "$HOME/agents/wa-paradigm/"*) ;; *) echo 'Extract the kit under ~/agents/wa-paradigm first.' >&2; exit 1 ;; esac
[[ $(uname -s) == Darwin && $(uname -m) == arm64 ]] || { echo 'Requires macOS arm64.' >&2; exit 1; }
if [[ ${ORCA_MAC_NICED:-0} != 1 ]]; then
  exec env ORCA_MAC_NICED=1 nice -n 19 bash "$0" "$@"
fi
cd "$here"
mkdir -p .cache/tmp .cache/node .cache/npm .cache/electron .cache/electron-builder
export TMPDIR="$here/.cache/tmp" npm_config_cache="$here/.cache/npm"
export ELECTRON_CACHE="$here/.cache/electron" ELECTRON_BUILDER_CACHE="$here/.cache/electron-builder"
export CSC_IDENTITY_AUTO_DISCOVERY=false npm_config_jobs=4 UV_THREADPOOL_SIZE=4 GOMAXPROCS=4
export OMP_NUM_THREADS=4 TBB_NUM_THREADS=4

node_version=22.16.0
node_dir="$here/.cache/node/node-v$node_version-darwin-arm64"
if [[ ! -x $node_dir/bin/node ]]; then
  archive="node-v$node_version-darwin-arm64.tar.gz"
  (cd .cache/node
    curl --fail --location --retry 3 -o "$archive" "https://nodejs.org/dist/v$node_version/$archive"
    curl --fail --location --retry 3 -o SHASUMS256.txt "https://nodejs.org/dist/v$node_version/SHASUMS256.txt"
    awk -v name="$archive" '$2 == name {print}' SHASUMS256.txt > NODE-SHA256SUMS
    [[ -s NODE-SHA256SUMS ]]
    shasum -a 256 -c NODE-SHA256SUMS
    tar -xzf "$archive")
fi
export PATH="$node_dir/bin:$PATH"

frozen=${1:-$HOME/np/bin/c334881648}
frozen=$(cd "$frozen" && pwd -P)
case "$frozen/" in "$HOME/np/"*) ;; *) echo 'Expected a frozen build under ~/np.' >&2; exit 1 ;; esac
engine="$here/build/engine-mac-arm64"
[[ -x $engine/orca-webapp-server && -f build/web/index.html ]]
[[ -f $frozen/nonplanar_slice_tool && -d $frozen/resources/profiles ]]
cp "$frozen/nonplanar_slice_tool" "$engine/nonplanar_slice_tool"
chmod u+w,ugo+x "$engine/nonplanar_slice_tool"
rm -rf "$engine/resources"
cp -R "$frozen/resources" "$engine/resources"
if [[ -d build/resource-extras ]]; then cp -R build/resource-extras/. "$engine/resources/"; fi
[[ -d $engine/resources/fonts && -d $engine/resources/handy_models ]]
rm -rf "$engine/lib"
python3 scripts/bundle-mac-libs.py "$frozen/nonplanar_slice_tool" "$engine"
{
  printf 'frozen_build %s\nstaged %s\n' "$frozen" "$(date -u +%FT%TZ)"
  shasum -a 256 "$engine/nonplanar_slice_tool" "$engine/orca-webapp-server"
} > "$engine/ENGINE.txt"
otool -L "$engine/nonplanar_slice_tool" > "$engine/DYLIBS.txt"
for lib in "$engine"/lib/*; do
  if [[ -f $lib ]]; then codesign --force --sign - "$lib"; fi
done
codesign --force --sign - "$engine/nonplanar_slice_tool"
codesign --force --sign - "$engine/orca-webapp-server"
npm ci --no-audit --no-fund
npm run dist:mac
(cd dist && shasum -a 256 ./*.dmg ./*.zip > SHA256SUMS)
printf 'Build complete: %s/dist\n' "$here"
