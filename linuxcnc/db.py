"""
Telemetry storage for panic controller — Postgres via psycopg2.

DB writes are queued and executed on a background thread so the HAL
component's serial read / pin update path is never blocked by disk I/O.

Schema uses standard SQL. Create the database once:
    sudo -u postgres createdb safety_mcu

Tables are created automatically on first connection.
"""

import threading
import queue
import datetime
import logging

import psycopg2
import psycopg2.extras

log = logging.getLogger(__name__)

DSN = "dbname=safety_mcu"

SCHEMA = """
    CREATE TABLE IF NOT EXISTS heartbeats (
        id            SERIAL      PRIMARY KEY,
        wall_time     TIMESTAMPTZ NOT NULL,
        uptime_s      INTEGER     NOT NULL,
        safety        TEXT        NOT NULL,
        usb           TEXT        NOT NULL,
        loop_avg_us   INTEGER     NOT NULL,
        loop_max_us   INTEGER     NOT NULL,
        overruns      INTEGER     NOT NULL,
        glitches      INTEGER     NOT NULL,
        total_panics  INTEGER     NOT NULL
    );

    CREATE TABLE IF NOT EXISTS panics (
        id            SERIAL      PRIMARY KEY,
        wall_time     TIMESTAMPTZ NOT NULL,
        uptime_s      INTEGER     NOT NULL,
        from_state    TEXT        NOT NULL,
        source_type   TEXT        NOT NULL,
        source_id     TEXT        NOT NULL,
        title         TEXT        NOT NULL,
        explanation   TEXT        NOT NULL,
        fix           TEXT        NOT NULL
    );

    CREATE TABLE IF NOT EXISTS transitions (
        id            SERIAL      PRIMARY KEY,
        wall_time     TIMESTAMPTZ NOT NULL,
        uptime_s      INTEGER     NOT NULL,
        from_state    TEXT        NOT NULL,
        to_state      TEXT        NOT NULL
    );

    CREATE TABLE IF NOT EXISTS events (
        id            SERIAL      PRIMARY KEY,
        wall_time     TIMESTAMPTZ NOT NULL,
        uptime_s      INTEGER     NOT NULL,
        source        TEXT        NOT NULL,
        description   TEXT        NOT NULL
    );
"""

_SENTINEL = None


class TelemetryDB:
    def __init__(self, dsn=DSN):
        self._queue = queue.Queue()
        self._thread = threading.Thread(target=self._worker, args=(dsn,),
                                        daemon=True, name="telemetry-db")
        self._thread.start()

    # --- public API (called from HAL thread, never blocks) ----------------

    def log_heartbeat(self, uptime_s, safety, usb,
                      loop_avg_us, loop_max_us, overruns, glitches, total_panics):
        self._enqueue(
            """INSERT INTO heartbeats
               (wall_time, uptime_s, safety, usb, loop_avg_us, loop_max_us,
                overruns, glitches, total_panics)
               VALUES (%s,%s,%s,%s,%s,%s,%s,%s,%s)""",
            (_now(), int(uptime_s), safety, usb,
             int(loop_avg_us), int(loop_max_us),
             int(overruns), int(glitches), int(total_panics)),
        )

    def log_panic(self, uptime_s, from_state, source_type, source_id,
                  title, explanation, fix):
        self._enqueue(
            """INSERT INTO panics
               (wall_time, uptime_s, from_state, source_type, source_id,
                title, explanation, fix)
               VALUES (%s,%s,%s,%s,%s,%s,%s,%s)""",
            (_now(), int(uptime_s), from_state, source_type,
             source_id, title, explanation, fix),
        )

    def log_transition(self, uptime_s, from_state, to_state):
        self._enqueue(
            """INSERT INTO transitions
               (wall_time, uptime_s, from_state, to_state)
               VALUES (%s,%s,%s,%s)""",
            (_now(), int(uptime_s), from_state, to_state),
        )

    def log_event(self, uptime_s, source, description):
        self._enqueue(
            """INSERT INTO events
               (wall_time, uptime_s, source, description)
               VALUES (%s,%s,%s,%s)""",
            (_now(), int(uptime_s), source, description),
        )

    def close(self):
        self._queue.put(_SENTINEL)
        self._thread.join(timeout=5)

    # --- internal ---------------------------------------------------------

    def _enqueue(self, sql, params):
        self._queue.put((sql, params))

    def _worker(self, dsn):
        conn = None
        while True:
            item = self._queue.get()
            if item is _SENTINEL:
                break
            sql, params = item
            try:
                if conn is None or conn.closed:
                    conn = psycopg2.connect(dsn)
                    conn.autocommit = False
                    with conn.cursor() as cur:
                        cur.execute(SCHEMA)
                    conn.commit()
                    log.info("telemetry db connected")
                with conn.cursor() as cur:
                    cur.execute(sql, params)
                conn.commit()
            except Exception as exc:
                log.error("db write failed: %s", exc)
                if conn and not conn.closed:
                    conn.rollback()


def _now():
    return datetime.datetime.now(datetime.timezone.utc)
