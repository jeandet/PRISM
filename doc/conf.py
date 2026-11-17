# Sphinx configuration for the Read the Docs manual: doc/index.md and the pages it lists, written
# in Markdown and read by MyST. The design notes (doc/design/) stay on GitHub. No C++ is built here.
import pathlib
import re

project = "PRISM"
author = "Alexis Jeandet"
copyright = "2026, Alexis Jeandet"

extensions = ["myst_parser", "sphinx_design", "sphinx_copybutton", "sphinxcontrib.mermaid"]
myst_enable_extensions = ["html_image", "colon_fence", "deflist"]
myst_heading_anchors = 3
myst_fence_as_directive = ["mermaid"]

source_suffix = {".md": "markdown"}
root_doc = "index"
exclude_patterns = ["_build", "review-*.md", "handover-*.md", "design", "superpowers"]
# Pygments cannot lex C++26 annotations ([[=core::range<...>]]): the block is still shown, uncolored.
suppress_warnings = ["myst.header", "misc.highlighting_failure"]

html_theme = "furo"
html_title = "PRISM"
html_logo = "logo/logo.png"
html_theme_options = {
    "source_repository": "https://github.com/jeandet/PRISM/",
    "source_branch": "main",
    "source_directory": "doc/",
}

# Pages link to code with paths that work on GitHub (../../include/...). Outside doc/ Sphinx
# has nothing to serve, so those links go to the file on GitHub instead.
REPO = pathlib.Path(__file__).resolve().parent.parent
DOC = REPO / "doc"
GITHUB = "https://github.com/jeandet/PRISM/blob/main/"
GITHUB_ONLY = ["review-*.md", "handover-*.md", "design/*.md"]  # in doc/, left out of the site
LINK = re.compile(r'(\]\(|src=")([^)"#\s]+)')


def _to_github(app, docname, source):
    here = (DOC / docname).parent

    def swap(m):
        target = m.group(2)
        if "://" in target or target.startswith(("mailto:", "/")):
            return m.group(0)
        path = (here / target).resolve()
        served = path.is_relative_to(DOC) and not any(path.match(p) for p in GITHUB_ONLY)
        if served or not path.is_relative_to(REPO):
            return m.group(0)
        return m.group(1) + GITHUB + path.relative_to(REPO).as_posix()

    source[0] = LINK.sub(swap, source[0])


def setup(app):
    app.connect("source-read", _to_github)
