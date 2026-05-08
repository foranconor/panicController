#!/usr/bin/env python3
"""
LinuxCNC HAL userspace component for the panic controller.

Reads USB CDC serial from the ESP32, drives the estop-ok HAL pin, and
bridges the LinuxCNC software estop state back to the physical controller.

Load in your HAL config:
    loadusr -W python3 /path/to/panic_controller.py

Wire the estop pin:
    net safety-estop  panic-controller.estop-ok  =>  iocontrol.0.emc-enable-in

HAL pins:
    panic-controller.estop-ok   BIT OUT — HIGH when ESP32 is in OK state
    panic-controller.connected  BIT OUT — HIGH when serial link is up
"""

import csv
import io
import json
import logging
import queue
import threading
import time

import hal
import linuxcnc
import serial

# ---------------------------------------------------------------------------

DEVICE              = "/dev/ttyACM0"
BAUD                = 115200
HEARTBEAT_TIMEOUT_S = 5.0
RECONNECT_DELAY_S   = 1.0
EVENT_LOG           = "/home/conor/linuxcnc/panic_events.jsonl"

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s: %(message)s",
)
log = logging.getLogger("panic_controller")

# ---------------------------------------------------------------------------
# HAL component

h = hal.component("panic-controller")
h.newpin("estop-ok",  hal.HAL_BIT, hal.HAL_OUT)
h.newpin("connected", hal.HAL_BIT, hal.HAL_OUT)
h.ready()

# ---------------------------------------------------------------------------
# Shared state (written by reader thread, read by main thread)

_lock           = threading.Lock()
_last_message_t = 0.0
_estop_ok       = False
_send_queue     = queue.SimpleQueue()


def _set_estop(ok):
    global _estop_ok
    with _lock:
        _estop_ok = ok


def _log_event(level, title, **extra):
    record = {"ts": time.strftime("%Y-%m-%d %H:%M:%S"), "level": level, "title": title}
    record.update(extra)
    try:
        with open(EVENT_LOG, "a") as f:
            f.write(json.dumps(record) + "\n")
    except OSError as exc:
        log.warning("could not write event log: %s", exc)


# ---------------------------------------------------------------------------
# Message parser

def _parse(line):
    line = line.strip()
    if not line:
        return

    if line == "OK":
        _set_estop(True)
        return

    if line == "PANIC":
        _set_estop(False)
        return

    # Parse structured telemetry for logging — no DB, just stdout for now
    try:
        parts = next(csv.reader(io.StringIO(line)))
    except Exception:
        return

    msg = parts[0]

    if msg == "PANIC_INFO" and len(parts) == 8:
        _, uptime, from_state, src_type, src_id, title, explanation, fix = parts
        log.warning("PANIC t=%ss [%s->PANIC] %s/%s: %s", uptime, from_state, src_type, src_id, title)
        _log_event("PANIC", title,
                   src=f"{src_type}/{src_id}",
                   explanation=explanation,
                   fix=fix.replace("\\n", "\n"))

    elif msg == "PANIC_CLEARED" and len(parts) == 5:
        _, uptime, src_type, src_id, title = parts
        log.info("CLEARED t=%ss %s/%s: %s", uptime, src_type, src_id, title)
        _log_event("CLEARED", title, src=f"{src_type}/{src_id}")

    elif msg == "TRANS" and len(parts) == 4:
        _, uptime, from_state, to_state = parts
        log.info("TRANS t=%ss %s -> %s", uptime, from_state, to_state)

    elif msg == "EVENT" and len(parts) == 4:
        _, uptime, source, description = parts
        log.info("EVENT t=%ss [%s] %s", uptime, source, description)

    elif msg == "STAT" and len(parts) == 9:
        _, uptime, safety, usb, avg, mx, overruns, glitches, panics = parts
        log.debug("STAT t=%ss safety=%s usb=%s avg=%sus max=%sus panics=%s",
                  uptime, safety, usb, avg, mx, panics)


# ---------------------------------------------------------------------------
# Serial reader thread

def _reader():
    global _last_message_t
    while True:
        try:
            with serial.Serial(DEVICE, BAUD, timeout=0.1) as port:
                log.info("serial open: %s", DEVICE)
                _log_event("CONNECT", f"serial link up ({DEVICE})", src=DEVICE)
                h["connected"] = True
                with _lock:
                    _last_message_t = time.monotonic()
                while True:
                    line = port.readline().decode("ascii", errors="replace")
                    if line:
                        with _lock:
                            _last_message_t = time.monotonic()
                        _parse(line)
                    while not _send_queue.empty():
                        port.write(_send_queue.get_nowait().encode())
        except serial.SerialException as exc:
            log.warning("serial error: %s — retrying in %ss", exc, RECONNECT_DELAY_S)
            _log_event("DISCONNECT", "serial link lost — retrying", src=DEVICE)
            h["connected"] = False
            _set_estop(False)
            time.sleep(RECONNECT_DELAY_S)


# ---------------------------------------------------------------------------
# Main loop

def main():
    t = threading.Thread(target=_reader, daemon=True, name="serial-reader")
    t.start()

    log.info("safety_mcu running — heartbeat timeout %.1fs", HEARTBEAT_TIMEOUT_S)

    lc  = None
    cmd = None
    prev_estop_ok = False
    prev_lc_state = None

    try:
        while True:
            now = time.monotonic()
            with _lock:
                timed_out = (now - _last_message_t) > HEARTBEAT_TIMEOUT_S
                ok = _estop_ok and not timed_out

            if timed_out and h["connected"]:
                log.warning("heartbeat timeout — asserting estop")
                h["connected"] = False

            h["estop-ok"] = ok

            # Connect to LinuxCNC task lazily — it may not be up yet at startup
            if lc is None:
                try:
                    lc  = linuxcnc.stat()
                    cmd = linuxcnc.command()
                    log.info("connected to LinuxCNC task")
                except Exception:
                    lc  = None
                    cmd = None

            if lc is not None:
                try:
                    lc.poll()
                    lc_state = lc.task_state

                    # LinuxCNC entered estop — tell the controller
                    if (lc_state == linuxcnc.STATE_ESTOP
                            and prev_lc_state != linuxcnc.STATE_ESTOP):
                        log.info("LinuxCNC estop — sending ESTOP to controller")
                        _send_queue.put("ESTOP\n")

                    # estop-ok rising edge — physical ack button was pressed.
                    # Auto-reset LinuxCNC estop so operator only needs cycle start.
                    if ok and not prev_estop_ok:
                        if lc_state == linuxcnc.STATE_ESTOP:
                            log.info("controller cleared — resetting LinuxCNC estop")
                            cmd.state(linuxcnc.STATE_ESTOP_RESET)
                            time.sleep(0.05)
                            cmd.state(linuxcnc.STATE_ON)

                    prev_lc_state = lc_state

                except Exception as exc:
                    log.warning("linuxcnc status error: %s — will reconnect", exc)
                    lc  = None
                    cmd = None

            prev_estop_ok = ok
            time.sleep(0.05)

    except KeyboardInterrupt:
        pass
    finally:
        h["estop-ok"]  = False
        h["connected"] = False
        log.info("safety_mcu stopped")


if __name__ == "__main__":
    main()
