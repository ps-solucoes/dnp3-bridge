#!/usr/bin/env python3
"""
Textual TUI for the Python DSP Simulator.

Provides a rich interactive interface for sending point updates to the
dnp3-bridge and receiving SCADA commands via gRPC streaming.

Usage:
    python3 tui.py [--address localhost:50051]
"""

import asyncio
import argparse
import os
import sys
import time
from datetime import datetime

import grpc

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from generated import dnp3bridge_pb2 as pb
from generated import dnp3bridge_pb2_grpc as pb_grpc
from point_map import (
    ANALOG_INPUT_NAMES,
    ANALOG_OUTPUT_NAMES,
    BINARY_INPUT_NAMES,
    BINARY_OUTPUT_NAMES,
    REALISTIC_BINARY_DEFAULTS,
    generate_realistic_analogs,
)

from textual import work
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, Vertical, VerticalScroll
from textual.message import Message
from textual.reactive import reactive
from textual.screen import ModalScreen
from textual.widgets import (
    Button,
    DataTable,
    Footer,
    Header,
    Input,
    Label,
    RichLog,
    Select,
    Static,
)
from textual.worker import get_current_worker

# ---------------------------------------------------------------------------
# Proto enum name tables (depend on pb, so kept here)
# ---------------------------------------------------------------------------
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
# Custom messages for thread-to-UI communication
# ---------------------------------------------------------------------------
class CommandReceived(Message):
    """Posted from the command listener worker when a SCADA command arrives."""

    def __init__(self, cmd: pb.CommandRequest) -> None:
        super().__init__()
        self.cmd = cmd


class ConnectionStateChanged(Message):
    """Posted when gRPC connection state changes."""

    def __init__(self, connected: bool) -> None:
        super().__init__()
        self.connected = connected


# ---------------------------------------------------------------------------
# Modal screens
# ---------------------------------------------------------------------------
class AnalogInputModal(ModalScreen[tuple[int, float] | None]):
    """Modal for selecting an analog input index and entering a value."""

    CSS = """
    AnalogInputModal {
        align: center middle;
    }
    #analog-modal-container {
        width: 60;
        height: auto;
        max-height: 80%;
        background: $surface;
        border: thick $accent;
        padding: 1 2;
    }
    #analog-modal-container Label {
        margin-bottom: 1;
    }
    #analog-modal-container Select {
        margin-bottom: 1;
    }
    #analog-modal-container Input {
        margin-bottom: 1;
    }
    #analog-modal-buttons {
        height: 3;
        margin-top: 1;
    }
    #analog-modal-buttons Button {
        margin-right: 1;
    }
    """

    BINDINGS = [
        Binding("escape", "cancel", "Cancel"),
    ]

    def compose(self) -> ComposeResult:
        options = [(f"{idx:2d}  {name}", idx) for idx, name in sorted(ANALOG_INPUT_NAMES.items())]
        with Vertical(id="analog-modal-container"):
            yield Label("Send Custom Analog Input")
            yield Select(options, prompt="Select point index", id="analog-select")
            yield Input(placeholder="Enter value (integer)", id="analog-value")
            with Horizontal(id="analog-modal-buttons"):
                yield Button("Send", variant="primary", id="analog-send")
                yield Button("Cancel", variant="default", id="analog-cancel")

    def _try_submit(self) -> None:
        select = self.query_one("#analog-select", Select)
        if select.value is Select.BLANK:
            return
        try:
            value = int(self.query_one("#analog-value", Input).value)
        except ValueError:
            return
        self.dismiss((select.value, value))

    def on_input_submitted(self, event: Input.Submitted) -> None:
        self._try_submit()

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "analog-cancel":
            self.dismiss(None)
        elif event.button.id == "analog-send":
            self._try_submit()

    def action_cancel(self) -> None:
        self.dismiss(None)


