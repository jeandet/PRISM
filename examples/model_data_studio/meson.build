sqlite3_dep = dependency('sqlite3', fallback : ['sqlite3', 'sqlite3_dep'])

model_data_studio_exe = executable('model_data_studio', 'model_data_studio.cpp',
  dependencies : [prism_dep, fmt_dep, sqlite3_dep],
  name_suffix : example_suffix,
  link_args : example_link_args,
  link_depends : example_shell,
)

if not is_wasm
  custom_target('svg_data_studio',
    output : 'data_studio.svg',
    command : [model_data_studio_exe, '@OUTPUT@'],
    depends : model_data_studio_exe,
    build_by_default : true,
  )
endif
