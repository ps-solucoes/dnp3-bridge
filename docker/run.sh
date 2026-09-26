#!/bin/sh
# Run a command in the build container, with the repository mounted at /src
# and the working directory mapped to the same place inside the repo.
# Builds the image on first use. Files are created as the calling user.
#
#   docker/run.sh cmake --workflow --preset armhf-release
#
# DNP3_BRIDGE_BUILD_IMAGE overrides the image tag (default: dnp3-bridge-build).
# DNP3_BRIDGE_REBUILD_IMAGE=1 forces a rebuild of the image.
set -eu

repo=$(cd "$(dirname "$0")/.." && pwd -P)
image=${DNP3_BRIDGE_BUILD_IMAGE:-dnp3-bridge-build}

if [ "${DNP3_BRIDGE_REBUILD_IMAGE:-0}" = 1 ] || ! docker image inspect "$image" >/dev/null 2>&1; then
    docker build -t "$image" -f "$repo/docker/build.Dockerfile" "$repo/docker"
fi

here=$(pwd -P)
case "$here/" in
    "$repo"/*) workdir="/src${here#"$repo"}" ;;
    *)         workdir=/src ;;
esac

tty=
if [ -t 0 ] && [ -t 1 ]; then tty=-it; fi

# shellcheck disable=SC2086  # $tty is intentionally unquoted
exec docker run --rm $tty \
    --user "$(id -u):$(id -g)" \
    --env HOME=/tmp \
    --volume "$repo:/src" \
    --workdir "$workdir" \
    "$image" "$@"
