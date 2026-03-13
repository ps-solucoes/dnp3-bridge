#!/usr/bin/env python3
"""
Python DSP Simulator for dnp3-bridge.

Simulates the Python side of the BeagleBone gateway:
  - Sends point updates to the C++ outstation (Python -> SCADA)
  - Listens for incoming SCADA commands via StreamCommands (SCADA -> Python)
  - Supports interactive menu and auto-pilot modes

Usage:
    python3 dsp_sim.py [--address localhost:50051] [--mode interactive|auto]
"""

import argparse
import logging
import random
import signal
import sys
import threading
import time

import grpc

# Add the generated directory to the path.
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from generated import dnp3bridge_pb2 as pb
from generated import dnp3bridge_pb2_grpc as pb_grpc

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("dsp_sim")

# ---------------------------------------------------------------------------
# Global state
# ---------------------------------------------------------------------------
shutdown_event = threading.Event()
auto_respond = True
auto_respond_lock = threading.Lock()

# ---------------------------------------------------------------------------
# Point name tables (mirrors the communication map)
# ---------------------------------------------------------------------------
BINARY_INPUT_NAMES = {
    0: "Equipamento Energizado",
    1: "Equipamento Operando",
    2: "Alarme",
    3: "Operacao Local/Remoto",
    4: "Botoeira de Emergencia",
    5: "Status Desequilibrio",
    6: "Status Reativo",
    7: "Status Suporte de Tensao",
    8: "Status Regulacao de Tensao",
    9: "Status Compensacao Harmonica",
    10: "Erro de IGBT",
}

ANALOG_INPUT_NAMES = {
    0: "Tensao Fase A",
    1: "Tensao Fase B",
    2: "Tensao Fase C",
    3: "Corrente Fase A",
    4: "Corrente Fase B",
    5: "Corrente Fase C",
    6: "Corrente Neutro",
    7: "THD Tensao Fase A",
    8: "THD Tensao Fase B",
    9: "THD Tensao Fase C",
    10: "THD Corrente Fase A",
    11: "THD Corrente Fase B",
    12: "THD Corrente Fase C",
    13: "Desequilibrio Negativo",
    14: "Desequilibrio Zero",
    15: "Tensao Link CC",
    16: "Temperatura Ponte",
    17: "Estado Atual de Operacao",
    18: "Modo Reativo",
    19: "Modo Harmonicos",
    20: "Codigo de Falta",
}

BINARY_OUTPUT_NAMES = {
    0: "Conectar Equipamento",
    1: "Desconectar Equipamento",
    2: "Reset Protecao",
    3: "Emergencia",
    4: "Ativa Desequilibrio",
    5: "Desativa Desequilibrio",
    6: "Ativa Suporte de Tensao",
    7: "Desativa Suporte de Tensao",
    8: "Ativa Regulacao de Tensao",
    9: "Desativa Regulacao de Tensao",
    10: "Ativa Compensacao Harmonica",
    11: "Desativa Compensacao Harmonica",
    12: "Compensa Harmonica 3",
    13: "Compensa Harmonica 5",
    14: "Compensa Harmonica 7",
    15: "Compensa Harmonica 9",
    16: "Compensa Harmonica 11",
    17: "Compensacao Harmonica Completa",
    18: "Aciona Ventilador Painel",
    19: "Aciona Ventilador Ponte",
}

ANALOG_OUTPUT_NAMES = {
    0: "Ref. Regulacao de Tensao",
    1: "Limite Corrente Deseq. Neg.",
    2: "Limite Corrente Deseq. Zero",
    3: "Limite Corrente Reativo",
    4: "Limite Corrente Harmonico",
}

COMMAND_TYPE_NAMES = {
    pb.COMMAND_TYPE_CROB: "CROB",
    pb.COMMAND_TYPE_ANALOG_INT16: "AnalogInt16",
    pb.COMMAND_TYPE_ANALOG_INT32: "AnalogInt32",
    pb.COMMAND_TYPE_ANALOG_FLOAT32: "AnalogFloat32",
    pb.COMMAND_TYPE_ANALOG_DOUBLE64: "AnalogDouble64",
}

CROB_OP_NAMES = {
    pb.CROB_OPERATION_NUL: "NUL",
    pb.CROB_OPERATION_PULSE_ON: "PULSE_ON",
    pb.CROB_OPERATION_PULSE_OFF: "PULSE_OFF",
    pb.CROB_OPERATION_LATCH_ON: "LATCH_ON",
    pb.CROB_OPERATION_LATCH_OFF: "LATCH_OFF",
}

