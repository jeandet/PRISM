perf_lab_exe = executable('perf_lab', 'main.cpp',
  # Two pre-started workers on wasm: model_app's logic thread plus the telemetry producer. A
  # dependency listed after prism_dep, so its pool size comes later on the link line and wins.
  dependencies : [prism_dep, declare_dependency(link_args : is_wasm ? ['-sPTHREAD_POOL_SIZE=2'] : [])],
  name_suffix : example_suffix,
  link_args : example_link_args,
  link_depends : example_shell,
)

if not is_wasm
  custom_target('svg_perf_lab',
    output : 'perf_lab.svg',
    command : [perf_lab_exe, '@OUTPUT@'],
    depends : perf_lab_exe,
    build_by_default : true,
  )
endif
