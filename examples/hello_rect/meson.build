hello_rect_exe = executable('hello_rect', 'hello_rect.cpp',
  dependencies : [prism_dep],
  name_suffix : example_suffix,
  link_args : example_link_args,
  link_depends : example_shell,
)

if not is_wasm
  custom_target('svg_hello_rect',
    output : 'hello_rect.svg',
    command : [hello_rect_exe, '@OUTPUT@'],
    depends : hello_rect_exe,
    build_by_default : true,
  )
endif
