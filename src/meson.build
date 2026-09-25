# Core: header-only + stdexec (header-only P2300 implementation)
stdexec_dep = dependency('stdexec', fallback : ['stdexec', 'stdexec_dep'])
fmt_dep = dependency('fmt', fallback : ['fmt', 'fmt_dep'])

if not has_reflection
  magic_enum_dep = dependency('magic_enum', fallback : ['magic_enum', 'magic_enum_dep'])
else
  magic_enum_dep = dependency('', required : false)
endif

prism_core_dep = declare_dependency(
  include_directories : prism_inc,
  dependencies : [stdexec_dep, magic_enum_dep, fmt_dep],
)

# Software backend: links SDL3 + SDL3_ttf
if host_machine.system() == 'emscripten'
  # The wrapdb SDL3 port has no emscripten support; emscripten ships its own ports.
  sdl3_dep = declare_dependency(compile_args : ['--use-port=sdl3'], link_args : ['--use-port=sdl3'])
  sdl3_ttf_dep = declare_dependency(compile_args : ['--use-port=sdl3_ttf'], link_args : ['--use-port=sdl3_ttf'])
  sdl_platform_src = 'backends/sdl_platform_web.cpp'
else
  sdl3_dep = dependency('sdl3', fallback : ['sdl3', 'sdl3_dep'])
  sdl3_ttf_dep = dependency('sdl3_ttf', fallback : ['sdl3_ttf', 'sdl3_ttf_dep'])
  sdl_platform_src = 'backends/sdl_platform_desktop.cpp'
endif

font_path = meson.project_source_root() / 'assets' / 'fonts' / 'JetBrainsMonoNerdFont-Regular.ttf'
font_install_path = get_option('datadir') / 'prism' / 'fonts' / 'JetBrainsMonoNerdFont-Regular.ttf'

prism_software_backend_lib = library('prism-software-backend',
  'backend.cpp',
  'backends/software_backend.cpp',
  'backends/sdl_window.cpp',
  sdl_platform_src,
  dependencies : [prism_core_dep, sdl3_dep, sdl3_ttf_dep],
  cpp_args : ['-DPRISM_FONT_PATH="' + font_path + '"',
              '-DPRISM_FONT_INSTALL_PATH="' + font_install_path + '"'],
  install : true,
)

install_data(font_path, install_dir : get_option('datadir') / 'prism' / 'fonts', install_tag : 'runtime')

prism_software_backend_dep = declare_dependency(
  link_with : prism_software_backend_lib,
  dependencies : [prism_core_dep],
)

# In a browser the main thread must keep returning to the page's event loop, but the backend's
# loop blocks in SDL_WaitEvent. Under JSPI, SDL's wait sleeps via emscripten_sleep, which
# suspends the wasm stack and resumes it later -- so the desktop loop runs unchanged. The font is
# preloaded into the virtual filesystem at its desktop path, so font lookup is unchanged too.
# One pre-started worker for model_app's logic thread: without it emscripten spawns the worker
# on demand and warns that the (exhausted) pool may deadlock.
app_link_args = []
app_sources = []
if host_machine.system() == 'emscripten'
  # 10k of the Nerd Font's 11.7k characters are icons PRISM never draws. The page ships a subset
  # with only the text ranges (Latin, IPA, Greek, Cyrillic, punctuation, arrows, maths, box
  # drawing, shapes): ~100 KB compressed instead of ~1.3 MB. pyftsubset comes from fonttools.
  web_font = custom_target('web_font',
    input : font_path,
    output : 'web_font.ttf',
    command : [find_program('pyftsubset'), '@INPUT@', '--output-file=@OUTPUT@', '--layout-features=*',
               '--unicodes=U+0000-02FF,U+0370-03FF,U+0400-052F,U+1E00-1EFF,U+2000-2BFF,U+FB00-FB06,U+FFFD'],
  )
  app_link_args = ['-sJSPI', '-sPTHREAD_POOL_SIZE=1', '--preload-file', web_font.full_path() + '@' + font_path]
  app_sources = [web_font]
endif

# Convenience: core + default software backend (SDL3 headers exposed for keycodes etc.)
prism_dep = declare_dependency(
  dependencies : [prism_core_dep, prism_software_backend_dep, sdl3_dep],
  sources : app_sources,
  link_args : app_link_args,
)
