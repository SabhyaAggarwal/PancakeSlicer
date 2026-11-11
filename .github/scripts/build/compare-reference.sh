#!/usr/bin/env bash
# Report how this commit's engine reference differs from the base commit's, in the job summary. Never fails the job.
#   compare-reference.sh <base-sha> <new-reference-dir> <new-reference-asset>
set -uo pipefail
base=$1 new=$2 asset=$3
summary=${GITHUB_STEP_SUMMARY:-/dev/stdout}
here=$(dirname "$0")
if ! git cat-file -e "$base^{commit}" 2>/dev/null && ! git fetch -q --depth=1 origin "$base"; then
  echo "Engine reference: base $base is not available, nothing to compare." >> "$summary"; exit 0
fi
index=$(mktemp); GIT_INDEX_FILE=$index git read-tree "$base"
base_asset=$(GIT_INDEX_FILE=$index GITHUB_OUTPUT= "$here/keys.sh" linux-x64 linux 2>/dev/null | sed -n 's/^reference=//p'); rm -f "$index"
if [ "$base_asset" = "$asset" ]; then
  echo "Engine reference: unchanged from the base (same engine source, resources and qualifier)." >> "$summary"; exit 0
fi
dir=$(mktemp -d)
if ! "$here/prebuilt.sh" fetch "$base_asset" "$dir" >/dev/null; then
  echo "Engine reference: the base reference $base_asset is not published, nothing to compare." >> "$summary"; exit 0
fi
python3 "$here/compare_reference.py" "$dir" "$new" >> "$summary" || echo "Engine reference: comparison failed." >> "$summary"
exit 0
