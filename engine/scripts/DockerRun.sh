#!/bin/bash
set -x
docker run \
  ` ` \
  --net=host \
  ` ` \
  --ipc host \
  ` ` \
  -u "$USER" \
  ` ` \
  -v "$HOME:/home/$USER" \
  ` ` \
  -e DISPLAY="$DISPLAY" \
  ` ` \
  --privileged=true \
  ` ` \
  -ti \
  ` ` \
  --rm \
  ` ` \
  orcaslicer "$@"
