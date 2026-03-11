# dnp3-bridge

A C++ service that acts as a DNP3 gateway for a BeagleBone-based system. It bridges a Python application (which handles Modbus communication with a DSP, logging, and data analysis) to a SCADA master via the DNP3 protocol.

```
DSP <== Modbus ==> BeagleBone [Python] <== gRPC ==> BeagleBone [C++] <== DNP3 ==> SCADA
```

The C++ side is intentionally minimal — it is a thin protocol translator. The Python application is the "brain" that handles all business logic.

Communication between the Python and C++ processes uses gRPC as an IPC mechanism. The DNP3 implementation uses [opendnp3](https://github.com/dnp3/opendnp3) (v3.1.2), an open-source C++ library.

## How It Works

The service runs a DNP3 outstation (slave) that a SCADA master connects to, and a gRPC server that the local Python application connects to. Data flows in both directions:

**Python to SCADA** — The Python app reads sensor data from the DSP via Modbus and pushes point updates (analog, binary, counter values) to the C++ service through the `UpdatePoints` gRPC call. The service writes these values into the DNP3 outstation database, making them available to the SCADA master via polls or unsolicited responses.

**SCADA to Python** — When the SCADA master sends control commands (CROB relay operations or analog output setpoints), the C++ service forwards them to the Python app over a gRPC server-streaming connection (`StreamCommands`). Python processes each command (e.g., sends a Modbus write to the DSP), then responds via `RespondToCommand`. The C++ service relays the result back to the SCADA master as the DNP3 command response.

## Prerequisites

- CMake 3.28+
- GCC 13+ (C++23 support required)
- gRPC and Protobuf system packages:
  ```bash
  sudo apt install libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc
  ```
- Internet access for the first build (CMake fetches opendnp3 and nlohmann_json via FetchContent)

## Building

```bash
cmake --preset dev        # configure (debug build)
cmake --build --preset dev  # build

cmake --preset release        # configure (release build)
cmake --build --preset release  # build
```

The binary is produced at `build/debug/dnp3-bridge` (or `build/release/dnp3-bridge`).

## Running

```bash
# With default configuration
./build/debug/dnp3-bridge

# With a JSON config file
./build/debug/dnp3-bridge config.json
```

See `config.example.json` for all available options. Environment variables can also be used and take precedence over the config file:

| Variable | Default | Description |
|---|---|---|
| `DNP3_BRIDGE_GRPC_ADDRESS` | `0.0.0.0:50051` | gRPC listen address |
| `DNP3_BRIDGE_DNP3_HOST` | `0.0.0.0` | DNP3 TCP server bind address |
| `DNP3_BRIDGE_DNP3_PORT` | `20000` | DNP3 TCP server port |
| `DNP3_BRIDGE_DNP3_LOCAL_ADDR` | `1024` | DNP3 outstation link-layer address |
| `DNP3_BRIDGE_DNP3_REMOTE_ADDR` | `1` | DNP3 master link-layer address |
| `DNP3_BRIDGE_COMMAND_TIMEOUT_MS` | `3000` | Timeout waiting for Python to respond to a SCADA command |
| `DNP3_BRIDGE_LOG_LEVEL` | `info` | Log level |

Configuration precedence: defaults < config file < environment variables.

## gRPC API

The service exposes a single gRPC service (`BridgeService`) defined in `proto/dnp3bridge.proto`:

| RPC | Direction | Description |
|---|---|---|
| `UpdatePoints` | Python → SCADA | Send point updates (analog, binary, counter) to the outstation |
| `GetStatus` | Python → C++ | Query outstation connection state |
| `StreamCommands` | SCADA → Python | Server-streaming RPC that pushes SCADA commands to Python |
| `RespondToCommand` | Python → C++ | Return command execution result (matched by `command_id`) |

## Python Mock Client

A mock Python client is included for testing and as a reference implementation:

```bash
# One-time setup
python3 -m venv python/.venv
python/.venv/bin/pip install -r python/requirements.txt
bash python/generate_proto.sh

# Run (with the C++ service already running)
python/.venv/bin/python3 python/mock_client.py
```

The mock client sends simulated sensor updates every 2 seconds and listens for SCADA commands on a background thread, responding to each with `SUCCESS`.

## Project Structure

```
proto/dnp3bridge.proto         # gRPC service and message definitions
src/
  main.cpp                     # Entry point, component wiring
  config/                      # Configuration (JSON file + env vars)
  grpc/                        # gRPC server and service implementation
  bridge/                      # Thread-safe update queue between gRPC and DNP3
  dnp3/                        # DNP3 outstation, command handling, dispatcher
python/
  mock_client.py               # Mock Python client for testing
  generate_proto.sh            # Generates Python gRPC stubs
config.example.json            # Example configuration file
CMakeLists.txt                 # Build system (single file)
CMakePresets.json              # Build presets (dev/release)
```
