"""15_docking_and_drag.py — a rearrangeable dock, and chips dragged onto a drop zone.

Demonstrates:
  - prism.dock_layout(prism.dock_split(...)) + vb.dock(handle, [(name, title, fn), ...]): drag a
    panel's tab onto another panel's middle to add it there, onto an edge to split there, or out
    of the window to float it; layout.value is the arrangement as nested dicts
  - vb.widget(h, drag=fn, drop=fn, accepts=str): a chip drags its name, the zone takes it
  - a status label saying what the controls do, and one saying what was dropped

Run:
  PYTHONPATH=builddir/python python3 python/examples/15_docking_and_drag.py
"""

import prism


class Workspace(prism.Model):
    layout = prism.dock_layout(
        prism.dock_split(
            prism.dock_group("notes", "list"),
            prism.dock_group("controls", "log"),
        )
    )
    hint = prism.label(
        "Drag a panel's tab onto another panel's middle or edge to rearrange the "
        "dock; drag a chip onto the drop zone.",
        wrap=True,
    )
    notes = prism.text_area("Type here: the text moves with its panel.", rows=4)
    items = prism.list_field(["Notes", "Table", "Plot"])
    chip_a = prism.button("Alpha")
    chip_b = prism.button("Beta")
    chip_c = prism.button("Gamma")
    drop = prism.label("Drop a chip here")
    wrap = prism.checkbox(True, "Wrap")
    copies = prism.spinbox(2, min=1, max=9)
    log = prism.label("Waiting for a click.")

    def view(self, vb):
        vb.widget(self.hint)
        vb.dock(
            self.layout,
            [
                ("notes", "Notes", lambda p: p.widget(self.notes)),
                ("list", "List", lambda p: p.list(self.items, selectable=True)),
                ("controls", "Controls", self.view_controls),
                ("log", "Log", lambda p: p.widget(self.log)),
            ],
        )

    def view_controls(self, p):
        p.vstack(
            lambda: [
                p.widget(self.chip_a, drag=lambda: "Alpha"),
                p.widget(self.chip_b, drag=lambda: "Beta"),
                p.widget(self.chip_c, drag=lambda: "Gamma"),
            ]
        )
        p.widget(self.drop, drop=self.take, accepts=str)
        p.widget(self.wrap)
        p.widget(self.copies)

    def take(self, value, x, y):
        self.drop.value = f"{value} ({int(x)}, {int(y)})"
        self.log.value = f"Dropped {value}"

    @prism.on_change(copies, wrap, immediate=True)
    def describe(self):
        self.log.value = (
            f"{self.copies.value} copies, wrap {'on' if self.wrap.value else 'off'}"
        )


def main():
    prism.run(Workspace(), title="Docking and drag — Python")


if __name__ == "__main__":
    main()
