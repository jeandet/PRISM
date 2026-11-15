"""18_data_studio.py — a SQL studio over a year of generated taxi trips.

Demonstrates:
  - a worker thread that builds an in-memory SQLite database while the window stays live
  - one request slot: the newest query wins, and the one it replaced is abandoned, not queued
  - Cancel calling sqlite3_interrupt(), so a runaway query stops within ~100 ms
  - a prism.table_field result set filled in from the worker: the table re-reads its width, so a
    new result just replaces the rows and the headers
  - a chart that picks its own columns, and cells formatted like a real database client

Run:
  PYTHONPATH=builddir/python python3 python/examples/18_data_studio.py
  PYTHONPATH=builddir/python python3 python/examples/18_data_studio.py --headless
"""

from __future__ import annotations

import random
import sqlite3
import sys
import threading
import time
from datetime import datetime, timedelta

import prism

ROWS = 50_000
HEADLESS_ROWS = 5_000
MAX_ROWS = 20_000
LOAD_CHUNK = 5_000

MINUTES_IN_YEAR = 365 * 24 * 60
YEAR_START = datetime(2025, 1, 1)

# name, borough, relative frequency — Manhattan-heavy, like the real thing.
ZONES = [
    ("Midtown", "Manhattan", 16),
    ("Upper East Side", "Manhattan", 11),
    ("Upper West Side", "Manhattan", 9),
    ("Chelsea", "Manhattan", 8),
    ("East Village", "Manhattan", 7),
    ("Williamsburg", "Brooklyn", 8),
    ("Park Slope", "Brooklyn", 6),
    ("Bushwick", "Brooklyn", 5),
    ("Astoria", "Queens", 7),
    ("Flushing", "Queens", 5),
    ("Long Island City", "Queens", 4),
    ("Harlem", "Manhattan", 5),
    ("Fordham", "Bronx", 4),
    ("Riverdale", "Bronx", 2),
    ("St George", "Staten Island", 2),
    ("Jamaica", "Queens", 3),
]

PAYMENTS = ("card", "cash", "app", "account")
# Tips are recorded on the card, the app and account rides; cash tips are not in the system.
PAYMENT_TIP = (0.18, 0.0, 0.21, 0.14)

# Trips per hour of the day: a morning peak, an evening peak, and a quiet 4 a.m.
HOUR_WEIGHT = (
    38,
    30,
    24,
    18,
    16,
    22,
    40,
    78,
    96,
    66,
    52,
    56,
    62,
    60,
    64,
    78,
    94,
    88,
    70,
    62,
    58,
    54,
    46,
    40,
)

SCHEMA_SQL = """
CREATE TABLE zones (id INTEGER PRIMARY KEY, name TEXT NOT NULL, borough TEXT NOT NULL);
CREATE TABLE trips (
    id INTEGER PRIMARY KEY,
    pickup TEXT NOT NULL,
    pickup_minute INTEGER NOT NULL,
    zone_id INTEGER NOT NULL,
    km REAL NOT NULL,
    minutes REAL NOT NULL,
    fare REAL NOT NULL,
    tip REAL NOT NULL,
    passengers INTEGER NOT NULL,
    payment TEXT NOT NULL
);
"""

