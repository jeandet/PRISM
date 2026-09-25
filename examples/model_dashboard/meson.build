model_dashboard_exe = executable('model_dashboard', 'model_dashboard.cpp',
  dependencies : [prism_dep],
  name_suffix : example_suffix,
)

if not is_wasm
  custom_target('svg_model_dashboard',
    output : 'model_dashboard.svg',
    command : [model_dashboard_exe, '@OUTPUT@'],
    depends : model_dashboard_exe,
    build_by_default : true,
  )
endif
