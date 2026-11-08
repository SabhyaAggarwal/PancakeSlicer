#!/usr/bin/env bash
set -u

BIN="${1:-}"
PY="${2:-python3}"
SRC="${3:-}"
[ -x "$BIN" ] || { echo "SKIP: orca-slicer binary not found: $BIN"; exit 77; }
[ -f "$SRC/resources/handy_models/OrcaBadge.3mf" ] || { echo "SKIP: handy model not found"; exit 77; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/orca-cli-emptycfg.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/datadir"

cp "$SRC/resources/handy_models/OrcaBadge.3mf" "$WORK/empty_config.3mf"
"$PY" - "$WORK/empty_config.3mf" <<'PYEOF'
import shutil, sys, zipfile

path = sys.argv[1]
entry = "Metadata/project_settings.config"
with zipfile.ZipFile(path) as src:
    items = [(i, src.read(i.filename)) for i in src.infolist()]
with zipfile.ZipFile(path + ".new", "w", zipfile.ZIP_DEFLATED) as dst:
    seen = False
    for info, data in items:
        if info.filename == entry:
            data, seen = b"{\n}\n", True
        dst.writestr(info, data)
    if not seen:
        dst.writestr(entry, b"{\n}\n")
shutil.move(path + ".new", path)
PYEOF

"$BIN" --datadir "$WORK/datadir" --info "$WORK/empty_config.3mf" > "$WORK/info.txt" 2>&1
rc=$?
if [ $rc -ne 0 ]; then
    echo "FAIL: --info on a project with empty settings exited $rc"
    tail -20 "$WORK/info.txt"
    exit 1
fi
grep -q "size_x" "$WORK/info.txt" || { echo "FAIL: --info printed no geometry"; cat "$WORK/info.txt"; exit 1; }
echo "PASS: a project with empty settings loads"
