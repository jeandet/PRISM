# On wasm there is no disk to browse: bundle PRISM's own sources and start the app inside them.
browse_args = []
browse_depends = []
if is_wasm
  foreach dir : ['include', 'src', 'examples']
    browse_args += ['--preload-file', meson.project_source_root() / dir + '@/PRISM/' + dir]
  endforeach
  browse_args += ['--pre-js', meson.current_source_dir() / 'web_pre.js']
  browse_depends = files('web_pre.js')
endif

model_tree_browser_exe = executable('model_tree_browser', 'model_tree_browser.cpp',
  dependencies : [prism_dep],
  name_suffix : example_suffix,
  link_args : example_link_args + browse_args,
  link_depends : [example_shell, browse_depends],
)

if not is_wasm
  custom_target('svg_model_tree_browser',
    output : 'model_tree_browser.svg',
    command : [model_tree_browser_exe, '@OUTPUT@', meson.project_source_root() / 'examples'],
    depends : model_tree_browser_exe,
    build_by_default : true,
  )
endif
