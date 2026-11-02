# Reads Linux /proc; elsewhere (the web, macOS, Windows) it shows a simulated machine.
model_system_monitor_exe = executable('model_system_monitor', 'model_system_monitor.cpp',
  dependencies : [prism_dep, fmt_dep],
  name_suffix : example_suffix,
  link_args : example_link_args,
  link_depends : example_shell,
)

if not is_wasm
  custom_target('svg_system_monitor',
    output : 'system_monitor.svg',
    command : [model_system_monitor_exe, '@OUTPUT@'],
    depends : model_system_monitor_exe,
    build_by_default : true,
  )
endif