# name, sql, note. The last one is deliberately slow: no index can help a self-join on a
# full scan, and it walks the trips table once per matching row.
SAVED_QUERIES = [
    (
        "Busiest hours",
        "SELECT strftime('%H:00', pickup) AS hour, COUNT(*) AS trips\n"
        "FROM trips GROUP BY hour ORDER BY hour",
        "the shape of a year of rides, one bar per hour of the day",
    ),
    (
        "Tips by payment",
        "SELECT payment,\n"
        "       COUNT(*) AS trips,\n"
        "       ROUND(AVG(tip), 2) AS avg_tip,\n"
        "       ROUND(100.0 * SUM(tip) / SUM(fare), 1) AS tip_pct\n"
        "FROM trips GROUP BY payment ORDER BY tip_pct DESC",
        "cash tips are never recorded, so card and app rides look generous",
    ),
    (
        "Fare per km by borough",
        "SELECT z.borough,\n"
        "       ROUND(AVG(t.fare / t.km), 2) AS fare_per_km,\n"
        "       COUNT(*) AS trips\n"
        "FROM trips t JOIN zones z ON z.id = t.zone_id\n"
        "GROUP BY z.borough ORDER BY fare_per_km DESC",
        "short trips at low speed cost more per km, so the busiest boroughs top this",
    ),
    (
        "Distance vs duration",
        "SELECT ROUND(km) AS km, ROUND(AVG(minutes), 1) AS minutes, COUNT(*) AS trips\n"
        "FROM trips WHERE km < 25 GROUP BY ROUND(km) ORDER BY km",
        "two numbers: the chart draws the line, the table keeps the detail",
    ),
    (
        "Slowest zones",
        "SELECT z.name, z.borough, ROUND(AVG(t.minutes), 1) AS minutes, COUNT(*) AS trips\n"
        "FROM trips t JOIN zones z ON z.id = t.zone_id\n"
        "GROUP BY z.name, z.borough ORDER BY minutes DESC LIMIT 12",
        "the twelve slowest zones, out of every trip",
    ),
    (
        "Passengers per trip",
        "SELECT passengers, COUNT(*) AS trips, ROUND(AVG(fare), 2) AS avg_fare\n"
        "FROM trips GROUP BY passengers ORDER BY passengers",
        "most rides are solo, and a full car pays very little more",
    ),
    (
        "Rush-hour fare premium (slow)",
        "SELECT a.pickup, b.pickup, a.fare, b.fare, z.name\n"
        "FROM trips a\n"
        "JOIN trips b ON b.zone_id = a.zone_id\n"
        "            AND (b.pickup_minute % 1440) BETWEEN 420 AND 600\n"
        "JOIN zones z ON z.id = a.zone_id\n"
        "WHERE a.fare > 40 LIMIT 700",
        "deliberately slow: a self-join with no index to help it",
    ),
]


def rush_hour(hour):
    return 7 <= hour <= 9 or 16 <= hour <= 19


