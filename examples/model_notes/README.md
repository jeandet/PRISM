# model_notes — the text-heavy reference app

A small text editor: a directory tree on the left; on the right a search field, Save/Reload,
the file in a `TextArea`, and a status line (lines, characters, modified, search matches).

```bash
builddir/examples/model_notes/model_notes [directory]
```

It exists to exercise what a dashboard never does: editing, selection, copy/cut/paste,
undo/redo, input-method composition and real text measurement on whole files. All file and
text logic is in `notes_logic.hpp`, tested by `tests/test_notes_logic.cpp`; `model_notes.cpp`
only wires it to widgets.

Known gaps it shows (tracked in `doc/review-2026-11-01.md`): `TextArea` wraps anywhere, not at
word boundaries; there is no menu bar or file dialog yet. (Its first capture also showed buttons
as wide as the search field -- fixed by content-sized widgets.)
