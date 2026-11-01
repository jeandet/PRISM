#!/bin/bash
# Build showcase examples and copy generated SVGs to doc/screenshots/
set -euo pipefail

builddir="${1:-builddir}"

meson compile -C "$builddir"

mkdir -p doc/screenshots
# Showcase snippets and every example app's capture (the README and the web demo page use both).
for svg in "$builddir"/examples/showcase/*.svg "$builddir"/examples/*/*.svg; do
    [ -f "$svg" ] && cp "$svg" doc/screenshots/
done

echo "Screenshots updated in doc/screenshots/"
