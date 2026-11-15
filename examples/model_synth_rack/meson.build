model_synth_rack_exe = executable('model_synth_rack', 'model_synth_rack.cpp',
  # model_app's logic thread, the analyzer thread, and the audio device's own thread: four
  # pre-started workers on wasm. A dependency listed after prism_dep, so its pool size comes
  # later on the link line and wins.
  dependencies : [prism_dep, fmt_dep, declare_dependency(link_args : is_wasm ? ['-sPTHREAD_POOL_SIZE=4'] : [])],
  name_suffix : example_suffix,
  link_args : example_link_args,
  link_depends : example_shell,
)

if not is_wasm
  custom_target('svg_synth_rack',
    output : 'synth_rack.svg',
    command : [model_synth_rack_exe, '@OUTPUT@'],
    depends : model_synth_rack_exe,
    build_by_default : true,
  )
  # The same screenshot with each block outlined and named (WidgetTree::show_blocks).
  custom_target('svg_synth_rack_blocks',
    output : 'synth_rack_blocks.svg',
    command : [model_synth_rack_exe, '@OUTPUT@'],
    env : {'PRISM_SHOW_BLOCKS' : '1'},
    depends : model_synth_rack_exe,
    build_by_default : true,
  )
endif
