#!/usr/bin/env python3
"""
Mock Python client for dnp3-bridge.

Simulates the Python side of the BeagleBone gateway:
  - Sends periodic point updates to the C++ outstation (Python → SCADA)
  - Listens for incoming SCADA commands via StreamCommands (SCADA → Python)
  - Responds to each command with SUCCESS after a short simulated delay

Usage:
    python3 mock_client.py [--address localhost:50051]
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
log = logging.getLogger("mock_client")

# Global shutdown event.
shutdown_event = threading.Event()


def handle_signal(signum, _frame):
    log.info("Signal %d received, shutting down...", signum)
    shutdown_event.set()


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


def command_listener(stub: pb_grpc.BridgeServiceStub):
    """Listen for SCADA commands on the StreamCommands stream and respond."""
    while not shutdown_event.is_set():
        try:
            log.info("Opening StreamCommands stream...")
            stream = stub.StreamCommands(pb.StreamCommandsRequest())

            for cmd in stream:
                cmd_type = COMMAND_TYPE_NAMES.get(cmd.command_type, "Unknown")
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
                        "  CROB: op=%s count=%d on=%dms off=%dms",
                        op_name,
                        crob.count,
                        crob.on_time_ms,
                        crob.off_time_ms,
                    )
                elif cmd.HasField("analog_value"):
                    log.info("  Analog value: %f", cmd.analog_value)

                # Simulate processing delay (e.g. Modbus round-trip to DSP).
                time.sleep(0.1)

                # Respond with SUCCESS.
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

        except grpc.RpcError as e:
            if shutdown_event.is_set():
                break
            log.warning("StreamCommands disconnected: %s. Reconnecting in 2s...", e.code())
            time.sleep(2)


def send_updates(stub: pb_grpc.BridgeServiceStub):
    """Send periodic simulated point updates."""
    cycle = 0
    while not shutdown_event.is_set():
        # Simulate sensor readings from the DSP via Modbus.
        request = pb.UpdateRequest(
            analogs=[
                pb.AnalogPoint(index=0, value=random.uniform(0, 100), quality=pb.POINT_QUALITY_GOOD),
                pb.AnalogPoint(index=1, value=random.uniform(200, 300), quality=pb.POINT_QUALITY_GOOD),
            ],
            binaries=[
                pb.BinaryPoint(index=0, value=(cycle % 2 == 0), quality=pb.POINT_QUALITY_GOOD),
            ],
            counters=[
                pb.CounterPoint(index=0, value=cycle, quality=pb.POINT_QUALITY_GOOD),
            ],
        )

        try:
            resp = stub.UpdatePoints(request)
            log.info(
                "Sent update cycle %d: success=%s",
                cycle,
                resp.success,
            )
        except grpc.RpcError as e:
            log.warning("UpdatePoints failed: %s", e.code())

        cycle += 1
        # Wait 2 seconds between updates (configurable for real use).
        shutdown_event.wait(timeout=2.0)


def check_status(stub: pb_grpc.BridgeServiceStub):
    """Query and log the outstation status."""
    try:
        resp = stub.GetStatus(pb.StatusRequest())
        state_name = pb.OutstationState.Name(resp.state)
        log.info("Outstation status: %s (last update: %d ms)", state_name, resp.last_update_timestamp_ms)
    except grpc.RpcError as e:
        log.warning("GetStatus failed: %s", e.code())


def main():
    parser = argparse.ArgumentParser(description="Mock Python client for dnp3-bridge")
    parser.add_argument("--address", default="localhost:50051", help="gRPC server address")
    args = parser.parse_args()

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    log.info("Connecting to dnp3-bridge at %s", args.address)
    channel = grpc.insecure_channel(args.address)
    stub = pb_grpc.BridgeServiceStub(channel)

    # Check initial status.
    check_status(stub)

    # Start the command listener in a background thread.
    cmd_thread = threading.Thread(target=command_listener, args=(stub,), daemon=True)
    cmd_thread.start()

    # Send updates on the main thread.
    try:
        send_updates(stub)
    except KeyboardInterrupt:
        shutdown_event.set()

    log.info("Mock client stopped.")
    channel.close()


if __name__ == "__main__":
    main()
