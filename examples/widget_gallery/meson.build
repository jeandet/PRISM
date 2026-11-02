# The icons are read from the source tree; on the web they are bundled into the page at that path.
gallery_icon_dir = meson.current_source_dir() / 'icons'
gallery_args = []
if is_wasm
  gallery_args += ['--preload-file', gallery_icon_dir + '@' + gallery_icon_dir]
endif

widget_gallery_exe = executable('widget_gallery', 'main.cpp',
  dependencies : [prism_dep, fmt_dep],
  cpp_args : ['-DGALLERY_ICON_DIR="' + gallery_icon_dir + '"'],
  name_suffix : example_suffix,
  link_args : example_link_args + gallery_args,
  link_depends : example_shell,
)

if not is_wasm
  custom_target('svg_widget_gallery',
    output : 'widget_gallery.svg',
    command : [widget_gallery_exe, '@OUTPUT@'],
    depends : widget_gallery_exe,
    build_by_default : true,
  )
endif
