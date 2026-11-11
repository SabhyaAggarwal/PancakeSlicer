#!/usr/bin/env bash
# apt-get update + install with retries; runner mirrors and DNS fail transiently.
#   apt-install.sh <package>...
set -euo pipefail
opts=(-o Acquire::Retries=5 -o Acquire::http::Timeout=30)
for attempt in 1 2 3; do
  if sudo apt-get "${opts[@]}" update && sudo apt-get "${opts[@]}" install -y --no-install-recommends "$@"; then exit 0; fi
  echo "apt attempt $attempt failed; retrying in $((attempt * 20)) s" >&2
  sleep $((attempt * 20))
done
exit 1
