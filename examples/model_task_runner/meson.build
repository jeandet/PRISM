# Runs generated tasks everywhere; the real ones (ping, ls, uname) need a shell, so they are
# offered on the desktop only.
model_task_runner_exe = executable('model_task_runner', 'model_task_runner.cpp',
  # Each run has its own thread: a few pre-started workers on wasm, so runs can overlap.
  dependencies : [prism_dep, fmt_dep, declare_dependency(link_args : is_wasm ? ['-sPTHREAD_POOL_SIZE=4'] : [])],
  name_suffix : example_suffix,
  link_args : example_link_args,
  link_depends : example_shell,
)

if not is_wasm
  custom_target('svg_task_runner',
    output : 'task_runner.svg',
    command : [model_task_runner_exe, '@OUTPUT@'],
    depends : model_task_runner_exe,
    build_by_default : true,
  )
  # The same screenshot with each block outlined and named (WidgetTree::show_blocks).
  custom_target('svg_task_runner_blocks',
    output : 'task_runner_blocks.svg',
    command : [model_task_runner_exe, '@OUTPUT@'],
    env : {'PRISM_SHOW_BLOCKS' : '1'},
    depends : model_task_runner_exe,
    build_by_default : true,
  )
endif
