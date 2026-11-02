#!/bin/bash
# Assemble the GitHub Pages site from an emscripten build of the examples.
# Usage: scripts/build_web_site.sh [wasm-builddir] [outdir]
set -euo pipefail

builddir="${1:-builddir-wasm}"
outdir="${2:-_site}"
# page:screenshot (the system monitor's capture keeps its README name)
examples=(widget_gallery:widget_gallery model_notes:model_notes model_system_monitor:system_monitor perf_lab:perf_lab)

coi_version=0.1.7
coi_sha256=03d2f35be3249f5eeb4e214c5daf2be8067e2b24630aaef26a1247b723f2d05d

rm -rf "$outdir"
mkdir -p "$outdir/screenshots"
cp examples/web/index.html "$outdir/"

curl -sfL "https://cdn.jsdelivr.net/npm/coi-serviceworker@${coi_version}/coi-serviceworker.min.js" \
    -o "$outdir/coi-serviceworker.js"
echo "${coi_sha256}  $outdir/coi-serviceworker.js" | sha256sum -c --quiet

for entry in "${examples[@]}"; do
    name="${entry%%:*}" shot="${entry##*:}"
    cp "$builddir/examples/$name/$name".{html,js,wasm,data} "$outdir/"
    cp "doc/screenshots/$shot.svg" "$outdir/screenshots/"
done

echo "Site assembled in $outdir/"
