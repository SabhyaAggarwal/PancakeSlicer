#!/bin/bash
SCRIPT_DIR=$(cd -P -- "$(dirname -- "$0")" && printf '%s\n' "$(pwd -P)")
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

set -x
docker build -t orcaslicer \
  --build-arg USER="$USER" \
  --build-arg UID="$(id -u)" \
  --build-arg GID="$(id -g)" \
  --build-arg NCORES="$NCORES" \
  -f "$SCRIPT_DIR/Dockerfile" \
  "$PROJECT_ROOT"
