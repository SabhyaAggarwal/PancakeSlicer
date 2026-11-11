#!/usr/bin/env bash
# Upload files to release $TAG, skipping names it already has.
set -euo pipefail
attached=$(gh release view "$TAG" --repo "$GITHUB_REPOSITORY" --json assets --jq '.assets[].name')
missing=()
for f in "$@"; do
  [ -f "$f" ] || continue
  if grep -qxF "$(basename "$f")" <<<"$attached"; then echo "$(basename "$f") is already attached"; else missing+=("$f"); fi
done
if [ ${#missing[@]} -gt 0 ]; then gh release upload "$TAG" --repo "$GITHUB_REPOSITORY" "${missing[@]}"; fi
