model_tree_browser_exe = executable('model_tree_browser', 'model_tree_browser.cpp',
  dependencies : [prism_dep],
  name_suffix : example_suffix,
  link_args : example_link_args,
  link_depends : example_shell,
)

if not is_wasm
  custom_target('svg_model_tree_browser',
    output : 'model_tree_browser.svg',
    command : [model_tree_browser_exe, '@OUTPUT@', meson.project_source_root() / 'examples'],
    depends : model_tree_browser_exe,
    build_by_default : true,
  )
endif