# ---------------------------------------------------------------------------
# Signal handling
# ---------------------------------------------------------------------------
def handle_signal(signum, _frame):
    log.info("Signal %d received, shutting down...", signum)
    shutdown_event.set()


# ---------------------------------------------------------------------------
# Command listener (runs in background thread)
# ---------------------------------------------------------------------------
def command_listener(stub: pb_grpc.BridgeServiceStub):
    """Listen for SCADA commands on the StreamCommands stream and respond."""
    global auto_respond
    while not shutdown_event.is_set():
        try:
            log.info("Opening StreamCommands stream...")
            stream = stub.StreamCommands(pb.StreamCommandsRequest())

            for cmd in stream:
                cmd_type = COMMAND_TYPE_NAMES.get(cmd.command_type, "Unknown")
                bo_name = BINARY_OUTPUT_NAMES.get(cmd.point_index, "")
                ao_name = ANALOG_OUTPUT_NAMES.get(cmd.point_index, "")

                log.info(
                    "Received command #%d: type=%s index=%d",
                    cmd.command_id,
                    cmd_type,
                    cmd.point_index,
                )

                if cmd.HasField("crob"):
                    crob = cmd.crob
                    op_name = CROB_OP_NAMES.get(crob.operation, "?")
                    log.info(
                        "  CROB: op=%s count=%d on=%dms off=%dms  [%s]",
                        op_name,
                        crob.count,
                        crob.on_time_ms,
                        crob.off_time_ms,
                        bo_name,
                    )
                elif cmd.HasField("analog_value"):
                    log.info("  Analog value: %f  [%s]", cmd.analog_value, ao_name)

                with auto_respond_lock:
                    should_respond = auto_respond

                if should_respond:
                    time.sleep(0.1)  # Simulate Modbus round-trip
                    resp = stub.RespondToCommand(
                        pb.CommandResponse(
                            command_id=cmd.command_id,
                            status=pb.COMMAND_RESULT_SUCCESS,
                        )
                    )
                    log.info(
                        "  Responded to command #%d: ack=%s",
                        cmd.command_id,
                        resp.success,
                    )
                else:
                    log.info(
                        "  Auto-respond OFF — not responding to command #%d (will timeout)",
                        cmd.command_id,
                    )

        except grpc.RpcError as e:
            if shutdown_event.is_set():
                break
            log.warning("StreamCommands disconnected: %s. Reconnecting in 2s...", e.code())
            time.sleep(2)


