# Core: headers for templates, libprism (below) for the rest; stdexec is header-only
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

# Unicode text boundaries (ui/text_segmentation.hpp): graphemes and line breaks.
# Static: it links into our libraries, so a wheel needs no extra shared library to bundle.
unibreak_dep = dependency('libunibreak', fallback : ['libunibreak', 'unibreak_dep'],
                          default_options : ['tests=disabled', 'default_library=static'])

# Bidirectional text (ui/text_bidi.hpp): visual runs of a line. Static, like libunibreak.
sheenbidi_dep = dependency('sheenbidi', fallback : ['sheenbidi', 'sheenbidi_dep'])

# Settings files (app/settings.hpp): a struct of fields to and from TOML. Header-only.
glaze_dep = dependency('glaze', fallback : ['glaze', 'glaze_dep'])

prism_headers_dep = declare_dependency(
  include_directories : prism_inc,
  dependencies : [stdexec_dep, magic_enum_dep, fmt_dep, unibreak_dep, sheenbidi_dep, glaze_dep],
)

# The non-template code of the headers, compiled once instead of in every file that includes
# them. simplify: built with this build's flags (reflection or not, NDEBUG or not); a header
# whose inline code differs on those must keep that code in the header. A project linking an
# installed libprism with other flags needs a library per flag set; add when one is packaged.
prism_lib = library('prism',
  'app/backend.cpp',
  'app/model_app.cpp',
  'app/view_builder.cpp',
  'app/widget_tree.cpp',
  'core/shared_state.cpp',
  'delegates/color_delegates.cpp',
  'delegates/date_delegates.cpp',
  'delegates/dialog_delegates.cpp',
  'delegates/dropdown_delegates.cpp',
  'delegates/form_delegates.cpp',
  'delegates/menu_delegates.cpp',
  'delegates/tabs_delegates.cpp',
  'delegates/text_delegates.cpp',
  'delegates/time_delegates.cpp',
  'text/document.cpp',
  'text/layout.cpp',
  'text/view.cpp',
  'text/editor.cpp',
  'text/highlight.cpp',
  'text/to_markdown.cpp',
  'ui/delegate.cpp',
  dependencies : [prism_headers_dep],
  install : true,
)

prism_core_dep = declare_dependency(
  link_with : prism_lib,
  dependencies : [prism_headers_dep],
)

# Software backend: links SDL3 + SDL3_ttf
if host_machine.system() == 'emscripten'
  # The wrapdb SDL3 port has no emscripten support; emscripten ships its own ports.
  # Emscripten's SDL3 port, also as `sdl3` for the subprojects built on it (SDL3_image: there
  # is no sdl3_image port, so it is built from our wrap as on desktop).
  sdl3_dep = declare_dependency(compile_args : ['--use-port=sdl3'], link_args : ['--use-port=sdl3'],
                                version : '3.4.0')
  meson.override_dependency('sdl3', sdl3_dep)
  sdl3_ttf_dep = declare_dependency(compile_args : ['--use-port=sdl3_ttf'], link_args : ['--use-port=sdl3_ttf'])
  sdl_platform_src = 'backends/sdl_platform_web.cpp'
else
  sdl3_dep = dependency('sdl3', fallback : ['sdl3', 'sdl3_dep'])
  # Text shaping (right-to-left runs, ligatures, kerning) is HarfBuzz inside SDL_ttf, which only
  # uses it when it is found: an optional dependency never falls back to our wrap, so without a
  # system HarfBuzz (Windows) text was laid out unshaped. Required here, before SDL_ttf looks.
  harfbuzz_dep = dependency('harfbuzz',
                            default_options : ['freetype=enabled', 'tests=disabled', 'docs=disabled',
                                               'utilities=disabled', 'introspection=disabled',
                                               'glib=disabled', 'gobject=disabled', 'cairo=disabled',
                                               'chafa=disabled', 'icu=disabled'])
  sdl3_ttf_dep = dependency('sdl3_ttf', fallback : ['sdl3_ttf', 'sdl3_ttf_dep'])
  sdl_platform_src = 'backends/sdl_platform_desktop.cpp'
endif
sdl3_image_dep = dependency('sdl3_image', fallback : ['sdl3_image', 'sdl3_image_dep'])

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
  'backends/settings_dir.cpp',
  'backends/sdl_image_decoder.cpp',
  'backends/sdl_audio.cpp',
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