class BinaryInputModal(ModalScreen[tuple[int, bool] | None]):
    """Modal for selecting a binary input index and toggling its value."""

    CSS = """
    BinaryInputModal {
        align: center middle;
    }
    #binary-modal-container {
        width: 60;
        height: auto;
        max-height: 80%;
        background: $surface;
        border: thick $accent;
        padding: 1 2;
    }
    #binary-modal-container Label {
        margin-bottom: 1;
    }
    #binary-modal-container Select {
        margin-bottom: 1;
    }
    #binary-modal-buttons {
        height: 3;
        margin-top: 1;
    }
    #binary-modal-buttons Button {
        margin-right: 1;
    }
    """

    BINDINGS = [
        Binding("escape", "cancel", "Cancel"),
    ]

    def compose(self) -> ComposeResult:
        index_options = [(f"{idx:2d}  {name}", idx) for idx, name in sorted(BINARY_INPUT_NAMES.items())]
        value_options = [("0 (False)", 0), ("1 (True)", 1)]
        with Vertical(id="binary-modal-container"):
            yield Label("Send Custom Binary Input")
            yield Select(index_options, prompt="Select point index", id="binary-select")
            yield Select(value_options, prompt="Select value", id="binary-value")
            with Horizontal(id="binary-modal-buttons"):
                yield Button("Send", variant="primary", id="binary-send")
                yield Button("Cancel", variant="default", id="binary-cancel")

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "binary-cancel":
            self.dismiss(None)
            return
        if event.button.id == "binary-send":
            idx_select = self.query_one("#binary-select", Select)
            val_select = self.query_one("#binary-value", Select)
            if idx_select.value is Select.BLANK or val_select.value is Select.BLANK:
                return
            self.dismiss((idx_select.value, bool(val_select.value)))

    def action_cancel(self) -> None:
        self.dismiss(None)


# ---------------------------------------------------------------------------
# Sent timestamp tracking
# ---------------------------------------------------------------------------
def _sent_marker(ts: float | None) -> str:
    """Return a sent marker based on time since last send."""
    if ts is None:
        return " "
    elapsed = time.monotonic() - ts
    if elapsed < 10.0:
        return "[green]*[/]"
    return "[dim].[/]"


