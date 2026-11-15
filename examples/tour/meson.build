# One program per step, in order: no shared header, no shared source.
foreach step : ['01_struct_to_ui', '02_derived', '03_worker', '04_custom_widget', '05_block']
  # 03_worker runs a std::jthread: on wasm, pre-start its worker like the other threaded examples.
  wasm_worker_pool = is_wasm and step == '03_worker' ? ['-sPTHREAD_POOL_SIZE=2'] : []
  executable(step, step + '.cpp',
    dependencies : [prism_dep, declare_dependency(link_args : wasm_worker_pool)],
    name_suffix : example_suffix,
    link_args : example_link_args,
    link_depends : example_shell,
  )
endforeach
