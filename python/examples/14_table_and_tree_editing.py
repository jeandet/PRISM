"""14_table_and_tree_editing.py — editing data in place: a table and a tree.

Demonstrates:
  - a TableSource with set_cell (double-click or F2 edits; False refuses the text), a checkbox
    column (cell_check), an icon before some cells (cell_image), and editable(col)
  - handle.columns: hiding and reordering columns (dragging a header reorders them too)
  - a TreeSource with checked/set_checked (a box per node) and rename (F2 or double-click)
  - handle.selection / handle.selected: Ctrl-click and Shift-click select several nodes

Run:
  PYTHONPATH=builddir/python python3 python/examples/14_table_and_tree_editing.py
"""

import os

import prism

INFO_ICON = os.path.join(os.path.dirname(__file__), "..", "..", "examples", "widget_gallery", "icons", "info.svg")


class Sensors:
    """Name, Watch (a box), Reading, Note (editable)."""

    def __init__(self):
        self.rows = [["pump", True, "3.2", ""], ["valve", False, "0.0", "stuck?"], ["fan", True, "71.5", ""]]

    def column_count(self):
        return 4

    def row_count(self):
        return len(self.rows)

    def header(self, col):
        return ("Name", "Watch", "Reading", "Note")[col]

    def cell_text(self, row, col):
        v = self.rows[row][col]
        return ("true" if v else "false") if col == 1 else v

    def cell_check(self, row, col):
        return self.rows[row][1] if col == 1 else None

    def cell_image(self, row, col):
        return INFO_ICON if col == 2 and float(self.rows[row][2]) > 50 else None   # a hot reading

    def editable(self, col):
        return col in (1, 3)

    def set_cell(self, row, col, text):
        if col == 1:
            self.rows[row][1] = text == "true"
        elif len(text) > 20:
            return False   # refused: the editor stays open, outlined
        else:
            self.rows[row][3] = text
        return True


class Chores:
    """Two rooms with their chores; every chore has a box, every node can be renamed."""

    def __init__(self):
        self.names = {1: "Kitchen", 2: "Garden", 11: "dishes", 12: "floor", 21: "mow", 22: "water"}
        self.children = {1: [11, 12], 2: [21, 22]}
        self.done = {11: True, 12: False, 21: False, 22: True}

    def root_count(self):
        return 2

    def root_at(self, i):
        return i + 1

    def child_count(self, nid):
        return len(self.children.get(nid, []))

    def child_at(self, nid, i):
        return self.children[nid][i]

    def label(self, nid):
        return self.names[nid]

    def has_children(self, nid):
        return nid in self.children

    def checked(self, nid):
        return self.done.get(nid)   # rooms: None, no box

    def set_checked(self, nid, on):
        self.done[nid] = on

    def rename(self, nid, name):
        if not name.strip():
            return False
        self.names[nid] = name
        return True


class Editor(prism.Model):
    sensors = prism.table_field(Sensors())
    chores = prism.tree_field(Chores())
    hide_reading = prism.button("Hide Reading", checked=False)
    status = prism.label("")

    def view(self, vb):
        vb.toolbar(lambda: vb.widget(self.hide_reading))
        vb.table(self.sensors)
        vb.tree(self.chores)
        vb.status_bar(lambda: vb.widget(self.status))


e = Editor()


def show_reading(_clicks):
    e.sensors.columns = [0, 1, 3] if e.hide_reading.checked else []   # []: every column


def describe(ids):
    e.status.value = f"{len(ids)} selected: {ids}" if ids else "Ctrl/Shift-click to select several chores"


e.hide_reading.observe(show_reading)
e.chores.observe_selection(describe)
describe([])
prism.run(e, title="Table and tree editing — Python")
