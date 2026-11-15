"""17_task_runner.py — an IDE-style build console: tasks, output, parsed progress.

Demonstrates:
  - prism.channel(): a run's thread streams the output of a real subprocess (or a
    generated one) to the logic thread, chunk by chunk, losing nothing
  - prism.list_field(): the console itself, grown by the drain, not rebuilt per frame
  - prism.progress(): fed from the [ 42%] and [3/120] tags in the output, not from the
    run (except while it is still unknown: then the bar crawls)
  - prism.choice() + a text field for the filter: 20 000 lines re-filtered on a keystroke
  - stopping a run: SIGTERM, then SIGKILL, and the exit code a shell reports for a signal

Run:
  PYTHONPATH=builddir/python python3 python/examples/17_task_runner.py
  PYTHONPATH=builddir/python python3 python/examples/17_task_runner.py --headless
"""

import collections
import os
import re
import shutil
import subprocess
import sys
import threading
import time

import prism

# --- the pure part -------------------------------------------------------------------------
# The same rules as examples/model_task_runner/runner_model.hpp, small enough to read in one
# screen here. Nothing below knows about threads, processes or the UI.

CSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
CSI_OPEN = re.compile(
    r"\x1b\[[0-?]*[ -/]*"
)  # an escape sequence a truncated read left open
OSC = re.compile(r"\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)")


def strip_ansi(text):
    """Escape sequences and control bytes out: what a console should show."""
    text = CSI_OPEN.sub("", OSC.sub("", CSI.sub("", text)))
    return "".join(ch for ch in text if ch == "\t" or ch >= " ")


class LineAssembler:
    """Cuts arbitrary chunks into lines, keeping a split line for the next chunk."""

    def __init__(self):
        self.pending = ""

    def feed(self, chunk):
        self.pending += chunk
        *lines, self.pending = self.pending.split("\n")
        return [line[:-1] if line.endswith("\r") else line for line in lines]

    def finish(self):
        """The line the writer left unterminated when the stream ended."""
        tail, self.pending = self.pending, ""
        return [tail[:-1] if tail.endswith("\r") else tail] if tail else []


PROGRESS = re.compile(r"\[\s*(\d+)\s*(?:%|/\s*(\d+))\s*\]")


def parse_progress(line):
    """The first bracket in `line` that reads as progress, as the fraction done."""
    for match in PROGRESS.finditer(line):
        done, total = float(match.group(1)), match.group(2)
        if total is None:
            return min(1.0, max(0.0, done / 100.0))
        if float(total) > 0.0:
            return min(1.0, max(0.0, done / float(total)))
    return None


def classify(line):
    """error, warning, success, command or info -- first match wins, as in the C++ model."""
    low = line.lower()
    if "error" in low or "failed" in low or "failure" in low:
        return "error"
    if "warning" in low or "warn:" in low:
        return "warning"
    if (
        "passed" in low
        or "ok " in low
        or "finished" in low
        or "succeeded" in low
        or "done" in low
    ):
        return "success"
    if line.startswith("$"):
        return "command"
    return "info"


def keeps(text, kind, needle, errors_only):
    """Whether a line passes the console's filter."""
    if errors_only and kind not in ("error", "warning"):
        return False
    return needle.lower() in text.lower() if needle else True


DIRS = ("core", "render", "ui", "delegates", "app", "widgets", "backends")
FILES = (
    "field",
    "draw_list",
    "layout",
    "button",
    "widget_tree",
    "table",
    "sdl_backend",
    "shared",
)
SUITES = (
    "Storage",
    "Index",
    "Parser",
    "Network",
    "Scheduler",
    "Geometry",
    "Renderer",
    "Catalog",
)
CASES = (
    "keeps insertion order",
    "rolls back a partial write",
    "reopens after a truncation",
    "rejects a short header",
    "splits a large batch",
    "keeps ties stable",
)

BUILD_UNITS = 120
TESTS_LINES = 97
FLOOD_LINES = 200_000
SIMULATED = {
    "Build": (BUILD_UNITS + 1, 10.0),
    "Unit tests": (TESTS_LINES, 8.1),
    "Flood": (FLOOD_LINES, 0.0),
}
REAL = {"ping": "ping -c 5 127.0.0.1", "ls": "ls -la /usr/bin", "uname": "uname -a"}
COMMANDS = {
    "Build": "ninja -C builddir",
    "Unit tests": "ctest --output-on-failure",
    "Flood": "seq 200000",
}

HAVE_SHELL = (
    os.name == "posix"
    and sys.platform != "emscripten"
    and shutil.which("/bin/sh") is not None
)
OPTIONS = list(SIMULATED) + (list(REAL) if HAVE_SHELL else [])

# A control line in the output stream: the run is over, and this is how it went. It goes down
# the same channel as the text, so it cannot overtake the run's last lines.
END_MARKER = "\x00exit "


