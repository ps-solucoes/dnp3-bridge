# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
cmake --preset dev -Wno-dev    # configure (debug)
cmake --build --preset dev     # build all targets
./build/debug/dnp3-bridge      # run

cmake --preset release         # configure (release)
cmake --build --preset release # build (release)
```

Release packages are built in the Debian trixie container (Docker; `sg docker -c '…'` if your shell predates the group):

```bash
docker/run.sh cmake --workflow --preset release   # configure, build, test, package
docker/run.sh cmake --workflow --preset armhf-release   # same for the BeagleBone: *_armhf.deb in build/armhf-release/
```

This leaves `dnp3-bridge_<ver>_amd64.deb` (`/usr/bin/dnp3-bridge`) and `dnp3-bridge-tools_<ver>_amd64.deb` (`/usr/bin/dnp3-master-sim`) in `build/release/`. opendnp3 is linked statically; only the `bridge` and `tools` CPack components are packaged, so FetchContent dependencies' install rules never reach a .deb.

Releasing: bump `project(VERSION)` in `CMakeLists.txt`, add its `## [<ver>]` section to `CHANGELOG.md`, and push the tag `v<ver>`; the Release workflow publishes the four .debs.

System dependencies (apt): `libgrpc++-dev`, `libprotobuf-dev`, `protobuf-compiler-grpc`

After modifying `proto/dnp3bridge.proto`, a rebuild will regenerate the C++ sources automatically.

## Testing

```bash
cmake --build --preset dev --target dnp3-bridge-tests              # build unit tests
cmake --build --preset dev --target dnp3-bridge-integration-tests  # build integration tests

./build/debug/dnp3-bridge-tests -s                                 # run unit tests (verbose)
./build/debug/dnp3-bridge-integration-tests -s                     # run integration tests (verbose)
./build/debug/dnp3-bridge-tests -tc="ConfigLoader*"                # run single test case by name
```

Tests use doctest. Unit tests cover ConfigLoader, Bridge, CommandDispatcher, and PointMap. Integration tests spin up the full stack in-process (gRPC client + DNP3 master + bridge) on test ports (50052/20001).

## Architecture

This is a C++23 bridge service running on a BeagleBone that translates between a Python application (gRPC) and a SCADA system (DNP3). The full system chain is:

```
DSP <==Modbus==> BeagleBone[Python] <==gRPC==> BeagleBone[C++] <==DNP3==> SCADA
```

The C++ side is intentionally thin — Python is the "brain" that handles business logic, logging, and data analysis. This service is purely a protocol translator.

### Bidirectional Data Flow

```
Python → SCADA (point updates):
  Python ──gRPC UpdatePoints──> BridgeServiceImpl ──> Bridge (queue) ──> OutstationManager ──DNP3──> SCADA

SCADA → Python (commands):
  SCADA ──DNP3 OPERATE──> ForwardingCommandHandler ──> CommandDispatcher ──gRPC stream──> Python
  Python ──gRPC RespondToCommand──> CommandDispatcher ──> ForwardingCommandHandler ──DNP3 response──> SCADA
```

### Component Layers

- **`src/grpc/`** — gRPC server. `BridgeServiceImpl` implements `BridgeService` (sync API). `GrpcServer` owns the `grpc::Server` lifecycle. Uses `::grpc::` namespace prefix to avoid collision with `dnp3bridge::grpc`.
- **`src/bridge/`** — Decoupling layer. `Bridge` accepts `PointUpdate` variants via a thread-safe queue and flushes to `OutstationManager` on a background `std::jthread`. `DataModel.hpp` defines the variant types and the `Quality` enum shared by the gRPC and DNP3 layers.
- **`src/dnp3/`** — DNP3 outstation using opendnp3 3.1.2:
  - `OutstationManager` — owns DNP3Manager, TCP server channel, and outstation. Decides event generation itself (`EventMode::Force`/`Suppress`) rather than using `EventMode::Detect`: opendnp3's `IsEvent()` treats *any* flags difference as an event before consulting the deadband, so quality changes would otherwise both emit URs (violating REQ-11) and bypass the deadband (REQ-08). Only the value decides; the deadband reference is the last **evented** value.
  - `ForwardingCommandHandler` — implements `ICommandHandler`, forwards SCADA commands (CROB, analog outputs) to Python via `CommandDispatcher`
  - `CommandDispatcher` — coordination hub for the reverse command path. Uses `std::promise`/`std::future` pairs keyed by command ID with configurable timeout. Manages a `ServerWriter` for the streaming RPC.
- **`src/config/`** — `AppConfig` struct with defaults; `ConfigLoader` reads from JSON file then overlays environment variables.
- **`src/main.cpp`** — Wires all components, configures spdlog, installs signal handlers, runs gRPC on a `jthread`.

### Command Dispatch Mechanism

When SCADA sends a DNP3 command, `ForwardingCommandHandler::Operate()` is called synchronously on the opendnp3 IO thread. It builds a `CommandRequest` proto, passes it to `CommandDispatcher::dispatch()` which:
1. Inserts a `std::promise` into a pending map
2. Writes the request to the active gRPC `ServerWriter` (the `StreamCommands` stream)
3. Blocks on `future.wait_for(timeout)` until Python calls `RespondToCommand`
4. Returns the `CommandStatus` to opendnp3

`SELECT` operations return `SUCCESS` unconditionally (validation happens at `OPERATE` time in Python). DNP3Manager runs with 2 threads so a blocked `Operate()` doesn't stall the link layer.

