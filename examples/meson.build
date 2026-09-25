# Under emscripten the examples build as web pages (name.html + .js/.wasm/.data), and the
# README SVG captures -- a desktop run of each example -- are skipped.
is_wasm = host_machine.system() == 'emscripten'
example_suffix = is_wasm ? 'html' : []

subdir('hello_rect')
subdir('model_dashboard')
subdir('model_plot')
subdir('model_tree_browser')
if not is_wasm
  subdir('model_system_monitor') # reads /proc
  subdir('perf_lab')             # not tried on wasm yet
  subdir('showcase')             # headless SVG capture tools, not apps
endif
