# With prism-io (not in the browser): --serve-instrument and --instrument, the bench over TCP.
model_lab_bench_exe = executable('model_lab_bench', 'model_lab_bench.cpp',
  dependencies : [prism_dep, fmt_dep, prism_io_dep],
  cpp_args : prism_io_dep.found() ? ['-DPRISM_LAB_BENCH_LINK=1'] : [],
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
  # The same screenshot with each block outlined and named (WidgetTree::show_blocks).
  custom_target('svg_lab_bench_blocks',
    output : 'lab_bench_blocks.svg',
    command : [model_lab_bench_exe, '@OUTPUT@'],
    env : {'PRISM_SHOW_BLOCKS' : '1'},
    depends : model_lab_bench_exe,
    build_by_default : true,
  )
endif
