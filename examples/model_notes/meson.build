model_notes_exe = executable('model_notes', 'model_notes.cpp',
  dependencies : [prism_dep],
  name_suffix : example_suffix,
  link_args : example_link_args,
  link_depends : example_shell,
)

if not is_wasm
  custom_target('svg_model_notes',
    output : 'model_notes.svg',
    command : [model_notes_exe, '@OUTPUT@', meson.project_source_root() / 'examples' / 'model_notes',
               meson.project_source_root() / 'examples' / 'model_notes' / 'notes_logic.hpp'],
    depends : model_notes_exe,
    build_by_default : true,
  )
endif
