# Core: header-only + stdexec (header-only P2300 implementation)
stdexec_dep = dependency('stdexec', fallback : ['stdexec', 'stdexec_dep'])
fmt_dep = dependency('fmt', fallback : ['fmt', 'fmt_dep'])

if not has_reflection
  # test=false here, not as -Dmagic_enum:test=false on the command line: with reflection the
  # subproject is never configured, and meson rejects that option on reconfigure.
  magic_enum_dep = dependency('magic_enum', fallback : ['magic_enum', 'magic_enum_dep'],
                              default_options : ['test=false'])
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
  sdl3_image_dep = declare_dependency(compile_args : ['--use-port=sdl3_image:formats=png,svg'],
                                      link_args : ['--use-port=sdl3_image:formats=png,svg'])
  sdl_platform_src = 'backends/sdl_platform_web.cpp'
else
  sdl3_dep = dependency('sdl3', fallback : ['sdl3', 'sdl3_dep'])
  sdl3_ttf_dep = dependency('sdl3_ttf', fallback : ['sdl3_ttf', 'sdl3_ttf_dep'])
  sdl3_image_dep = dependency('sdl3_image', fallback : ['sdl3_image', 'sdl3_image_dep'])
  sdl_platform_src = 'backends/sdl_platform_desktop.cpp'
endif

fonts_dir = meson.project_source_root() / 'assets' / 'fonts'
fonts_install_dir = get_option('datadir') / 'prism' / 'fonts'
ui_font_path = fonts_dir / 'NotoSans-Regular.ttf'
mono_font_path = fonts_dir / 'JetBrainsMonoNerdFont-Regular.ttf'

# WebGPU renderer (opt-in, -Dwebgpu=enabled): prebuilt wgpu-native, one wrap per platform.
wgpu_wraps = {
  'linux-x86_64' : 'wgpu-native-linux-x86_64',
  'linux-aarch64' : 'wgpu-native-linux-aarch64',
  'darwin-x86_64' : 'wgpu-native-macos-x86_64',
  'darwin-aarch64' : 'wgpu-native-macos-aarch64',
}
wgpu_platform = host_machine.system() + '-' + host_machine.cpu_family()
prism_webgpu_dep = dependency('', required : false)
if get_option('webgpu').allowed() and wgpu_platform in wgpu_wraps
  wgpu_native_dep = subproject(wgpu_wraps[wgpu_platform]).get_variable('wgpu_native_dep')
  prism_webgpu_lib = library('prism-webgpu',
    'backends/wgpu_renderer.cpp',
    dependencies : [prism_core_dep, wgpu_native_dep],
    install : true,
  )
  prism_webgpu_dep = declare_dependency(
    link_with : prism_webgpu_lib,
    dependencies : [prism_core_dep, wgpu_native_dep],
  )
elif get_option('webgpu').enabled()
  error('webgpu: no prebuilt wgpu-native for ' + wgpu_platform)
endif

# The SDL backend draws with WebGPU when asked (RenderConfig::renderer) if it was built with it.
sdl_webgpu_src = prism_webgpu_dep.found() ? 'backends/sdl_webgpu.cpp' : 'backends/sdl_webgpu_stub.cpp'

prism_software_backend_lib = library('prism-software-backend',
  'backend.cpp',
  'backends/software_backend.cpp',
  'backends/sdl_window.cpp',
  'backends/sdl_text_measurer.cpp',
  'backends/sdl_font_set.cpp',
  'backends/font_lookup.cpp',
  'backends/sdl_image_decoder.cpp',
  sdl_platform_src,
  sdl_webgpu_src,
  # dl: dladdr() lives in libdl before glibc 2.34; empty dependency elsewhere.
  dependencies : [prism_core_dep, sdl3_dep, sdl3_ttf_dep, sdl3_image_dep, prism_webgpu_dep,
                  meson.get_compiler('cpp').find_library('dl', required : false)],
  cpp_args : ['-DPRISM_UI_FONT_PATH="' + ui_font_path + '"',
              '-DPRISM_UI_FONT_INSTALL_PATH="' + fonts_install_dir / 'NotoSans-Regular.ttf' + '"',
              '-DPRISM_MONO_FONT_PATH="' + mono_font_path + '"',
              '-DPRISM_MONO_FONT_INSTALL_PATH="' + fonts_install_dir / 'JetBrainsMonoNerdFont-Regular.ttf' + '"'],
  install : true,
)

# OFL: the licence texts travel with the fonts. Own install tag, so wheels (tags runtime,
# python-runtime) don't carry a second copy: the Python package ships its own (python/prism).
bundled_font_files = [ui_font_path, mono_font_path,
                      fonts_dir / 'NotoSans-OFL.txt', fonts_dir / 'JetBrainsMono-OFL.txt']
install_data(bundled_font_files, install_dir : fonts_install_dir, install_tag : 'fonts')

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
  # 10k of the Nerd Font's 11.7k characters are icons PRISM never draws. The page ships subsets
  # with only the text ranges (Latin, IPA, Greek, Cyrillic, punctuation, arrows, maths, box
  # drawing, shapes) -- plus, for mono, the two file-tree icons -- instead of ~1.9 MB of fonts.
  # Each subset is preloaded at its desktop path, so font lookup is unchanged. pyftsubset comes
  # from fonttools.
  text_ranges = 'U+0000-02FF,U+0370-03FF,U+0400-052F,U+1E00-1EFF,U+2000-2BFF,U+FB00-FB06,U+FFFD'
  web_fonts = []
  app_link_args = ['-sJSPI', '-sPTHREAD_POOL_SIZE=1']
  foreach f : [[ui_font_path, 'web_ui_font.ttf', text_ranges],
               [mono_font_path, 'web_mono_font.ttf', text_ranges + ',U+F07B,U+F15B']]
    t = custom_target(f[1],
      input : f[0],
      output : f[1],
      command : [find_program('pyftsubset'), '@INPUT@', '--output-file=@OUTPUT@', '--layout-features=*',
                 '--unicodes=' + f[2]],
    )
    web_fonts += t
    app_link_args += ['--preload-file', t.full_path() + '@' + f[0]]
  endforeach
  app_sources = web_fonts
endif

# Convenience: core + default software backend (SDL3 headers exposed for keycodes etc.)
prism_dep = declare_dependency(
  dependencies : [prism_core_dep, prism_software_backend_dep, sdl3_dep],
  sources : app_sources,
  link_args : app_link_args,
)