# ---------------------------------------------------------------------------
# Point data helpers
# ---------------------------------------------------------------------------
def send_realistic_data(stub: pb_grpc.BridgeServiceStub):
    """Send all 11 binary inputs + 21 analog inputs with realistic values."""
    binaries = [
        pb.BinaryPoint(index=0, value=True, quality=pb.POINT_QUALITY_GOOD),   # Energizado
        pb.BinaryPoint(index=1, value=True, quality=pb.POINT_QUALITY_GOOD),   # Operando
        pb.BinaryPoint(index=2, value=False, quality=pb.POINT_QUALITY_GOOD),  # Alarme
        pb.BinaryPoint(index=3, value=True, quality=pb.POINT_QUALITY_GOOD),   # Local/Remoto
        pb.BinaryPoint(index=4, value=False, quality=pb.POINT_QUALITY_GOOD),  # Botoeira Emergencia
        pb.BinaryPoint(index=5, value=False, quality=pb.POINT_QUALITY_GOOD),  # Status Desequilibrio
        pb.BinaryPoint(index=6, value=True, quality=pb.POINT_QUALITY_GOOD),   # Status Reativo
        pb.BinaryPoint(index=7, value=False, quality=pb.POINT_QUALITY_GOOD),  # Status Suporte Tensao
        pb.BinaryPoint(index=8, value=False, quality=pb.POINT_QUALITY_GOOD),  # Status Regulacao Tensao
        pb.BinaryPoint(index=9, value=True, quality=pb.POINT_QUALITY_GOOD),   # Status Comp. Harmonica
        pb.BinaryPoint(index=10, value=False, quality=pb.POINT_QUALITY_GOOD), # Erro IGBT
    ]

    analogs = [
        pb.AnalogPoint(index=0, value=random.gauss(220.0, 2.0), quality=pb.POINT_QUALITY_GOOD),   # Tensao A
        pb.AnalogPoint(index=1, value=random.gauss(220.0, 2.0), quality=pb.POINT_QUALITY_GOOD),   # Tensao B
        pb.AnalogPoint(index=2, value=random.gauss(220.0, 2.0), quality=pb.POINT_QUALITY_GOOD),   # Tensao C
        pb.AnalogPoint(index=3, value=random.gauss(15.0, 1.0), quality=pb.POINT_QUALITY_GOOD),    # Corrente A
        pb.AnalogPoint(index=4, value=random.gauss(15.0, 1.0), quality=pb.POINT_QUALITY_GOOD),    # Corrente B
        pb.AnalogPoint(index=5, value=random.gauss(15.0, 1.0), quality=pb.POINT_QUALITY_GOOD),    # Corrente C
        pb.AnalogPoint(index=6, value=random.gauss(0.5, 0.1), quality=pb.POINT_QUALITY_GOOD),     # Corrente Neutro
        pb.AnalogPoint(index=7, value=random.uniform(2.0, 5.0), quality=pb.POINT_QUALITY_GOOD),   # THD Tensao A
        pb.AnalogPoint(index=8, value=random.uniform(2.0, 5.0), quality=pb.POINT_QUALITY_GOOD),   # THD Tensao B
        pb.AnalogPoint(index=9, value=random.uniform(2.0, 5.0), quality=pb.POINT_QUALITY_GOOD),   # THD Tensao C
        pb.AnalogPoint(index=10, value=random.uniform(2.0, 5.0), quality=pb.POINT_QUALITY_GOOD),  # THD Corrente A
        pb.AnalogPoint(index=11, value=random.uniform(2.0, 5.0), quality=pb.POINT_QUALITY_GOOD),  # THD Corrente B
        pb.AnalogPoint(index=12, value=random.uniform(2.0, 5.0), quality=pb.POINT_QUALITY_GOOD),  # THD Corrente C
        pb.AnalogPoint(index=13, value=random.uniform(0.5, 2.0), quality=pb.POINT_QUALITY_GOOD),  # Deseq. Neg.
        pb.AnalogPoint(index=14, value=random.uniform(0.5, 2.0), quality=pb.POINT_QUALITY_GOOD),  # Deseq. Zero
        pb.AnalogPoint(index=15, value=random.gauss(650.0, 5.0), quality=pb.POINT_QUALITY_GOOD),  # Tensao Link CC
        pb.AnalogPoint(index=16, value=random.gauss(45.0, 3.0), quality=pb.POINT_QUALITY_GOOD),   # Temp. Ponte
        pb.AnalogPoint(index=17, value=1.0, quality=pb.POINT_QUALITY_GOOD),                       # Estado Operacao
        pb.AnalogPoint(index=18, value=2.0, quality=pb.POINT_QUALITY_GOOD),                       # Modo Reativo
        pb.AnalogPoint(index=19, value=0.0, quality=pb.POINT_QUALITY_GOOD),                       # Modo Harmonicos
        pb.AnalogPoint(index=20, value=0.0, quality=pb.POINT_QUALITY_GOOD),                       # Codigo Falta
    ]

    request = pb.UpdateRequest(analogs=analogs, binaries=binaries)
    try:
        resp = stub.UpdatePoints(request)
        log.info("Sent realistic data (11 BI + 21 AI): success=%s", resp.success)
    except grpc.RpcError as e:
        log.warning("UpdatePoints failed: %s", e.code())


def send_custom_analog(stub: pb_grpc.BridgeServiceStub):
    """Prompt for index and value, send a single analog point."""
    print("\nAnalog Input points:")
    for idx, name in sorted(ANALOG_INPUT_NAMES.items()):
        print(f"  {idx:2d}  {name}")
    try:
        index = int(input("Index (0-20): "))
        if index < 0 or index > 20:
            raise ValueError
        value = float(input("Value: "))
    except (ValueError, EOFError):
        print("Invalid input.")
        return

    request = pb.UpdateRequest(
        analogs=[pb.AnalogPoint(index=index, value=value, quality=pb.POINT_QUALITY_GOOD)],
    )
    try:
        resp = stub.UpdatePoints(request)
        log.info("Sent AI[%d] = %.4f: success=%s", index, value, resp.success)
    except grpc.RpcError as e:
        log.warning("UpdatePoints failed: %s", e.code())


