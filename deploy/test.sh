#!/bin/sh
# Release test against a running PancakeSlicer service: deploy/test.sh http://127.0.0.1:6868
# Fails unless the engine passed its release check, the UI loads, and printer covers and bed models are served.
set -eu
base=${1:?base URL}
fail() { echo "test: FAIL $*" >&2; exit 1; }
health=$(curl -fsS "$base/api/healthz") || fail "healthz unreachable"
echo "$health" | grep -q '"artifact_state":"qualified"' || fail "engine has not passed its release check: $(echo "$health" | head -c 300)"
check() { # path expected-status min-bytes
  out=$(curl -sS -o /dev/null -w '%{http_code} %{size_download}' "$base$1") || fail "$1 unreachable"
  code=${out% *}; size=${out#* }
  [ "$code" = "$2" ] || fail "$1 returned $code, expected $2"
  [ "$size" -ge "$3" ] || fail "$1 returned only $size bytes"
  echo "test: ok $code $size $1"
}
check / 200 500
check "/profiles/BBL/Bambu%20Lab%20A1%20mini_cover.png" 200 1000
check "/profiles/BBL/bbl-3dp-A1M.stl" 200 1000
check "/profiles/BBL/does-not-exist.stl" 404 0
echo "test: passed $base"
