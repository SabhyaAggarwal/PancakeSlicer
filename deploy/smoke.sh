#!/bin/sh
# Release smoke check against a running PancakeSlicer service: deploy/smoke.sh http://127.0.0.1:8080
# Fails unless the engine is qualified, the UI loads, and printer covers and bed models are served.
set -eu
base=${1:?base URL}
fail() { echo "smoke: FAIL $*" >&2; exit 1; }
health=$(curl -fsS "$base/api/healthz") || fail "healthz unreachable"
echo "$health" | grep -q '"artifact_state":"qualified"' || fail "engine not qualified: $(echo "$health" | head -c 300)"
check() { # path expected-status min-bytes
  out=$(curl -sS -o /dev/null -w '%{http_code} %{size_download}' "$base$1") || fail "$1 unreachable"
  code=${out% *}; size=${out#* }
  [ "$code" = "$2" ] || fail "$1 returned $code, expected $2"
  [ "$size" -ge "$3" ] || fail "$1 returned only $size bytes"
  echo "smoke: ok $code $size $1"
}
check / 200 500
check "/profiles/BBL/Bambu%20Lab%20A1%20mini_cover.png" 200 1000
check "/profiles/BBL/bbl-3dp-A1M.stl" 200 1000
check "/profiles/BBL/does-not-exist.stl" 404 0
echo "smoke: passed $base"
