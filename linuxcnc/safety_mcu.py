#!/usr/bin/env python3
"""
LinuxCNC HAL userspace component for the panic controller.

Reads USB CDC serial from the ESP32, sets HAL pins for machine control,
logs structured telemetry to Postgres via db.py, and bridges the LinuxCNC
software estop state to the physical safety controller.

Load in your HAL config:
    loadusr -W python3 /path/to/safety_mcu.py

Wire the estop pin:
    net safety-estop  safety-mcu.estop-ok  =>  iocontrol.0.emc-enable-in

HAL pins exported:
    safety-mcu.estop-ok      BIT  OUT  — machine may run (ESP32 in OK state)
    safety-mcu.connected     BIT  OUT  — serial link is up
    safety-mcu.loop-avg-us   FLOAT OUT — ESP32 loop average work time (us)
    safety-mcu.loop-max-us   FLOAT OUT — ESP32 loop max work time (us)
    safety-mcu.panic-count   S32  OUT  — total panics since ESP32 boot
    safety-mcu.glitch-count  S32  OUT  — glitches in last heartbeat period

Estop flow:
    ESP32 physical ack button pressed
        → ESP32 goes OK → estop-ok goes high
        → this component commands STATE_ESTOP_RESET + STATE_ON
        → operator turns to AXIS and presses cycle start, nothing else needed

    Soft estop pressed in AXIS (or any LinuxCNC fault)
        → this component detects STATE_ESTOP → sends ESTOP to ESP32
        → ESP32 panics → estop-ok goes low → emc-enable-in goes low
        → wire is double-tripped from both sides
"""

import csv
import io
import logging
import queue
import threading
import time

import hal
import linuxcnc
import serial

from db import TelemetryDB

# --- configuration --------------------------------------------------------

DEVICE              = "/dev/ttyACM0"
BAUD                = 115200
HEARTBEAT_TIMEOUT_S = 5.0
RECONNECT_DELAY_S   = 1.0

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s: %(message)s",
)
log = logging.getLogger("safety_mcu")

# --- HAL component --------------------------------------------------------

h = hal.component("safety-mcu")
h.newpin("estop-ok",     hal.HAL_BIT,   hal.HAL_OUT)
h.newpin("connected",    hal.HAL_BIT,   hal.HAL_OUT)
h.newpin("loop-avg-us",  hal.HAL_FLOAT, hal.HAL_OUT)
h.newpin("loop-max-us",  hal.HAL_FLOAT, hal.HAL_OUT)
h.newpin("panic-count",  hal.HAL_S32,   hal.HAL_OUT)
h.newpin("glitch-count", hal.HAL_S32,   hal.HAL_OUT)
h.ready()

# --- shared state (written by reader thread, read by main thread) ---------

_lock           = threading.Lock()
_last_message_t = 0.0
_estop_ok       = False
_send_queue     = queue.SimpleQueue()  # outgoing commands to the ESP32

def _set_estop(ok):
    global _estop_ok
    with _lock:
        _estop_ok = ok


# --- message parser -------------------------------------------------------

def _parse(line, db):
    line = line.strip()
    if not line:
        return

    if line == "OK":
        _set_estop(True)
        return
    if line in ("HB", "PANIC"):
        if line == "PANIC":
            _set_estop(False)
        return

    try:
        parts = next(csv.reader(io.StringIO(line)))
    except Exception:
        log.warning("malformed line: %r", line)
        return

    msg = parts[0]

    try:
        if msg == "STAT" and len(parts) == 9:
            _, uptime_s, safety, usb, avg, mx, overruns, glitches, panics = parts
            h["loop-avg-us"]  = float(avg)
            h["loop-max-us"]  = float(mx)
            h["panic-count"]  = int(panics)
            h["glitch-count"] = int(glitches)
            db.log_heartbeat(uptime_s, safety, usb, avg, mx, overruns, glitches, panics)

        elif msg == "PANIC_INFO" and len(parts) == 8:
            _, uptime_s, from_state, src_type, src_id, title, explanation, fix = parts
            db.log_panic(uptime_s, from_state, src_type, src_id, title, explanation, fix)

        elif msg == "TRANS" and len(parts) == 4:
            _, uptime_s, from_state, to_state = parts
            db.log_transition(uptime_s, from_state, to_state)

        elif msg == "EVENT" and len(parts) == 4:
            _, uptime_s, source, description = parts
            db.log_event(uptime_s, source, description)

    except Exception as exc:
        log.error("parse error on %r: %s", line, exc)


# --- serial reader/writer thread ------------------------------------------

def _reader(db):
    global _last_message_t
    while True:
        try:
            with serial.Serial(DEVICE, BAUD, timeout=0.1) as port:
                log.info("serial open: %s", DEVICE)
                h["connected"] = True
                while True:
                    line = port.readline().decode("ascii", errors="replace")
                    if line:
                        with _lock:
                            _last_message_t = time.monotonic()
                        _parse(line, db)
                    # Drain outgoing commands (ESTOP etc.) on every iteration
                    while not _send_queue.empty():
                        port.write(_send_queue.get_nowait().encode())
        except serial.SerialException as exc:
            log.warning("serial error: %s — retrying in %ss", exc, RECONNECT_DELAY_S)
            h["connected"] = False
            _set_estop(False)
            time.sleep(RECONNECT_DELAY_S)


# --- main loop ------------------------------------------------------------

def main():
    db  = TelemetryDB()
    lc  = linuxcnc.stat()
    cmd = linuxcnc.command()

    t = threading.Thread(target=_reader, args=(db,), daemon=True, name="serial-reader")
    t.start()

    log.info("safety_mcu running — timeout %.1fs", HEARTBEAT_TIMEOUT_S)

    prev_estop_ok = False
    prev_lc_state = None

    try:
        while True:
            now = time.monotonic()
            with _lock:
                timed_out = (now - _last_message_t) > HEARTBEAT_TIMEOUT_S
                ok = _estop_ok and not timed_out

            h["estop-ok"] = ok

            if timed_out and h["connected"]:
                log.warning("heartbeat timeout — asserting estop")

            # --- LinuxCNC state bridge ------------------------------------

            try:
                lc.poll()
                lc_state = lc.task_state

                # LinuxCNC entered estop (soft button, fault, etc.)
                # Send ESTOP to ESP32 so it double-trips the hardware wire.
                if lc_state == linuxcnc.STATE_ESTOP and prev_lc_state != linuxcnc.STATE_ESTOP:
                    log.info("LinuxCNC estop — sending ESTOP to controller")
                    _send_queue.put("ESTOP\n")

                # estop-ok rising edge — physical ack button was just pressed.
                # Auto-clear the LinuxCNC estop and enable the machine so the
                # operator only needs to press cycle start.
                if ok and not prev_estop_ok:
                    if lc_state == linuxcnc.STATE_ESTOP:
                        log.info("controller cleared — restoring LinuxCNC machine state")
                        cmd.state(linuxcnc.STATE_ESTOP_RESET)
                        time.sleep(0.05)
                        cmd.state(linuxcnc.STATE_ON)

                prev_lc_state = lc_state

            except Exception as exc:
                log.warning("linuxcnc status error: %s", exc)

            prev_estop_ok = ok

            time.sleep(0.05)

    except KeyboardInterrupt:
        pass
    finally:
        db.close()
        log.info("safety_mcu stopped")


if __name__ == "__main__":
    main()