### Namespaces

All project code lives under `dnp3bridge::` with sub-namespaces: `config`, `bridge`, `grpc`, `dnp3`.

### Threading Model

Four thread domains: gRPC sync server thread pool, Bridge flush `jthread`, opendnp3's internal ASIO threads (2), and the main thread polling for shutdown signals. The Bridge queue synchronizes gRPC→DNP3; the CommandDispatcher promise/future pairs synchronize DNP3→gRPC.

### Crash Safety

`SIGPIPE` is ignored in `main.cpp` — gRPC stream writes to a disconnected client must not kill the process. `ForwardingCommandHandler` and `CommandDispatcher` wrap all gRPC writes in try/catch, returning `DOWNSTREAM_FAIL` on exceptions.

## Build System Notes

- Single `CMakeLists.txt` at root (no subdirectory build files).
- opendnp3 is fetched via `FetchContent` (tag 3.1.2) and **must be compiled as C++14** — its bundled ASIO uses `concept bool` (Concepts TS) which is invalid in C++20+. This is enforced via `set_target_properties` after `FetchContent_MakeAvailable`.
- gRPC/protobuf come from system apt packages via `find_package`.
- Proto codegen uses `add_custom_command` with `VERBATIM` — do not add quotes around `--proto_path=` / `--cpp_out=` flag values (causes double-escaping).
- Generated proto headers land in `${CMAKE_BINARY_DIR}/generated/` and are included as `"dnp3bridge.pb.h"` / `"dnp3bridge.grpc.pb.h"`.

## Logging

Uses spdlog. All source files use `spdlog::info/debug/warn/error/trace()` — no `std::cerr` in `src/`.

Log levels used: `critical` (server bind failure), `error` (exceptions), `warn` (timeouts, missing stream, missing/partial configuration), `info` (lifecycle events, effective DNP3 point database), `debug` (RPC calls, command dispatch), `trace` (individual point values, flush batches).

At startup the outstation logs the effective per-point database (class, deadband, variations) at `info`, coalescing consecutive identical points into ranges. This is the authoritative view of what opendnp3 received — check it first when point behaviour looks wrong.

When `log_file` is configured, logs go to both stderr (with color) and a rotating file.

## Configuration

Loaded from JSON config file (optional CLI arg), then environment variables override. Env vars always take precedence.

| Variable | JSON key | Default |
|---|---|---|
| `DNP3_BRIDGE_GRPC_ADDRESS` | `grpc_listen_address` | `0.0.0.0:50051` |
| `DNP3_BRIDGE_DNP3_HOST` | `dnp3_channel_host` | `0.0.0.0` |
| `DNP3_BRIDGE_DNP3_PORT` | `dnp3_channel_port` | `20000` |
| `DNP3_BRIDGE_DNP3_LOCAL_ADDR` | `dnp3_local_address` | `1024` |
| `DNP3_BRIDGE_DNP3_REMOTE_ADDR` | `dnp3_remote_address` | `1` |
| `DNP3_BRIDGE_COMMAND_TIMEOUT_MS` | `command_timeout_ms` | `3000` |
| `DNP3_BRIDGE_COMMAND_MODE` | `command_mode` | `direct_operate` |
| `DNP3_BRIDGE_LOG_LEVEL` | `log_level` | `info` |
| `DNP3_BRIDGE_LOG_FILE` | `log_file` | *(empty — disabled)* |
| `DNP3_BRIDGE_LOG_MAX_SIZE_MB` | `log_max_size_mb` | `5` |
| `DNP3_BRIDGE_LOG_MAX_FILES` | `log_max_files` | `3` |

### DNP3 Configuration (JSON only, no env var override)

These sections are configured in the JSON config file only. See `config.example.json` for a full example.

**`unsolicited`** — Controls unsolicited response behavior:
- `enabled` (bool, default: `true`) — enable/disable URs globally
- `class_mask` (array, default: `["class1", "class2"]`) — which classes trigger URs

**`event_buffer`** — Event buffer sizes (minimum 100 total per CEMIG):
- `max_binary_events` (default: `50`), `max_analog_events` (default: `50`), others default to `0`

**`point_database`** — Per-point DNP3 database configuration. Entries support `"index": N` (single point) or `"range": [start, end]` (inclusive range). Each entry can set:
- `class` — event class (`"class0"`, `"class1"`, `"class2"`, `"class3"`)
- `deadband` — analog deadband threshold (double). When omitted, the point keeps whatever an earlier matching entry set (opendnp3 default is `0.0`)
- `static_variation` / `event_variation` — DNP3 object variations (e.g., `"Group30Var2"`)

When `point_database` is absent, hardcoded defaults are used (11 BI, 20 BO, 21 AI, 5 AO).

`class`, `static_variation` and `event_variation` are validated at startup by `dnp3::validatePointDatabase()`; an unrecognized value (including wrong case) fails startup with the offending key and the accepted names. Values are case-sensitive: `class2`, not `Class2`; `Group30Var2`, not `Group30var2`.

## Simulators

Two test tools in `tools/` for end-to-end testing without real hardware:

- **`tools/dnp3-master-sim/`** — C++ DNP3 master with interactive CLI (polls, CROB, analog commands). Links only opendnp3, no gRPC. Uses `std::cout` for CLI output (not spdlog).
- **`tools/python-dsp-sim/`** — Python gRPC client simulating the DSP/Python side. Supports `--mode interactive` (menu) and `--mode auto` (headless). Has its own venv and `generate_proto.sh`.