# HTTP (net/http.hpp): cpp-httplib behind prism-net. Header-only upstream; TLS when OpenSSL is
# found. No sockets in the browser, so never built there.
prism_net_dep = dependency('', required : false)
httplib_dep = dependency('', required : false)
if host_machine.system() != 'emscripten' and get_option('http').allowed()
  # Its async name lookup calls GetAddrInfoExCancel, which MinGW's headers do not declare.
  httplib_options = ['zlib=disabled', 'brotli=disabled', 'zstd=disabled']
  if host_machine.system() == 'windows'
    httplib_options += 'non_blocking_getaddrinfo=disabled'
  endif
  httplib_dep = dependency('cpp-httplib', required : get_option('http'), fallback : ['cpp-httplib', 'cpp_httplib_dep'],
                           default_options : httplib_options)
  # A system header: its own warnings (OpenSSL 4.1 deprecations, ...) are not ours to fail on.
  httplib_dep = httplib_dep.as_system('system')
  # httplib.h asks for winsock and the certificate store with #pragma comment(lib), which only
  # MSVC reads: MinGW must be told. Part of httplib_dep, so the test's own server gets them too.
  if host_machine.system() == 'windows' and httplib_dep.found()
    httplib_dep = declare_dependency(dependencies : [httplib_dep,
                                                     meson.get_compiler('cpp').find_library('ws2_32'),
                                                     meson.get_compiler('cpp').find_library('crypt32')])
  endif
  if httplib_dep.found()
    prism_net_lib = library('prism-net', 'net/http.cpp',
      dependencies : [prism_core_dep, httplib_dep],
      install : true,
    )
    prism_net_dep = declare_dependency(link_with : prism_net_lib, dependencies : [prism_core_dep])
  endif
endif

# Markdown (text/markdown.hpp): GitHub's CommonMark parser, cmark-gfm, into a text::Document.
# Static, like libunibreak: a wheel needs no extra shared library. Its own warnings are not ours.
prism_markdown_dep = dependency('', required : false)
if get_option('markdown').allowed()
  cmark_dep = dependency('libcmark-gfm', required : get_option('markdown'),
                         fallback : ['cmark-gfm', 'cmark_gfm_dep'],
                         default_options : ['default_library=static', 'werror=false', 'warning_level=0'])
  if cmark_dep.found()
    prism_markdown_lib = library('prism-markdown', 'text/markdown.cpp',
      dependencies : [prism_core_dep, cmark_dep.as_system('system')],
      install : true,
    )
    prism_markdown_dep = declare_dependency(link_with : prism_markdown_lib, dependencies : [prism_core_dep])
  endif
endif

# Byte streams (io/streams.hpp): TCP and serial ports through standalone Asio (header-only) behind
# prism-io. No sockets in the browser, so never built there.
prism_io_dep = dependency('', required : false)
if host_machine.system() != 'emscripten' and get_option('io').allowed()
  asio_dep = dependency('asio', required : get_option('io'), fallback : ['asio', 'asio_dep']).as_system('system')
  if asio_dep.found()
    # Asio's accept uses AcceptEx from mswsock; WrapDB's asio links only ws2_32.
    io_platform_deps = host_machine.system() == 'windows' ? [meson.get_compiler('cpp').find_library('mswsock')] : []
    prism_io_lib = library('prism-io', 'io/streams.cpp',
      dependencies : [prism_headers_dep, asio_dep] + io_platform_deps,
      install : true,
    )
    prism_io_dep = declare_dependency(link_with : prism_io_lib, dependencies : [prism_headers_dep])
  endif
endif

# OFL: the licence texts travel with the fonts. Own install tag, so wheels (tags runtime,
# python-runtime) don't carry a second copy: the Python package ships its own (python/prism).
# The script fonts are found beside the UI font (SdlFontSet::script_font_files).
script_font_paths = [fonts_dir / 'NotoSansHebrew-Regular.ttf', fonts_dir / 'NotoSansArabic-Regular.ttf']
bundled_font_files = [ui_font_path, mono_font_path] + script_font_paths + [
                      fonts_dir / 'NotoSans-OFL.txt', fonts_dir / 'JetBrainsMono-OFL.txt',
                      fonts_dir / 'NotoSansScripts-OFL.txt']
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
  app_link_args = ['-sJSPI', '-sPTHREAD_POOL_SIZE=1',
                   # settings kept in the page's IndexedDB (backends/web_settings.js says how)
                   '-lidbfs.js', '--pre-js=' + (meson.current_source_dir() / 'backends' / 'web_settings.js')]
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
  foreach f : script_font_paths   # small already (Hebrew 26 KB, Arabic 229 KB): not subset
    app_link_args += ['--preload-file', f + '@' + f]
  endforeach
endif

# Convenience: core + default software backend (SDL3 headers exposed for keycodes etc.)
prism_dep = declare_dependency(
  dependencies : [prism_core_dep, prism_software_backend_dep, sdl3_dep],
  sources : app_sources,
  link_args : app_link_args,
)