def simulated_line(name, index, failing=False):
    """Line `index` of a generated run: the same seed, the same lines, every time."""
    if name == "Build":
        if index >= BUILD_UNITS:
            return (
                "ninja: build stopped: subcommand failed."
                if failing
                else "Build finished: 120/120 in 10.0 s"
            )
        if failing and index == BUILD_UNITS - 1:
            return "src/net/socket.cpp:88:9: error: no matching function for call to 'connect'"
        source = f"src/{DIRS[index % len(DIRS)]}/{FILES[index % len(FILES)]}.cpp"
        if index % 23 == 11:
            return f"{source}:42:13: warning: unused parameter 'count' [-Wunused-parameter]"
        return f"[{index + 1:3d}/{BUILD_UNITS}] Compiling {source}"
    if name == "Unit tests":
        suite = SUITES[(index // 12) % len(SUITES)]
        if index >= TESTS_LINES - 1:
            return (
                "[100%] 510 passed, 1 failed in 8.1 s"
                if failing
                else "[100%] 511 passed in 8.1 s"
            )
        if index % 12 == 0:
            return f"[{index * 100 // TESTS_LINES:3d}%] Running {suite}Test (48 tests)"
        case = CASES[index % len(CASES)]
        if failing and index == 61:
            return f"  FAILED {suite} / {case}"
        return f"  ok {suite} / {case}"
    return (
        f"[{index + 1:>7}/{FLOOD_LINES}] stage {index % 9}:"
        f" compacting batch {index // 500} of {FLOOD_LINES // 500}, 1200 records/s"
    )


def grouped(value):
    return f"{value:,}".replace(",", " ")


# --- the app -------------------------------------------------------------------------------

MAX_LINES = 20_000  # what the Python console keeps of a run; see the README
BATCH_LINES = 512


class TaskRunner(prism.Model):
    task = prism.choice(OPTIONS, default="Build")
    run_btn = prism.button("Run", shortcut="Ctrl+Enter")
    stop_btn = prism.button("Stop")
    filter = prism.field("")
    errors_only = prism.checkbox(False, label="Errors and warnings")
    progress = prism.progress(0.0, min=0.0, max=1.0, label="not run yet")
    stats = prism.label("no output yet")
    console = prism.list_field([])
    status = prism.label("idle    0 runs")
    outbox = prism.channel("")

    def __init__(self):
        super().__init__()
        self.assembler = LineAssembler()
        self.history = collections.deque(
            maxlen=MAX_LINES
        )  # (text, kind), the run so far
        self.shown = []  # what the console holds now
        self.line_count = 0
        self.warnings = 0
        self.errors = 0
        self.progress_value = 0.0
        self.saw_progress = False
        self.running = False
        self.runs = 0
        self.exit_code = None
        self.started = time.monotonic()
        self.elapsed = 0.0
        self.stop_event = threading.Event()
        self.thread = None

    def view(self, vb):
        vb.vstack(
            lambda: (
                vb.group(
                    "Task", lambda: vb.vstack(self.task, self.run_btn, self.stop_btn)
                ),
                vb.group(
                    "Console",
                    lambda: (
                        vb.hstack(self.filter, self.errors_only),
                        vb.widget(self.progress),
                        vb.widget(self.stats),
                        vb.list(self.console, selectable=True, follow_tail=True),
                    ),
                ),
                vb.status_bar(lambda: vb.widget(self.status)),
            )
        )

    # -- running ------------------------------------------------------------------------

    @prism.on_change(run_btn)
    def start_selected(self):
        self.start(self.task.value)

    @prism.on_change(stop_btn)
    def stop_selected(self):
        self.stop()

    def start(self, name):
        self.stop()
        command = COMMANDS.get(name) or REAL.get(name, "")
        self.assembler = LineAssembler()
        self.history.clear()
        self.shown = []
        self.console.replace_all([])
        self.line_count = self.warnings = self.errors = 0
        self.progress_value, self.saw_progress = 0.0, False
        self.exit_code = None
        self.elapsed = 0.0
        self.started = time.monotonic()
        self.runs += 1
        # Chosen here, on the logic thread: a run's thread reads nothing the model owns.
        failing = (name == "Build" and self.runs % 3 == 2) or (
            name == "Unit tests" and self.runs % 4 == 3
        )
        self.running = True
        self.progress.value = 0.0
        self.progress.busy = True
        self.append(f"$ {command}    (run {self.runs})")
        self.sync()
        self.stop_event = threading.Event()
        self.thread = threading.Thread(
            target=self.run_on_worker,
            args=(name, command, failing, self.stop_event),
            daemon=True,
        )
        self.thread.start()

    def seed(self):
        """A finished build in the console, without a thread: what the capture frame shows."""
        self.runs += 1
        self.started = time.monotonic() - 10.0
        self.running = True
        self.append(f"$ {COMMANDS['Build']}    (run {self.runs})")
        for index in range(BUILD_UNITS + 1):
            self.append(simulated_line("Build", index))
        self.finish(0)

    def stop(self):
        """Asks the run to end and waits for it: a stopped run's thread owns its process."""
        if self.thread is None or not self.thread.is_alive():
            return
        self.stop_event.set()
        self.thread.join(timeout=2.0)
        self.thread = None

    def run_on_worker(self, name, command, failing, stop):
        """On a thread of its own: hands chunks over and never touches a Field."""
        if name in SIMULATED:
            self.run_generated(name, failing, stop)
        elif not HAVE_SHELL:
            self.outbox.send(
                "\na browser has no shell: this task exists on the desktop\n"
            )
            self.outbox.send(f"{END_MARKER}127\n")
        else:
            self.run_process(command, stop)

    def run_generated(self, name, failing, stop):
        total, duration = SIMULATED[name]
        started = time.monotonic()
        batch = []
        for index in range(total):
            if stop.is_set():
                self.outbox.send(f"{END_MARKER}143\n")
                return
            if duration:
                due = started + duration * (index + 1) / total
                while time.monotonic() < due and not stop.is_set():
                    time.sleep(0.004)
            batch.append(simulated_line(name, index, failing))
            if len(batch) >= BATCH_LINES or duration:
                self.outbox.send("\n".join(batch) + "\n")
                batch.clear()
        if batch:
            self.outbox.send("\n".join(batch) + "\n")
        self.outbox.send(f"{END_MARKER}{1 if failing else 0}\n")

    def run_process(self, command, stop):
        try:
            process = subprocess.Popen(
                ["/bin/sh", "-c", command],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                bufsize=0,
            )
        except OSError as exc:
            self.outbox.send(f"cannot run /bin/sh: {exc}\n{END_MARKER}127\n")
            return
        while not stop.is_set():
            chunk = process.stdout.read(4096)
            if not chunk:
                break
            self.outbox.send(chunk.decode("utf-8", "replace"))
        if stop.is_set():
            process.terminate()  # SIGTERM
            try:
                process.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                process.kill()  # SIGKILL
        self.outbox.send(f"{END_MARKER}{process.wait()}\n")

    # -- the drain ----------------------------------------------------------------------

    def ingest(self, chunk):
        """On the logic thread, once per batch: the only place the console grows."""
        for raw in self.assembler.feed(chunk):
            if raw.startswith(END_MARKER):
                self.finish(int(raw[len(END_MARKER) :]))
                continue
            self.append(strip_ansi(raw))
        self.sync()

    def append(self, text):
        kind = classify(text)
        self.history.append((text, kind))
        self.line_count += 1
        if kind == "error":
            self.errors += 1
        elif kind == "warning":
            self.warnings += 1
        done = parse_progress(text)
        if done is not None:
            self.progress_value, self.saw_progress = done, True
        if keeps(text, kind, self.filter.value, self.errors_only.value):
            self.show(text)

    def show(self, text):
        self.shown.append(text)
        self.console.push(text)
        if len(self.shown) > MAX_LINES:
            self.shown = self.shown[-MAX_LINES:]
            self.console.replace_all(self.shown)

    def sync(self):
        seconds = self.elapsed_of()
        rate = self.line_count / seconds if seconds > 0.0 else 0.0
        self.progress.value = self.progress_value
        self.progress.busy = self.running and not self.saw_progress
        parts = [f"{grouped(self.line_count)} lines"]
        if self.warnings or self.errors:
            parts.append(f"{self.warnings} warnings, {self.errors} errors")
        if len(self.shown) < self.line_count:
            parts.append(f"{grouped(len(self.shown))} shown")
        parts.append(f"{rate:.0f} lines/s")
        parts.append(f"{seconds:.1f} s")
        self.stats.value = "    ".join(parts)
        self.status.value = (
            f"{'1 running' if self.running else 'idle'}    {self.runs} runs"
        )

    def finish(self, code):
        self.elapsed = self.elapsed_of()
        self.running = False
        self.exit_code = code
        self.progress.busy = False
        self.progress.value = (
            self.progress_value if self.saw_progress else (1.0 if code == 0 else 0.0)
        )
        self.append(
            f"exit {code}    {grouped(self.line_count)} lines in {self.elapsed:.1f} s"
        )
        self.sync()

    def elapsed_of(self):
        return self.elapsed if not self.running else time.monotonic() - self.started

    @prism.on_change(filter, errors_only)
    def refilter(self):
        needle = self.filter.value
        errors_only = self.errors_only.value
        self.shown = [
            text
            for text, kind in self.history
            if keeps(text, kind, needle, errors_only)
        ]
        self.console.replace_all(self.shown)
        self.sync()


def main():
    app = TaskRunner()
    app.outbox.observe(app.ingest)

    if "--headless" in sys.argv:
        app.seed()
        prism.run(app, headless_seconds=0.4)
        print("task runner headless verification OK")
    else:
        app.start("Build")  # the window opens with a build already running
        prism.run(app, title="PRISM Task Runner - Python")


if __name__ == "__main__":
    main()
