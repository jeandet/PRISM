"""19_markdown_notes.py — Markdown on the left, its rendering on the right, as you type.

Demonstrates:
  - prism.document(): a text document field, edited in place with undo, however long
  - vb.code_editor(doc, "toml" | "python" | ...): line numbers, colors, Tab to indent
  - vb.text_view(doc): read-only, only the lines in sight laid out
  - doc.observe() + other.set_text(text, markdown=True): one side re-rendered from the other
  - doc.find_all(): the hit count in a status line

Run:
  PYTHONPATH=builddir/python python3 python/examples/19_markdown_notes.py
  PYTHONPATH=builddir/python python3 python/examples/19_markdown_notes.py --headless
"""

import sys

import prism

NOTES = """# Notes

Type **Markdown** here; it is rendered on the right as you type.

- lists, *emphasis*, `code`
- [links](https://github.com/jeandet/PRISM)

```python
print("code blocks keep their language")
```
"""


class Notes(prism.Model):
    source = prism.document(NOTES)
    rendered = prism.document(NOTES, markdown=prism.has_markdown)
    status = prism.field("")

    def __init__(self):
        super().__init__()
        self.source.observe(self.render)
        self.render()

    def render(self):
        text = self.source.text
        self.rendered.set_text(text, markdown=prism.has_markdown)
        words = len(text.split())
        self.status.value = f"{words} words, {len(self.source.find_all('#'))} '#' marks"

    def view(self, vb):
        vb.hstack(lambda: (vb.code_editor(self.source), vb.text_view(self.rendered)))
        vb.widget(self.status)


def main():
    notes = Notes()
    if "--headless" in sys.argv:
        prism.run(notes, headless_seconds=0.5)
        print(notes.status.value)
    else:
        prism.run(notes, title="PRISM Markdown notes")


if __name__ == "__main__":
    main()
