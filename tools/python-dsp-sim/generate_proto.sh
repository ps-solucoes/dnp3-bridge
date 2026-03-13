#!/usr/bin/env bash
# Generate Python gRPC stubs from the proto file.
# Run from the repository root: bash tools/python-dsp-sim/generate_proto.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="$SCRIPT_DIR/generated"
VENV_PYTHON="$SCRIPT_DIR/.venv/bin/python3"

mkdir -p "$OUT_DIR"

"$VENV_PYTHON" -m grpc_tools.protoc \
    --proto_path="$REPO_ROOT/proto" \
    --python_out="$OUT_DIR" \
    --grpc_python_out="$OUT_DIR" \
    "$REPO_ROOT/proto/dnp3bridge.proto"

# Fix absolute imports to relative so the generated/ package works properly.
sed -i 's/^import dnp3bridge_pb2/from . import dnp3bridge_pb2/' "$OUT_DIR/dnp3bridge_pb2_grpc.py"

touch "$OUT_DIR/__init__.py"

echo "Generated Python stubs in $OUT_DIR"
