"""13_pickers.py — color, date and time-of-day pickers, and a selectable list.

Demonstrates:
  - prism.color("#RRGGBB"): a swatch; a click opens a hue bar and a saturation/brightness square
  - prism.date("YYYY-MM-DD"): a month calendar
  - prism.time_of_day("HH:MM"): click the hour or the minutes, then Up/Down, the wheel or digits
  - vb.list(h, selectable=True): items.selected follows clicks and the arrow keys
  - one derived summary over all of them

Run:
  PYTHONPATH=builddir/python python3 python/examples/13_pickers.py
"""

import prism


class Booking(prism.Model):
    rooms = prism.list_field(["Atlas", "Borealis", "Cosmos"])
    day = prism.date("2026-11-14")
    start = prism.time_of_day("09:30")
    badge = prism.color("#7AA2F7")
    summary = prism.label("")

    def view(self, vb):
        vb.group("Room", lambda: vb.list(self.rooms, selectable=True))
        vb.group("When", lambda: vb.hstack(self.day, self.start))
        vb.group("Badge", lambda: vb.widget(self.badge))
        vb.status_bar(lambda: vb.widget(self.summary))


b = Booking()


def describe(*_):
    row = b.rooms.selected
    room = b.rooms.get(row) if row is not None else "no room"
    b.summary.value = f"{room} on {b.day.value} at {b.start.value}, badge {b.badge.value}"


for handle in (b.day, b.start, b.badge):
    handle.observe(describe)
b.rooms.observe_selected(describe)
b.rooms.selected = 0
describe()
prism.run(b, title="Pickers — Python")
