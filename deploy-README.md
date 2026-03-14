# dnp3-bridge — Deployment Package

This package contains three pre-built tools for the DNP3 bridge system. No compilation required.

## Contents

```
deploy/
├── bin/
│   ├── dnp3-bridge          # Bridge service (gRPC <-> DNP3)
│   └── dnp3-master-sim      # DNP3 master simulator (TUI)
├── python-dsp-sim/          # Python DSP simulator
│   ├── tui.py               # TUI interface (Textual)
│   ├── dsp_sim.py           # CLI interface (interactive/auto)
│   ├── setup.sh             # One-time Python setup
│   └── generated/           # gRPC/protobuf stubs
└── config.example.json      # Bridge configuration example
```

## System Requirements

- **C++ binaries**: Ubuntu 24.04 (or compatible). Requires system libraries:
  ```bash
  sudo apt install libgrpc++-dev libprotobuf-dev
  ```
- **Python simulator**: Python 3.10+

## Quick Start

### 1. Set up the Python simulator (one-time)

```bash
cd python-dsp-sim
bash setup.sh
```

### 2. Start the bridge

```bash
# With defaults (gRPC on :50051, DNP3 on :20000)
./bin/dnp3-bridge

# With custom config
./bin/dnp3-bridge config.example.json

# Or override individual settings via environment variables
DNP3_BRIDGE_LOG_LEVEL=debug ./bin/dnp3-bridge
```

Stop with `Ctrl+C`.

### 3. Start the DNP3 master simulator

```bash
# Connect to bridge on localhost (default)
./bin/dnp3-master-sim

# Connect to a remote bridge
./bin/dnp3-master-sim --host 192.168.1.10 --port 20000
```

Options: `--host`, `--port`, `--local-addr`, `--remote-addr` (run with `--help` for details).

### 4. Start the Python DSP simulator

```bash
cd python-dsp-sim

# TUI (recommended)
.venv/bin/python3 tui.py

# CLI — interactive menu
.venv/bin/python3 dsp_sim.py --mode interactive

# CLI — auto-pilot (sends data every 2s, auto-responds to commands)
.venv/bin/python3 dsp_sim.py --mode auto
```

Both accept `--address host:port` (default: `localhost:50051`).

## Full Test Setup

Open three terminals to test the complete chain without hardware:

```
Terminal 1:  ./bin/dnp3-bridge
Terminal 2:  ./bin/dnp3-master-sim
Terminal 3:  cd python-dsp-sim && .venv/bin/python3 tui.py
```

Data flows:

```
Python sim --gRPC--> dnp3-bridge --DNP3--> Master sim     (point updates)
Python sim <--gRPC-- dnp3-bridge <--DNP3-- Master sim     (commands)
```

## Bridge Configuration

Edit `config.example.json` or use environment variables (env vars take precedence):

| Variable | Default | Description |
|---|---|---|
| `DNP3_BRIDGE_GRPC_ADDRESS` | `0.0.0.0:50051` | gRPC listen address |
| `DNP3_BRIDGE_DNP3_HOST` | `0.0.0.0` | DNP3 TCP server bind address |
| `DNP3_BRIDGE_DNP3_PORT` | `20000` | DNP3 TCP server port |
| `DNP3_BRIDGE_DNP3_LOCAL_ADDR` | `1024` | DNP3 outstation address |
| `DNP3_BRIDGE_DNP3_REMOTE_ADDR` | `1` | DNP3 master address |
| `DNP3_BRIDGE_COMMAND_TIMEOUT_MS` | `3000` | Command response timeout (ms) |
| `DNP3_BRIDGE_LOG_LEVEL` | `info` | Log level (trace/debug/info/warn/error) |
| `DNP3_BRIDGE_LOG_FILE` | *(disabled)* | Path to rotating log file |
