# Under emscripten the examples build as web pages (name.html + .js/.wasm/.data), and the
# README SVG captures -- a desktop run of each example -- are skipped.
is_wasm = host_machine.system() == 'emscripten'
example_suffix = is_wasm ? 'html' : []
example_shell = is_wasm ? files('web/shell.html') : []
example_link_args = is_wasm ? ['--shell-file', meson.current_source_dir() / 'web' / 'shell.html'] : []

subdir('model_notes')
subdir('widget_gallery')
subdir('perf_lab')
subdir('model_system_monitor')   # /proc on Linux, a simulated machine elsewhere
subdir('model_lab_bench')
subdir('model_synth_rack')       # real audio through SDL3
subdir('model_data_studio')      # SQLite, from WrapDB
subdir('model_task_runner')
if not is_wasm
  subdir('showcase')             # headless SVG capture tools, not apps
endif