def pick_pickup_minute(rng):
    """Rejection-sample the hour of the day, so the year keeps HOUR_WEIGHT's shape."""
    total = sum(HOUR_WEIGHT)
    while True:
        minute = rng.randrange(MINUTES_IN_YEAR)
        if rng.random() * total < HOUR_WEIGHT[(minute // 60) % 24]:
            return minute


def pick_zone(rng):
    roll = rng.random() * sum(weight for _, _, weight in ZONES)
    for index, (_, _, weight) in enumerate(ZONES):
        roll -= weight
        if roll <= 0:
            return index
    return len(ZONES) - 1


def pickup_text(minute):
    return (YEAR_START + timedelta(minutes=minute)).strftime("%Y-%m-%d %H:%M:00")


def make_trip(rng):
    pickup = pick_pickup_minute(rng)
    hour = (pickup // 60) % 24
    zone = pick_zone(rng)
    payment = rng.randrange(len(PAYMENTS))
    distance = min(38.0, max(0.4, rng.lognormvariate(0.9, 0.55)))
    speed = 11.0 + 4.0 * rng.random() if rush_hour(hour) else 18.0 + 10.0 * rng.random()
    minutes = distance / speed * 60.0 + 1.5 + 2.5 * rng.random()
    fare = round(3.0 + 2.6 * distance + 0.42 * minutes, 2)
    tip = (
        round(fare * PAYMENT_TIP[payment] * (0.6 + 0.8 * rng.random()), 2)
        if PAYMENT_TIP[payment]
        else 0.0
    )
    return (
        pickup_text(pickup),
        pickup,
        zone + 1,
        round(distance, 2),
        round(minutes, 1),
        fare,
        min(tip, fare),
        1 + rng.randrange(4),
        PAYMENTS[payment],
    )


def make_trips(count, seed=0x5150):
    """A whole year of rides from one seed: same seed, same table, on every machine."""
    rng = random.Random(seed)
    return [make_trip(rng) for _ in range(count)]


def thousands(value):
    return f"{value:,}"


def format_real(value):
    """Readable at a glance: 1,234,567 / 12.3 / 4.50 — enough digits to compare, no more."""
    if abs(value) >= 1000:
        return thousands(round(value))
    if abs(value) >= 10:
        return f"{value:.1f}"
    return f"{value:.2f}"


def cell_text(value):
    if value is None:
        return "NULL"
    if isinstance(value, float):
        return format_real(value)
    if isinstance(value, int):
        return thousands(value)
    return str(value)


def elapsed_text(seconds):
    return f"{round(seconds * 1000)} ms" if seconds < 1.0 else f"{seconds:.1f} s"


def done_status(rows, seconds):
    noun = "row" if rows == 1 else "rows"
    return f"{rows:,} {noun} in {elapsed_text(seconds)}"


def running_status(seconds):
    return f"running {seconds:.1f} s..."


def is_numeric(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def mostly_numeric(rows, column):
    samples = [
        row[column] for row in rows if column < len(row) and row[column] is not None
    ]
    if len(samples) < 2:
        return False
    return sum(is_numeric(value) for value in samples) >= 0.9 * len(samples)


def chart_columns(columns, rows):
    """Pick what to chart: the first mostly-numeric column is the value.

    A text column to its left gives the labels; otherwise a second numeric column becomes the
    x axis, and failing that the row index is the x axis. Returns (x, y, categorical) or None.
    """
    if len(rows) < 2 or len(columns) < 2:
        return None
    value = next(
        (column for column in range(len(columns)) if mostly_numeric(rows, column)), None
    )
    if value is None:
        return None
    labels = next(
        (
            column
            for column in range(value)
            if not mostly_numeric(rows, column)
            and len({str(row[column]) for row in rows}) >= 2
        ),
        None,
    )
    if labels is not None:
        return labels, value, True
    numeric = next(
        (
            column
            for column in range(value + 1, len(columns))
            if mostly_numeric(rows, column)
        ),
        None,
    )
    return (numeric, value, False) if numeric is not None else (None, value, True)


class ResultSet:
    """The table's source: a table re-reads column_count() every layout, so a new result simply
    replaces `payload` — one attribute, so a repaint can never see half of a new result."""

    def __init__(self):
        self.payload = ([], [])  # columns, rows

    def column_count(self):
        return len(self.payload[0])

    def row_count(self):
        return len(self.payload[1])

    def header(self, column):
        columns, _ = self.payload
        return columns[column] if 0 <= column < len(columns) else ""

    def cell_text(self, row, column):
        columns, rows = self.payload
        if column >= len(columns) or row >= len(rows):
            return ""
        return cell_text(rows[row][column])


def build_database(trips, progress=None):
    """Create the trips and zones tables in memory; `progress(done)` reports each chunk."""
    db = sqlite3.connect(":memory:", check_same_thread=False)
    db.executescript(SCHEMA_SQL)
    db.executemany(
        "INSERT INTO zones (id, name, borough) VALUES (?, ?, ?)",
        [(index + 1, name, borough) for index, (name, borough, _) in enumerate(ZONES)],
    )
    for start in range(0, len(trips), LOAD_CHUNK):
        chunk = trips[start : start + LOAD_CHUNK]
        db.executemany(
            "INSERT INTO trips (id, pickup, pickup_minute, zone_id, km, minutes, fare, tip, passengers, payment)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            ((start + offset + 1, *trip) for offset, trip in enumerate(chunk)),
        )
        if progress is not None:
            progress(start + len(chunk))
    db.commit()
    return db


def run_query(db, sql, limit=MAX_ROWS):
    """One statement, at most `limit` rows: (columns, rows, truncated, error)."""
    try:
        cursor = db.execute(sql)
        columns = [description[0] for description in cursor.description or ()]
        fetched = cursor.fetchmany(limit + 1)
    except sqlite3.Error as error:
        return [], [], False, str(error)
    return columns, [tuple(row) for row in fetched[:limit]], len(fetched) > limit, ""


class DataStudio(prism.Model):
    source = ResultSet()
    results = prism.table_field(source)
    editor = prism.text_area(SAVED_QUERIES[0][1], rows=9)
    run = prism.button("Run", shortcut="Ctrl+Enter")
    cancel = prism.button("Cancel")
    status = prism.label("starting the database...")
    queries = prism.list_field([name for name, _, _ in SAVED_QUERIES])
    note = prism.label(SAVED_QUERIES[0][2])
    results_tab = prism.tabs(0)
    chart = prism.plot_field()

    # The worker's side of the handoff, all guarded by one lock: the slot holding the newest
    # request, the generation that decides which answers still matter, and the connection
    # Cancel interrupts. A query is not real-time work, so a lock is the honest tool here.
    _lock = threading.Lock()
    _request = None
    _generation = 0
    _db = None
    _cancel = False
    _rows = ROWS

    def view(self, vb):
        vb.toolbar(lambda: vb.hstack(self.run, self.cancel))
        vb.group("Saved queries", lambda: self.saved_queries(vb))
        vb.group("SQL", lambda: vb.widget(self.editor))
        vb.group("Results", lambda: self.results_page(vb))
        vb.status_bar(lambda: vb.widget(self.status))

    def saved_queries(self, vb):
        vb.list(self.queries, selectable=True)
        vb.widget(self.note)

    def results_page(self, vb):
        vb.tabs(
            self.results_tab, [("Table", self.table_page), ("Chart", self.chart_page)]
        )

    def table_page(self, vb):
        vb.table(self.results)

    def chart_page(self, vb):
        vb.canvas(self.chart)

    # --- the logic thread ----------------------------------------------------------------

    def submit(self, sql):
        """Hand a query to the worker; whatever it was running is abandoned, not queued."""
        if not sql.strip():
            self.status.value = "nothing to run"
            return
        with self._lock:
            self._generation += 1
            self._request = (self._generation, sql)
            self._cancel = False
        self.status.value = running_status(0.0)

    def abort(self):
        """Interrupt the statement the worker is inside: SQLite gives up at its next step."""
        with self._lock:
            db, self._cancel = self._db, True
        if db is not None:
            db.interrupt()
        self.status.value = "cancelling..."

    def open_query(self, choice):
        """Load a saved query into the editor; `choice` is the row the list reports."""
        if not isinstance(choice, int) or not 0 <= choice < len(SAVED_QUERIES):
            return
        self.editor.value = SAVED_QUERIES[choice][1]
        self.note.value = SAVED_QUERIES[choice][2]

    def report_load(self, done):
        self.status.value = f"loading {done:,} of {self._rows:,} trips"

    # --- the worker thread ---------------------------------------------------------------

    def take_request(self):
        with self._lock:
            request, self._request = self._request, None
        return request

    def superseded(self, generation):
        with self._lock:
            return generation != self._generation

    def publish(self, generation, columns, rows, truncated, seconds, error):
        """Called on the worker thread; every field write here is posted to the logic thread."""
        if self.superseded(generation):
            return
        if error:
            with self._lock:
                cancelled, self._cancel = self._cancel, False
            elapsed = elapsed_text(seconds)
            self.status.value = (
                f"cancelled after {elapsed}" if cancelled else f"SQL error - {error}"
            )
            return
        self.source.payload = (columns, rows)
        self.results.refresh()
        self.draw_chart(columns, rows)
        text = done_status(len(rows), seconds)
        self.status.value = text + (
            f" (the first {MAX_ROWS:,} rows)" if truncated else ""
        )

    def draw_chart(self, columns, rows):
        """Chart what the result happens to be: numbers over an index, or one number against
        another. Python's plot has no tick formatter, so the categories stay in the table."""
        plan = chart_columns(columns, rows)
        if plan is None:
            return
        x_column, y_column, categorical = plan
        self.chart.set_labels(
            x="" if categorical else columns[x_column], y=columns[y_column]
        )
        if categorical:
            values = [float(row[y_column]) for row in rows if is_numeric(row[y_column])]
            if values:
                self.chart.replace_series(
                    list(range(len(values))), values, color="#7AA2F7"
                )
            return
        pairs = [
            (float(row[x_column]), float(row[y_column]))
            for row in rows
            if is_numeric(row[x_column]) and is_numeric(row[y_column])
        ]
        if pairs:
            self.chart.replace_series(
                [x for x, _ in pairs], [y for _, y in pairs], color="#7AA2F7"
            )


def query_worker(app, stop, rows):
    """Build the database once, then serve the request slot until the app quits."""
    app._rows = rows
    started = time.perf_counter()
    trips = make_trips(rows)
    db = build_database(trips, app.report_load)
    app._db = db
    app.status.value = (
        f"{rows:,} trips ready in {elapsed_text(time.perf_counter() - started)}"
    )
    # The editor already holds this query: show a result as soon as the table is ready.
    app.submit(SAVED_QUERIES[0][1])
    while not stop.wait(0.02):
        request = app.take_request()
        if request is None or app.superseded(request[0]):
            continue
        generation, sql = request
        started = time.perf_counter()
        columns, result_rows, truncated, error = run_query(db, sql)
        app.publish(
            generation,
            columns,
            result_rows,
            truncated,
            time.perf_counter() - started,
            error,
        )


def main():
    app = DataStudio()
    app.run.observe(lambda _clicks: app.submit(app.editor.value))
    app.cancel.observe(lambda _clicks: app.abort())
    app.queries.observe_selected(app.open_query)

    headless = "--headless" in sys.argv
    rows = HEADLESS_ROWS if headless else ROWS
    prism.worker(lambda stop: query_worker(app, stop, rows))

    if headless:
        prism.run(app, headless_seconds=1.0)
        print(f"data studio headless verification OK - {app.status.value}")
    else:
        prism.run(app, title="PRISM Data Studio - Python")


if __name__ == "__main__":
    main()