def send_custom_binary(stub: pb_grpc.BridgeServiceStub):
    """Prompt for index and value, send a single binary point."""
    print("\nBinary Input points:")
    for idx, name in sorted(BINARY_INPUT_NAMES.items()):
        print(f"  {idx:2d}  {name}")
    try:
        index = int(input("Index (0-10): "))
        if index < 0 or index > 10:
            raise ValueError
        val_str = input("Value (0/1): ").strip()
        value = val_str in ("1", "true", "True")
    except (ValueError, EOFError):
        print("Invalid input.")
        return

    request = pb.UpdateRequest(
        binaries=[pb.BinaryPoint(index=index, value=value, quality=pb.POINT_QUALITY_GOOD)],
    )
    try:
        resp = stub.UpdatePoints(request)
        log.info("Sent BI[%d] = %s: success=%s", index, value, resp.success)
    except grpc.RpcError as e:
        log.warning("UpdatePoints failed: %s", e.code())


def check_status(stub: pb_grpc.BridgeServiceStub):
    """Query and log the outstation status."""
    try:
        resp = stub.GetStatus(pb.StatusRequest())
        state_name = pb.OutstationState.Name(resp.state)
        log.info("Outstation status: %s (last update: %d ms)", state_name, resp.last_update_timestamp_ms)
    except grpc.RpcError as e:
        log.warning("GetStatus failed: %s", e.code())


# ---------------------------------------------------------------------------
# Auto-pilot mode (original mock_client behavior, minus counters)
# ---------------------------------------------------------------------------
def send_updates_auto(stub: pb_grpc.BridgeServiceStub):
    """Send periodic simulated point updates (auto-pilot mode)."""
    cycle = 0
    while not shutdown_event.is_set():
        request = pb.UpdateRequest(
            analogs=[
                pb.AnalogPoint(index=0, value=random.uniform(210, 230), quality=pb.POINT_QUALITY_GOOD),
                pb.AnalogPoint(index=1, value=random.uniform(210, 230), quality=pb.POINT_QUALITY_GOOD),
            ],
            binaries=[
                pb.BinaryPoint(index=0, value=(cycle % 2 == 0), quality=pb.POINT_QUALITY_GOOD),
            ],
        )
        try:
            resp = stub.UpdatePoints(request)
            log.info("Sent update cycle %d: success=%s", cycle, resp.success)
        except grpc.RpcError as e:
            log.warning("UpdatePoints failed: %s", e.code())

        cycle += 1
        shutdown_event.wait(timeout=2.0)


# ---------------------------------------------------------------------------
# Interactive mode
# ---------------------------------------------------------------------------
def interactive_loop(stub: pb_grpc.BridgeServiceStub):
    """Interactive menu for manual control."""
    global auto_respond

    while not shutdown_event.is_set():
        with auto_respond_lock:
            ar_status = "ON" if auto_respond else "OFF"

        print(f"\n=== Python DSP Simulator ===")
        print(f"Commands:")
        print(f"  1  Send realistic point data (all 11 BI + 21 AI)")
        print(f"  2  Send custom analog point")
        print(f"  3  Send custom binary point")
        print(f"  4  Check outstation status")
        print(f"  5  Toggle command auto-respond (currently: {ar_status})")
        print(f"  q  Quit")

        try:
            choice = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            shutdown_event.set()
            break

        if not choice:
            continue

        if choice == "1":
            send_realistic_data(stub)
        elif choice == "2":
            send_custom_analog(stub)
        elif choice == "3":
            send_custom_binary(stub)
        elif choice == "4":
            check_status(stub)
        elif choice == "5":
            with auto_respond_lock:
                auto_respond = not auto_respond
                log.info("Auto-respond toggled to %s", "ON" if auto_respond else "OFF")
        elif choice in ("q", "Q"):
            shutdown_event.set()
            break
        else:
            print("Unknown option.")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="Python DSP Simulator for dnp3-bridge")
    parser.add_argument("--address", default="localhost:50051", help="gRPC server address")
    parser.add_argument("--mode", choices=["interactive", "auto"], default="interactive",
                        help="Run mode: interactive (menu) or auto (periodic updates)")
    args = parser.parse_args()

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    log.info("Connecting to dnp3-bridge at %s (mode=%s)", args.address, args.mode)
    channel = grpc.insecure_channel(args.address)
    stub = pb_grpc.BridgeServiceStub(channel)

    # Check initial status.
    check_status(stub)

    # Start the command listener in a background thread.
    cmd_thread = threading.Thread(target=command_listener, args=(stub,), daemon=True)
    cmd_thread.start()

    try:
        if args.mode == "auto":
            send_updates_auto(stub)
        else:
            interactive_loop(stub)
    except KeyboardInterrupt:
        shutdown_event.set()

    log.info("DSP simulator stopped.")
    channel.close()


if __name__ == "__main__":
    main()