# ---------------------------------------------------------------------------
# Main TUI App
# ---------------------------------------------------------------------------
class DspSimApp(App):
    """Textual TUI for the Python DSP Simulator."""

    TITLE = "DSP Simulator"
    SUB_TITLE = "dnp3-bridge gRPC client"

    CSS = """
    #header-bar {
        height: 1;
        dock: top;
        background: $accent;
        color: $text;
        padding: 0 1;
    }
    #main-area {
        height: 1fr;
    }
    #left-panel {
        width: 1fr;
        min-width: 40;
        border-right: solid $accent;
        padding: 0 1;
    }
    #center-panel {
        width: 1fr;
        min-width: 35;
        border-right: solid $accent;
        padding: 0 1;
    }
    #right-panel {
        width: 30;
        padding: 0 1;
    }
    #log-panel {
        height: 10;
        dock: bottom;
        border-top: solid $accent;
    }
    #log-panel RichLog {
        height: 100%;
    }
    .section-title {
        text-style: bold;
        margin-top: 1;
        margin-bottom: 0;
    }
    .point-line {
        height: 1;
    }
    #cmd-table {
        height: 1fr;
    }
    #cmd-detail {
        height: auto;
        max-height: 6;
        margin-top: 1;
        border-top: dashed $accent;
        padding-top: 1;
    }
    #actions-area {
        height: auto;
    }
    #stats-area {
        margin-top: 1;
        border-top: dashed $accent;
        padding-top: 1;
    }
    .action-label {
        margin: 0;
    }
    """

    BINDINGS = [
        Binding("f1", "send_realistic", "Send All", show=True),
        Binding("r", "send_realistic", "Send All", show=False),
        Binding("f2", "custom_analog", "Analog", show=True),
        Binding("a", "custom_analog", "Analog", show=False),
        Binding("f3", "custom_binary", "Binary", show=True),
        Binding("b", "custom_binary", "Binary", show=False),
        Binding("f4", "check_status", "Status", show=True),
        Binding("s", "check_status", "Status", show=False),
        Binding("f5", "toggle_auto_respond", "Auto-rsp", show=True),
        Binding("t", "toggle_auto_respond", "Auto-rsp", show=False),
        Binding("p", "toggle_auto_pilot", "Auto-pilot", show=True),
        Binding("q", "quit", "Quit", show=True),
    ]

    # Reactive state
    connected: reactive[bool] = reactive(False)
    outstation_state: reactive[str] = reactive("UNKNOWN")
    auto_respond: reactive[bool] = reactive(True)
    auto_pilot: reactive[bool] = reactive(False)
    cmds_received: reactive[int] = reactive(0)
    updates_sent: reactive[int] = reactive(0)

    def __init__(self, address: str = "localhost:50051") -> None:
        super().__init__()
        self.address = address
        self.channel: grpc.Channel | None = None
        self.stub: pb_grpc.BridgeServiceStub | None = None
        # Track sent timestamps: index -> monotonic time
        self.binary_sent_ts: dict[int, float] = {}
        self.analog_sent_ts: dict[int, float] = {}
        # Track current point values
        self.binary_values: dict[int, bool | None] = {i: None for i in BINARY_INPUT_NAMES}
        self.analog_values: dict[int, float | None] = {i: None for i in ANALOG_INPUT_NAMES}
        # Command history
        self.last_cmd_detail: str = ""

    def compose(self) -> ComposeResult:
        yield Header()
        yield Static(self._header_text(), id="header-bar")
        with Horizontal(id="main-area"):
            with VerticalScroll(id="left-panel"):
                yield Static("[b]Binary Inputs (11)[/b]", classes="section-title")
                for idx in sorted(BINARY_INPUT_NAMES):
                    yield Static(self._binary_line(idx), id=f"bi-{idx}", classes="point-line")
                yield Static("[b]Analog Inputs (21)[/b]", classes="section-title")
                for idx in sorted(ANALOG_INPUT_NAMES):
                    yield Static(self._analog_line(idx), id=f"ai-{idx}", classes="point-line")
            with Vertical(id="center-panel"):
                yield Static("[b]Incoming Commands[/b]", classes="section-title")
                yield DataTable(id="cmd-table")
                yield Static("", id="cmd-detail")
            with Vertical(id="right-panel"):
                yield Static("[b]Actions[/b]", classes="section-title")
                with Vertical(id="actions-area"):
                    yield Static("[bold][F1/R][/] Send realistic data", classes="action-label")
                    yield Static("[bold][F2/A][/] Custom analog", classes="action-label")
                    yield Static("[bold][F3/B][/] Custom binary", classes="action-label")
                    yield Static("[bold][F4/S][/] Check status", classes="action-label")
                    yield Static("", id="auto-respond-label", classes="action-label")
                    yield Static("", id="auto-pilot-label", classes="action-label")
                with Vertical(id="stats-area"):
                    yield Static("", id="stats-cmds")
                    yield Static("", id="stats-updates")
        with Vertical(id="log-panel"):
            yield RichLog(highlight=True, markup=True, auto_scroll=True, id="event-log")
        yield Footer()

    def on_mount(self) -> None:
        table = self.query_one("#cmd-table", DataTable)
        table.add_columns("ID", "Time", "Type", "Point", "Response")
        table.cursor_type = "row"

        self._refresh_auto_respond_label()
        self._refresh_auto_pilot_label()
        self._refresh_stats()

        self._connect_grpc()
        self._start_command_listener()
        self._start_clock_ticker()
        self._start_marker_refresher()

    def _connect_grpc(self) -> None:
        """Create gRPC channel and stub."""
        try:
            self.channel = grpc.insecure_channel(self.address)
            self.stub = pb_grpc.BridgeServiceStub(self.channel)
            self.connected = True
            self._log("[green][CON][/] Connected to gRPC at " + self.address)
        except Exception as e:
            self.connected = False
            self._log(f"[red][ERR][/] Failed to connect: {e}")

    def _log(self, message: str) -> None:
        """Write a timestamped message to the event log."""
        try:
            log = self.query_one("#event-log", RichLog)
            ts = datetime.now().strftime("%H:%M:%S")
            log.write(f"[dim]{ts}[/] {message}")
        except Exception:
            pass

    # -----------------------------------------------------------------------
    # Header
    # -----------------------------------------------------------------------
    def _header_text(self) -> str:
        conn = "[green]CONNECTED[/]" if self.connected else "[red]DISCONNECTED[/]"
        state = self.outstation_state
        now = datetime.now().strftime("%H:%M:%S")
        return f" gRPC: {self.address}  |  {conn}  |  Outstation: {state}  |  {now}"

    def _refresh_header(self) -> None:
        try:
            self.query_one("#header-bar", Static).update(self._header_text())
        except Exception:
            pass

    def _start_clock_ticker(self) -> None:
        """Refresh the header clock every second."""
        self.set_interval(1.0, self._refresh_header)

    def _start_marker_refresher(self) -> None:
        """Refresh point displays every 2s to update sent markers."""
        self.set_interval(2.0, self._refresh_all_points)

    # -----------------------------------------------------------------------
    # Point display helpers
    # -----------------------------------------------------------------------
    def _binary_line(self, idx: int) -> str:
        name = BINARY_INPUT_NAMES[idx]
        val = self.binary_values.get(idx)
        marker = _sent_marker(self.binary_sent_ts.get(idx))
        if val is None:
            val_str = "[dim]---[/]"
        elif val:
            val_str = "[green]  1[/]"
        else:
            val_str = "[dim]  0[/]"
        return f" {idx:2d}  {name:<30s} {val_str} {marker}"

    def _analog_line(self, idx: int) -> str:
        name = ANALOG_INPUT_NAMES[idx]
        val = self.analog_values.get(idx)
        marker = _sent_marker(self.analog_sent_ts.get(idx))
        if val is None:
            val_str = "[dim]-------[/]"
        else:
            val_str = f"{val:>10.2f}"
        return f" {idx:2d}  {name:<28s} {val_str} {marker}"

    def _refresh_point(self, point_type: str, idx: int) -> None:
        try:
            if point_type == "bi":
                widget = self.query_one(f"#bi-{idx}", Static)
                widget.update(self._binary_line(idx))
            else:
                widget = self.query_one(f"#ai-{idx}", Static)
                widget.update(self._analog_line(idx))
        except Exception:
            pass

    def _refresh_all_points(self) -> None:
        for idx in BINARY_INPUT_NAMES:
            self._refresh_point("bi", idx)
        for idx in ANALOG_INPUT_NAMES:
            self._refresh_point("ai", idx)

    # -----------------------------------------------------------------------
    # Reactive watchers
    # -----------------------------------------------------------------------
    def watch_connected(self, value: bool) -> None:
        self._refresh_header()

    def watch_outstation_state(self, value: str) -> None:
        self._refresh_header()

    def watch_auto_respond(self, value: bool) -> None:
        self._refresh_auto_respond_label()

    def watch_auto_pilot(self, value: bool) -> None:
        self._refresh_auto_pilot_label()

    def watch_cmds_received(self, value: int) -> None:
        self._refresh_stats()

    def watch_updates_sent(self, value: int) -> None:
        self._refresh_stats()

    def _refresh_auto_respond_label(self) -> None:
        try:
            status = "[green]ON[/]" if self.auto_respond else "[red]OFF[/]"
            self.query_one("#auto-respond-label", Static).update(
                f"[bold][F5/T][/] Auto-respond: {status}"
            )
        except Exception:
            pass

    def _refresh_auto_pilot_label(self) -> None:
        try:
            status = "[green]ON[/]" if self.auto_pilot else "[red]OFF[/]"
            self.query_one("#auto-pilot-label", Static).update(
                f"[bold][ P ][/] Auto-pilot:   {status}"
            )
        except Exception:
            pass

    def _refresh_stats(self) -> None:
        try:
            self.query_one("#stats-cmds", Static).update(f"Cmds recv'd: {self.cmds_received}")
            self.query_one("#stats-updates", Static).update(f"Updates sent: {self.updates_sent}")
        except Exception:
            pass

    # -----------------------------------------------------------------------
    # gRPC operations (run in threads to avoid blocking UI)
    # -----------------------------------------------------------------------
    async def _send_update(self, request: pb.UpdateRequest) -> bool:
        """Send an UpdatePoints RPC in a background thread."""
        if not self.stub:
            self._log("[red][ERR][/] Not connected")
            return False
        try:
            resp = await asyncio.to_thread(self.stub.UpdatePoints, request)
            self.updates_sent += 1
            return resp.success
        except grpc.RpcError as e:
            self.connected = False
            self._log(f"[red][ERR][/] UpdatePoints failed: {e.code()}")
            return False

    async def _get_status(self) -> pb.StatusResponse | None:
        """Call GetStatus RPC in a background thread."""
        if not self.stub:
            self._log("[red][ERR][/] Not connected")
            return None
        try:
            resp = await asyncio.to_thread(self.stub.GetStatus, pb.StatusRequest())
            return resp
        except grpc.RpcError as e:
            self.connected = False
            self._log(f"[red][ERR][/] GetStatus failed: {e.code()}")
            return None

    async def _respond_to_command(self, command_id: int) -> bool:
        """Send RespondToCommand RPC in a background thread."""
        if not self.stub:
            return False
        try:
            resp = await asyncio.to_thread(
                self.stub.RespondToCommand,
                pb.CommandResponse(
                    command_id=command_id,
                    status=pb.COMMAND_RESULT_SUCCESS,
                ),
            )
            return resp.success
        except grpc.RpcError as e:
            self._log(f"[red][ERR][/] RespondToCommand failed: {e.code()}")
            return False

    # -----------------------------------------------------------------------
    # Command listener worker (background thread)
    # -----------------------------------------------------------------------
    @work(thread=True, exclusive=True, group="cmd_listener")
    def _start_command_listener(self) -> None:
        """Blocking loop that listens for SCADA commands via StreamCommands."""
        worker = get_current_worker()
        while not worker.is_cancelled:
            if not self.stub:
                time.sleep(2)
                continue
            try:
                self.call_from_thread(self._post_connection_state, True)
                stream = self.stub.StreamCommands(pb.StreamCommandsRequest())
                for cmd in stream:
                    if worker.is_cancelled:
                        break
                    self.call_from_thread(self._handle_incoming_command, cmd)
            except grpc.RpcError:
                if worker.is_cancelled:
                    break
                self.call_from_thread(self._post_connection_state, False)
                time.sleep(2)

    def _post_connection_state(self, connected: bool) -> None:
        self.connected = connected
        if connected:
            self._log("[green][CON][/] StreamCommands connected")
        else:
            self._log("[yellow][CON][/] StreamCommands disconnected, reconnecting...")

    def _handle_incoming_command(self, cmd: pb.CommandRequest) -> None:
        """Process an incoming command on the main thread."""
        self.cmds_received += 1

        cmd_type = COMMAND_TYPE_NAMES.get(cmd.command_type, "Unknown")
        point_name = ""
        detail_lines = []

        if cmd.command_type == pb.COMMAND_TYPE_CROB:
            point_name = BINARY_OUTPUT_NAMES.get(cmd.point_index, f"BO[{cmd.point_index}]")
            if cmd.HasField("crob"):
                op_name = CROB_OP_NAMES.get(cmd.crob.operation, "?")
                detail_lines = [
                    f"Command #{cmd.command_id}: CROB",
                    f"  Point: {cmd.point_index} - {point_name}",
                    f"  Operation: {op_name}",
                    f"  Count: {cmd.crob.count}",
                    f"  On: {cmd.crob.on_time_ms}ms  Off: {cmd.crob.off_time_ms}ms",
                ]
        else:
            point_name = ANALOG_OUTPUT_NAMES.get(cmd.point_index, f"AO[{cmd.point_index}]")
            analog_val = cmd.analog_value if cmd.HasField("analog_value") else 0.0
            detail_lines = [
                f"Command #{cmd.command_id}: {cmd_type}",
                f"  Point: {cmd.point_index} - {point_name}",
                f"  Value: {analog_val}",
            ]

        self._log(f"[cyan][CMD][/] #{cmd.command_id} {cmd_type} pt={cmd.point_index} ({point_name})")

        self.last_cmd_detail = "\n".join(detail_lines)
        try:
            self.query_one("#cmd-detail", Static).update(self.last_cmd_detail)
        except Exception:
            pass

        now = datetime.now().strftime("%H:%M:%S")
        table = self.query_one("#cmd-table", DataTable)
        row_key = table.add_row(
            str(cmd.command_id),
            now,
            cmd_type,
            point_name[:20],
            "[yellow]PENDING[/]",
        )

        if self.auto_respond:
            self.set_timer(0.1, lambda: self._do_auto_respond(cmd.command_id, row_key))
        else:
            self._log(
                f"[yellow][CMD][/] Auto-respond OFF - command #{cmd.command_id} will timeout"
            )

    @work
    async def _do_auto_respond(self, command_id: int, row_key) -> None:
        """Respond to a command and update the table row."""
        success = await self._respond_to_command(command_id)
        try:
            table = self.query_one("#cmd-table", DataTable)
            if success:
                self._log(f"[green][RSP][/] Responded SUCCESS to #{command_id}")
                table.update_cell(row_key, "Response", "[green]SUCCESS[/]")
            else:
                self._log(f"[red][RSP][/] Response failed for #{command_id}")
                table.update_cell(row_key, "Response", "[red]FAILED[/]")
        except Exception:
            pass

    # -----------------------------------------------------------------------
    # Actions
    # -----------------------------------------------------------------------
    async def action_send_realistic(self) -> None:
        """Send all 11 binary + 21 analog points with realistic values."""
        binaries = []
        for idx, val in REALISTIC_BINARY_DEFAULTS.items():
            binaries.append(pb.BinaryPoint(index=idx, value=val, quality=pb.POINT_QUALITY_GOOD))
            self.binary_values[idx] = val

        analogs = []
        for idx, val in generate_realistic_analogs():
            analogs.append(pb.AnalogPoint(index=idx, value=val, quality=pb.POINT_QUALITY_GOOD))
            self.analog_values[idx] = val

        now = time.monotonic()
        for idx in BINARY_INPUT_NAMES:
            self.binary_sent_ts[idx] = now
        for idx in ANALOG_INPUT_NAMES:
            self.analog_sent_ts[idx] = now

        request = pb.UpdateRequest(analogs=analogs, binaries=binaries)
        success = await self._send_update(request)
        self._refresh_all_points()
        self._log(f"[dim][UPD][/] Sent realistic data (11 BI + 21 AI): success={success}")

    def action_custom_analog(self) -> None:
        """Show modal to send a custom analog point."""
        self.push_screen(AnalogInputModal(), callback=self._on_analog_modal_dismiss)

    def _on_analog_modal_dismiss(self, result: tuple[int, float] | None) -> None:
        if result is None:
            return
        idx, value = result
        self.analog_values[idx] = value
        self.analog_sent_ts[idx] = time.monotonic()

        request = pb.UpdateRequest(
            analogs=[pb.AnalogPoint(index=idx, value=value, quality=pb.POINT_QUALITY_GOOD)],
        )
        self._send_update_bg(request, "ai", idx)

    def action_custom_binary(self) -> None:
        """Show modal to send a custom binary point."""
        self.push_screen(BinaryInputModal(), callback=self._on_binary_modal_dismiss)

    def _on_binary_modal_dismiss(self, result: tuple[int, bool] | None) -> None:
        if result is None:
            return
        idx, value = result
        self.binary_values[idx] = value
        self.binary_sent_ts[idx] = time.monotonic()

        request = pb.UpdateRequest(
            binaries=[pb.BinaryPoint(index=idx, value=value, quality=pb.POINT_QUALITY_GOOD)],
        )
        self._send_update_bg(request, "bi", idx)

    @work
    async def _send_update_bg(self, request: pb.UpdateRequest, point_type: str, idx: int) -> None:
        """Send an update in a worker and refresh the UI."""
        success = await self._send_update(request)
        self._refresh_point(point_type, idx)
        if point_type == "bi":
            name = BINARY_INPUT_NAMES.get(idx, "?")
            val = self.binary_values.get(idx)
            self._log(f"[dim][UPD][/] Sent BI[{idx}] ({name}) = {val}: success={success}")
        else:
            name = ANALOG_INPUT_NAMES.get(idx, "?")
            val = self.analog_values.get(idx)
            val_str = f"{val:.4f}" if val is not None else "N/A"
            self._log(f"[dim][UPD][/] Sent AI[{idx}] ({name}) = {val_str}: success={success}")

    async def action_check_status(self) -> None:
        """Query outstation status."""
        resp = await self._get_status()
        if resp:
            state_name = pb.OutstationState.Name(resp.state)
            self.outstation_state = state_name.replace("OUTSTATION_STATE_", "")
            self.connected = True
            self._log(
                f"[dim][UPD][/] Status: {self.outstation_state} "
                f"(last update: {resp.last_update_timestamp_ms} ms)"
            )
        self._refresh_header()

    def action_toggle_auto_respond(self) -> None:
        """Toggle auto-respond for incoming commands."""
        self.auto_respond = not self.auto_respond
        status = "ON" if self.auto_respond else "OFF"
        self._log(f"[cyan][CFG][/] Auto-respond toggled to {status}")

    def action_toggle_auto_pilot(self) -> None:
        """Toggle auto-pilot mode."""
        self.auto_pilot = not self.auto_pilot
        status = "ON" if self.auto_pilot else "OFF"
        self._log(f"[cyan][CFG][/] Auto-pilot toggled to {status}")
        if self.auto_pilot:
            self._run_auto_pilot()

    @work(exclusive=True, group="auto_pilot")
    async def _run_auto_pilot(self) -> None:
        """Send realistic data every 2 seconds while auto-pilot is ON."""
        while self.auto_pilot:
            await self.action_send_realistic()
            await asyncio.sleep(2.0)

    # -----------------------------------------------------------------------
    # DataTable cursor for command detail
    # -----------------------------------------------------------------------
    def on_data_table_row_highlighted(self, event: DataTable.RowHighlighted) -> None:
        """Update command detail when a row is highlighted."""
        if event.row_key and event.row_key.value is not None:
            try:
                table = self.query_one("#cmd-table", DataTable)
                row = table.get_row(event.row_key)
                cmd_id = row[0]
                detail = self.query_one("#cmd-detail", Static)
                detail.update(
                    f"Command #{cmd_id}\n"
                    f"  Time: {row[1]}\n"
                    f"  Type: {row[2]}\n"
                    f"  Point: {row[3]}\n"
                    f"  Response: {row[4]}"
                )
            except Exception:
                pass

    # -----------------------------------------------------------------------
    # Cleanup
    # -----------------------------------------------------------------------
    def on_unmount(self) -> None:
        if self.channel:
            try:
                self.channel.close()
            except Exception:
                pass


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Python DSP Simulator TUI")
    parser.add_argument(
        "--address",
        default="localhost:50051",
        help="gRPC server address (default: localhost:50051)",
    )
    args = parser.parse_args()
    app = DspSimApp(address=args.address)
    app.run()
