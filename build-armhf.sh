#!/usr/bin/env bash
# Build dnp3-bridge for BeagleBone Black (armhf) using Docker multiarch.
#
# Prerequisites:
#   1. Docker with buildx (user in docker group): docker buildx version
#   2. QEMU binfmt registered:
#      docker run --rm --privileged multiarch/qemu-user-static --reset -p yes
#
# Usage:
#   ./build-armhf.sh          # Build and extract binary to ./build/armhf/
#   ./build-armhf.sh deploy   # Build, extract, and scp to BeagleBone

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="${SCRIPT_DIR}/build/armhf"
BBB_HOST="${BBB_HOST:-192.168.7.2}"
BBB_USER="${BBB_USER:-debian}"
IMAGE_NAME="dnp3-bridge-armhf"

echo "==> Registering QEMU binfmt..."
docker run --rm --privileged multiarch/qemu-user-static --reset -p yes 2>/dev/null || true

echo "==> Building for armhf via Docker..."
docker build \
    --platform linux/arm/v7 \
    -f Dockerfile.armhf \
    -t "${IMAGE_NAME}" \
    --target builder \
    "${SCRIPT_DIR}"

echo "==> Extracting binaries..."
mkdir -p "${OUTPUT_DIR}"
CONTAINER_ID=$(docker create --platform linux/arm/v7 "${IMAGE_NAME}")
docker cp "${CONTAINER_ID}:/src/build/release/dnp3-bridge" "${OUTPUT_DIR}/dnp3-bridge"
docker cp "${CONTAINER_ID}:/src/build/release/dnp3-master-sim" "${OUTPUT_DIR}/dnp3-master-sim"
docker cp "${CONTAINER_ID}:/src/build/release/_deps/opendnp3-build/cpp/lib/libopendnp3.so" "${OUTPUT_DIR}/libopendnp3.so"
docker rm "${CONTAINER_ID}" > /dev/null

echo "==> Binaries ready at: ${OUTPUT_DIR}/"
file "${OUTPUT_DIR}/dnp3-bridge" "${OUTPUT_DIR}/dnp3-master-sim" "${OUTPUT_DIR}/libopendnp3.so"

if [[ "${1:-}" == "deploy" ]]; then
    echo "==> Deploying to ${BBB_USER}@${BBB_HOST}..."
    scp "${OUTPUT_DIR}/dnp3-bridge" "${OUTPUT_DIR}/dnp3-master-sim" "${OUTPUT_DIR}/libopendnp3.so" "${BBB_USER}@${BBB_HOST}:~/"
    echo "==> Deployed! SSH in and run: ~/dnp3-bridge"
fi
