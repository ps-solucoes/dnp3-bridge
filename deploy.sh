#!/usr/bin/env bash
# Create a deployment package for the BeagleBone (armhf).
#
# Usage:
#   ./deploy.sh          # Package armhf binaries (must run build-armhf.sh first)
#   ./deploy.sh --native # Package native (x86) release binaries instead
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEPLOY_DIR="$SCRIPT_DIR/deploy"

if [[ "${1:-}" == "--native" ]]; then
    echo "=== Building native C++ release binaries ==="
    cmake --preset release -Wno-dev
    cmake --build --preset release -j"$(nproc)"
    BIN_DIR="$SCRIPT_DIR/build/release"
    SHIP_DNP3_LIB=false
else
    BIN_DIR="$SCRIPT_DIR/build/armhf"
    SHIP_DNP3_LIB=true
    if [[ ! -f "$BIN_DIR/dnp3-bridge" ]]; then
        echo "ERROR: armhf binaries not found at $BIN_DIR/"
        echo "Run ./build-armhf.sh first, or use ./deploy.sh --native for a native build."
        exit 1
    fi
    echo "=== Using armhf binaries from $BIN_DIR ==="
fi

echo "=== Preparing deploy folder ==="
rm -rf "$DEPLOY_DIR"
mkdir -p "$DEPLOY_DIR/bin"
mkdir -p "$DEPLOY_DIR/python-dsp-sim"

# C++ binaries
cp "$BIN_DIR/dnp3-bridge" "$DEPLOY_DIR/bin/"
cp "$BIN_DIR/dnp3-master-sim" "$DEPLOY_DIR/bin/"

# Shared library (armhf builds link opendnp3 dynamically)
if [[ "$SHIP_DNP3_LIB" == true ]]; then
    cp "$BIN_DIR/libopendnp3.so" "$DEPLOY_DIR/bin/"
fi

# Config example
cp "$SCRIPT_DIR/config.example.json" "$DEPLOY_DIR/"

# Python simulator (code + generated stubs)
PYSIM="$SCRIPT_DIR/tools/python-dsp-sim"
cp "$PYSIM/tui.py" "$DEPLOY_DIR/python-dsp-sim/"
cp "$PYSIM/point_map.py" "$DEPLOY_DIR/python-dsp-sim/"
cp "$PYSIM/requirements.txt" "$DEPLOY_DIR/python-dsp-sim/"
cp -r "$PYSIM/generated" "$DEPLOY_DIR/python-dsp-sim/generated"
# Remove __pycache__
find "$DEPLOY_DIR/python-dsp-sim" -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true

# Setup script for Python venv
cat > "$DEPLOY_DIR/python-dsp-sim/setup.sh" << 'SETUP_EOF'
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
echo "Creating Python virtual environment..."
python3 -m venv .venv
echo "Installing dependencies..."
.venv/bin/pip install -q -r requirements.txt
echo "Done. Run the simulator with:"
echo "  .venv/bin/python3 tui.py"
SETUP_EOF
chmod +x "$DEPLOY_DIR/python-dsp-sim/setup.sh"

# README
cp "$SCRIPT_DIR/deploy-README.md" "$DEPLOY_DIR/README.md"

echo ""
echo "=== Deploy folder ready at: $DEPLOY_DIR ==="
echo ""
ls -lhR "$DEPLOY_DIR"
