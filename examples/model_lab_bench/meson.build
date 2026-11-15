model_lab_bench_exe = executable('model_lab_bench', 'model_lab_bench.cpp',
  dependencies : [prism_dep, fmt_dep],
  name_suffix : example_suffix,
  link_args : example_link_args,
  link_depends : example_shell,
)

if not is_wasm
  custom_target('svg_lab_bench',
    output : 'lab_bench.svg',
    command : [model_lab_bench_exe, '@OUTPUT@'],
    depends : model_lab_bench_exe,
    build_by_default : true,
  )
endif
