# The icons are read from the source tree; on the web they are bundled into the page at that path.
gallery_icon_dir = meson.current_source_dir() / 'icons'
gallery_args = []
if is_wasm
  gallery_args += ['--preload-file', gallery_icon_dir + '@' + gallery_icon_dir]
endif

# One translation unit per tab: each tab's view() builds its own widgets, so no single compile
# carries every tab's ViewBuilder instantiations, and editing one tab recompiles only that tab.
widget_gallery_sources = files(
  'main.cpp',
  'controls_tab.cpp',
  'catalog_tab.cpp',
  'text_tab.cpp',
  'data_tab.cpp',
  'plot_tab.cpp',
  'popups_tab.cpp',
  'docking_tab.cpp',
  'document_tab.cpp',
  'code_tab.cpp',
  'build_your_own_tab.cpp',
  'orbit_tab.cpp',
  'orbit_canvas.cpp',
  'nodes_tab.cpp'
)

gallery_cpp_args = ['-DGALLERY_ICON_DIR="' + gallery_icon_dir + '"']
if prism_markdown_dep.found()
  gallery_cpp_args += ['-DGALLERY_MARKDOWN']
endif

widget_gallery_exe = executable('widget_gallery', widget_gallery_sources,
  dependencies : [prism_dep, fmt_dep, prism_markdown_dep],
  cpp_args : gallery_cpp_args,
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
