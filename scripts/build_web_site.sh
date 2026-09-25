#!/bin/bash
# Assemble the GitHub Pages site from an emscripten build of the examples.
# Usage: scripts/build_web_site.sh [wasm-builddir] [outdir]
set -euo pipefail

builddir="${1:-builddir-wasm}"
outdir="${2:-_site}"
examples=(model_dashboard model_plot hello_rect)

coi_version=0.1.7
coi_sha256=03d2f35be3249f5eeb4e214c5daf2be8067e2b24630aaef26a1247b723f2d05d

rm -rf "$outdir"
mkdir -p "$outdir/screenshots"
cp examples/web/index.html "$outdir/"

curl -sfL "https://cdn.jsdelivr.net/npm/coi-serviceworker@${coi_version}/coi-serviceworker.min.js" \
    -o "$outdir/coi-serviceworker.js"
echo "${coi_sha256}  $outdir/coi-serviceworker.js" | sha256sum -c --quiet

for name in "${examples[@]}"; do
    cp "$builddir/examples/$name/$name".{html,js,wasm,data} "$outdir/"
    cp "doc/screenshots/$name.svg" "$outdir/screenshots/"
done

echo "Site assembled in $outdir/"
