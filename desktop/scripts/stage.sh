#!/usr/bin/env bash
set -euo pipefail
os=$1 arch=$2 tool=$3 sha=$4 what=${5:-all}
here=$(cd "$(dirname "$0")/.." && pwd)
repo=$(cd "$here/.." && pwd)
source_root=${SOURCE_ROOT:-$repo}
build=$here/build
engine=$build/engine-$os-$arch
rev=$(git -C "$source_root" rev-parse HEAD)
packaging_rev=$(git -C "$repo" rev-parse HEAD)

if [[ $what == web || $what == all ]]; then
  rm -rf "$build/web"
  mkdir -p "$build"
  src=${WEB_SRC:-$source_root}
  node -e 'const fs = require("node:fs"); const [src, build] = process.argv.slice(1); fs.writeFileSync(build + "/vite-desktop.config.mts", `import config from ${JSON.stringify(src + "/web/vite.config.ts")};\nexport default { ...config, cacheDir: ${JSON.stringify(build + "/vite-cache")} };\n`);' "$src" "$build"
  (cd "$src/web" && ${VITE_LOCK:+flock "$VITE_LOCK"} npx vite build --config "$build/vite-desktop.config.mts" --outDir "$build/web" --emptyOutDir --logLevel warn)
  git -C "$src" rev-parse HEAD > "$build/web/REVISION.txt"
fi

if [[ $what == engine || $what == all ]]; then
  rm -rf "$engine"
  mkdir -p "$engine/resources"
  goos=$os; [[ $os == mac ]] && goos=darwin
  goarch=$arch; [[ $arch == x64 ]] && goarch=amd64
  (cd "$source_root/server" && GOOS=$goos GOARCH=$goarch CGO_ENABLED=0 go build -trimpath \
    -ldflags "-s -w -X main.buildRevision=$rev -X main.buildImage=orca-desktop-$os-$arch-tool-${sha:0:10}" \
    -o "$engine/orca-webapp-server" .)
  cp "$source_root/server/schemas.json" "$engine/schemas.json"
  cp "$tool" "$engine/nonplanar_slice_tool"
  chmod 755 "$engine/nonplanar_slice_tool"
  if [[ -n ${FROZEN_RESOURCES:-} ]]; then
    cp -a "$FROZEN_RESOURCES/." "$engine/resources/"
  else
    git -C "$repo" archive "$sha" resources/profiles resources/nonplanar resources/info resources/printers resources/filament_mixing resources/fonts resources/handy_models \
      | tar -x -C "$engine/resources" --strip-components=1
  fi
  if [[ -n ${SURFACE_MANIFEST_SOURCE:-} ]]; then
    cp "$SURFACE_MANIFEST_SOURCE" "$engine/surface-manifest.json"
    python3 -I - "$engine" "$sha" <<'PY'
import hashlib, json, pathlib, sys
engine = pathlib.Path(sys.argv[1])
manifest = json.loads((engine / 'surface-manifest.json').read_text())
digest = lambda p: hashlib.file_digest(p.open('rb'), 'sha256').hexdigest()
assert manifest['source_sha'].startswith(sys.argv[2]), 'tool source differs from manifest'
assert digest(engine / 'nonplanar_slice_tool') == manifest['binary_sha256'], 'tool hash differs from manifest'
root = engine / 'resources'
h = hashlib.sha256()
count = 0
def visit(directory):
    global count
    for p in sorted(directory.iterdir(), key=lambda p: p.name):
        assert not p.is_symlink(), 'nonregular resource: ' + str(p)
        if p.is_dir():
            visit(p)
        else:
            assert p.is_file(), 'nonregular resource: ' + str(p)
            h.update((p.relative_to(root).as_posix() + '\0' + digest(p) + '\n').encode())
            count += 1
visit(root)
assert h.hexdigest() == manifest['resources_sha256'], 'resource hash differs from manifest'
assert count == manifest['resource_file_count'], 'resource file count differs from manifest'
print('Verified frozen surface bundle:', manifest['source_sha'], count, 'resources', h.hexdigest())
PY
  fi
  if [[ -n ${ARTIFACT_QUALIFICATION_SOURCE:-} ]]; then
    cp "$ARTIFACT_QUALIFICATION_SOURCE" "$engine/artifact-qualification.ready"
    python3 -I - "$engine" <<'PY'
import hashlib, json, pathlib, sys
engine = pathlib.Path(sys.argv[1])
raw = (engine / 'surface-manifest.json').read_bytes()
manifest = json.loads(raw)
proof = json.loads((engine / 'artifact-qualification.ready').read_bytes())
assert proof['ready'] is True, 'artifact proof is not ready'
for key in ('source_sha', 'binary_sha256', 'resources_sha256'):
    assert proof[key] == manifest[key], 'artifact proof mismatch: ' + key
assert proof['manifest_sha256'] == hashlib.sha256(raw).hexdigest(), 'artifact proof manifest hash mismatch'
print('Verified qualification marker:', proof['source_sha'], proof['manifest_sha256'])
PY
  fi
  if [[ $os == linux ]]; then
    mkdir -p "$engine/lib"
    ldd "$tool" | awk '$3 ~ /^\// {print $3}' | while read -r lib; do
      case $(basename "$lib") in libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|ld-linux*) continue ;; esac
      cp -L "$lib" "$engine/lib/"
    done
  fi
  printf 'tool %s\ntool_sha %s\napp_rev %s\npackaging_rev %s\nstaged %s\n' "$(basename "$tool")" "$sha" "$rev" "$packaging_rev" "$(date -u +%FT%TZ)" > "$engine/ENGINE.txt"
fi
git -C "$repo" show "$sha:resources/Icon.icns" > "$build/icon.icns"
python3 -I - "$build/icon.icns" "$build/icon.png" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read(); i = 8
while i < len(d):
    t, n = struct.unpack('>4sI', d[i:i + 8])
    if t == b'ic09': open(sys.argv[2], 'wb').write(d[i + 8:i + n])
    i += n
PY
du -sh "$build"/* | sed "s|$build/||"
