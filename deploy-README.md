# dnp3-bridge — Deployment Package

## Contents

```bash
deploy/
├── bin/
│   ├── dnp3-bridge          # Bridge service (gRPC <-> DNP3)
│   ├── dnp3-master-sim      # DNP3 master simulator (TUI)
│   └── libopendnp3.so       # opendnp3 shared library
├── python-dsp-sim/          # Python DSP simulator (TUI)
│   ├── tui.py
│   ├── setup.sh             # One-time Python setup
│   └── generated/           # gRPC/protobuf stubs
└── config.example.json      # Bridge configuration example
```

## BeagleBone Setup

Tested on **Debian 13 (Trixie)** — `am335x-debian-13.4-base-v6.12-armhf-2026-03-17`.

### 1. Copy files to the BeagleBone

```bash
scp bin/dnp3-bridge bin/dnp3-master-sim bin/libopendnp3.so debian@192.168.7.2:~/
```

### 2. SSH in and install dependencies

```bash
ssh debian@192.168.7.2   # password: temppwd

sudo cp ~/libopendnp3.so /usr/local/lib/
sudo ldconfig
sudo apt update && sudo apt install -y libgrpc++-dev libprotobuf-dev
chmod +x ~/dnp3-bridge ~/dnp3-master-sim
```

### 3. Run the bridge

```bash
~/dnp3-bridge
```

The bridge listens on:
- gRPC: `0.0.0.0:50051`
- DNP3 outstation: `0.0.0.0:20000`

### 4. Run the DNP3 master simulator (optional, for testing)

In a second SSH session to the BeagleBone:

```bash
~/dnp3-master-sim
```

Connects to the bridge's DNP3 port on `127.0.0.1:20000`. Provides an interactive TUI to send polls, CROBs, and analog output commands. Use `--help` for options.

Stop with `Q` or `Ctrl+C`.

## Python TUI (from your PC)

The Python TUI connects to the bridge via gRPC to push point updates and receive SCADA commands.

### One-time setup

```bash
cd python-dsp-sim
bash setup.sh
```

### Run

```bash
cd python-dsp-sim
.venv/bin/python3 tui.py --address 192.168.7.2:50051
```

Replace `192.168.7.2` with the BeagleBone's IP. Default is `localhost:50051`.

## Configuration

The bridge can be configured via environment variables or a JSON config file (env vars take precedence).

```bash
# Environment variables
DNP3_BRIDGE_GRPC_ADDRESS=0.0.0.0:50051
DNP3_BRIDGE_DNP3_PORT=20000
DNP3_BRIDGE_LOG_LEVEL=info    # trace | debug | info | warn | error

# Or pass a JSON config file
~/dnp3-bridge /path/to/config.json
```

## Troubleshooting

**`error while loading shared libraries: libopendnp3.so`**
Run `sudo ldconfig` again, or check `sudo ldconfig -p | grep opendnp3`.

**Bridge exits immediately with "bind failed"**
Port 50051 or 20000 is already in use. Check with `ss -tlnp | grep -E '50051|20000'`.
