# On wasm there is no disk: bundle PRISM's own sources and open them.
notes_args = []
notes_depends = []
if is_wasm
  foreach dir : ['include', 'src', 'examples']
    notes_args += ['--preload-file', meson.project_source_root() / dir + '@/PRISM/' + dir]
  endforeach
  notes_args += ['--pre-js', meson.current_source_dir() / 'web_pre.js']
  notes_depends = files('web_pre.js')
endif

model_notes_exe = executable('model_notes', 'model_notes.cpp',
  dependencies : [prism_dep],
  name_suffix : example_suffix,
  link_args : example_link_args + notes_args,
  link_depends : [example_shell, notes_depends],
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
