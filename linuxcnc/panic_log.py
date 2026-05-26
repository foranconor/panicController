#!/usr/bin/env python3
"""
Interactive panic log viewer.
    pip install textual
    python3 panic_log.py [path/to/events.jsonl]
"""

import json
import os
import sys
import time

from rich.markup import escape as markup_escape
from rich.text import Text
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import ScrollableContainer
from textual.widgets import DataTable, Footer, Header, Static

LOG_PATH = "/home/conor/linuxcnc/panic_events.jsonl"

LEVEL_STYLE = {
    "PANIC":      "bold red",
    "CLEARED":    "bold green",
    "CONNECT":    "bold cyan",
    "DISCONNECT": "bold yellow",
}


class PanicLogApp(App):

    CSS = """
    DataTable {
        height: 1fr;
    }
    #detail_scroll {
        height: 16;
        border-top: solid $accent;
        background: $surface;
    }
    #detail {
        padding: 1 2;
        height: auto;
    }
    """

    TITLE = "Panic Log"

    BINDINGS = [
        Binding("q", "quit", "Quit"),
        Binding("end", "jump_to_latest", "Latest"),
    ]

    def __init__(self, log_path: str = LOG_PATH):
        super().__init__()
        self.log_path = log_path
        self._events: list[dict] = []
        self._file_pos = 0

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)
        yield DataTable(id="table", cursor_type="row", zebra_stripes=True)
        with ScrollableContainer(id="detail_scroll"):
            yield Static("Select a row to see details.", id="detail")
        yield Footer()

    def on_mount(self) -> None:
        table = self.query_one(DataTable)
        table.add_column("Time",   width=10, key="ts")
        table.add_column("Level",  width=12, key="level")
        table.add_column("Source", width=22, key="src")
        table.add_column("Title",             key="title")
        self._load_file()
        self.set_interval(0.5, self._poll)

    def _load_file(self) -> None:
        if not os.path.exists(self.log_path):
            return
        with open(self.log_path) as f:
            for line in f:
                self._ingest(line)
            self._file_pos = f.tell()

    def _poll(self) -> None:
        if not os.path.exists(self.log_path):
            return
        try:
            with open(self.log_path) as f:
                f.seek(self._file_pos)
                chunk = f.read()
                self._file_pos = f.tell()
        except OSError:
            return
        for line in chunk.splitlines():
            self._ingest(line)

    def _ingest(self, raw: str) -> None:
        raw = raw.strip()
        if not raw:
            return
        try:
            ev = json.loads(raw)
        except json.JSONDecodeError:
            return

        idx = len(self._events)
        self._events.append(ev)

        level = ev.get("level", "")
        style = LEVEL_STYLE.get(level, "")
        src   = ev.get("src", "-")
        title = ev.get("title", "")
        ts    = ev.get("ts", "")[-8:]  # HH:MM:SS

        table = self.query_one(DataTable)
        at_bottom = table.row_count == 0 or table.cursor_row == table.row_count - 1

        table.add_row(
            ts,
            Text(f"{level:<11}", style=style),
            src,
            Text(title, style=style if level == "PANIC" else ""),
            key=str(idx),
        )

        if at_bottom:
            table.move_cursor(row=table.row_count - 1, animate=False)

    def on_data_table_row_highlighted(self, event: DataTable.RowHighlighted) -> None:
        key = event.row_key.value
        if key is None:
            return
        idx = int(key)
        if 0 <= idx < len(self._events):
            self._render_detail(self._events[idx])

    def _render_detail(self, ev: dict) -> None:
        level  = ev.get("level", "")
        title  = ev.get("title", "")
        src    = ev.get("src", "")
        ts     = ev.get("ts", "")
        detail = self.query_one("#detail", Static)

        if level == "PANIC":
            explanation = markup_escape(ev.get("explanation", ""))
            fix_lines   = [markup_escape(s.strip()) for s in ev.get("fix", "").split("\n") if s.strip()]
            fix_block   = "\n".join(f"  {line}" for line in fix_lines)
            markup = (
                f"[bold red]{markup_escape(title)}[/bold red]  [dim]{src}  {ts}[/dim]\n\n"
                f"{explanation}\n\n"
                f"[bold]Fix:[/bold]\n{fix_block}"
            )

        elif level == "CLEARED":
            markup = f"[bold green]Cleared:[/bold green] {markup_escape(title)}  [dim]{src}  {ts}[/dim]"

        elif level == "CONNECT":
            markup = f"[bold cyan]Connected:[/bold cyan] {markup_escape(title)}  [dim]{ts}[/dim]"

        elif level == "DISCONNECT":
            markup = f"[bold yellow]Disconnected:[/bold yellow] {markup_escape(title)}  [dim]{ts}[/dim]"

        else:
            markup = markup_escape(title)

        detail.update(markup)

    def action_jump_to_latest(self) -> None:
        table = self.query_one(DataTable)
        if table.row_count:
            table.move_cursor(row=table.row_count - 1, animate=False)


if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else LOG_PATH
    if not os.path.exists(path):
        print(f"Waiting for {path} ...", flush=True)
        while not os.path.exists(path):
            time.sleep(0.5)
    PanicLogApp(log_path=path).run()
