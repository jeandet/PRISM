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

It has a File/Search menu bar with Alt shortcuts (`&File`), a right-click menu on the editor,
tooltips, the platform's Open and Save As dialogs, drop-a-file-to-open, and a Rename dialog
holding a text field. Reload asks before discarding unsaved edits.

The file tree is `file_tree_source.hpp`, a `TreeStorage` over the filesystem (tier 2 of the tree
adapters), tested by `tests/test_file_tree_source.cpp`. In the browser the build bundles PRISM's
own sources and `web_pre.js` starts the app in that folder, since a page has no disk.
