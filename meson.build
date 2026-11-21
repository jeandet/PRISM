# NOTE: per-subproject 'sdl3:werror=false'/'sdl3:warning_level=0' below does NOT take effect on
# Meson 1.8+ due to a known upstream bug (mesonbuild/meson#14528: parent-project default_options
# cannot set a subproject's warning_level/werror). Kept here as documentation of intent (harmless,
# and would start working if that bug is ever fixed), but a genuinely clean `meson setup builddir`
# on an affected Meson version still needs the equivalent passed on the command line:
#   meson setup builddir -Dsdl3:werror=false -Dsdl3:warning_level=0
# sdl3_ttf's own wrap doesn't hit this in practice here, but is listed for the same intent.
project('prism', 'cpp',
  version : '0.1.0',
  default_options : [
    'cpp_std=c++26',
    'warning_level=3',
    'werror=true',
    'b_ndebug=if-release',
    'sdl3:werror=false',
    'sdl3:warning_level=0',
    'sdl3_ttf:werror=false',
    'sdl3_ttf:warning_level=0',
    'freetype2:werror=false',     # built from source where the system has none (Windows)
    'freetype2:warning_level=0',
    'harfbuzz:werror=false',
    'harfbuzz:warning_level=0',
    'sdl3_image:werror=false',
    'sdl3_image:warning_level=0',
    'libunibreak:werror=false',
    'libunibreak:warning_level=0',
    'sqlite3:werror=false',       # the data studio example's database
    'sqlite3:warning_level=0',
  ],
  license : 'MIT',
  meson_version : '>= 1.5.0',
)

cpp = meson.get_compiler('cpp')

# GCC before 15 reports false "maybe uninitialized" errors on std::optional members compared
# after inlining (GCC bug 80635); with -Werror each one fails the build in a new place.
if cpp.get_id() == 'gcc' and cpp.version().version_compare('<15')
  add_project_arguments('-Wno-maybe-uninitialized', language : 'cpp')
endif

# Meson passes the optimization level to em++ when compiling only. Emscripten needs it when
# linking too: that is where it runs wasm-opt over the whole binary (ports included) and
# minifies the JS glue. Without it every link is effectively -O0.
if host_machine.system() == 'emscripten' and get_option('optimization') != 'plain'
  add_project_link_arguments('-O' + get_option('optimization'), language : ['c', 'cpp'])
endif

# The flags that change what PRISM's headers declare. Every file including them must see the
# same ones as libprism, or the two disagree on class layouts: project arguments for PRISM's
# own targets, and prism_headers_dep's compile_args (src/meson.build) for a parent project.
prism_header_args = []

# Probe the feature macro, not just the flag: emscripten's clang accepts -freflection
# (with an unused-argument warning) but doesn't implement it.
has_reflection = cpp.compiles('''
  #if __cpp_impl_reflection
  int main() { return 0; }
  #else
  #error no reflection
  #endif
''', args : ['-freflection'], name : '__cpp_impl_reflection check')
if has_reflection
  prism_header_args += ['-freflection']
endif

debug_tools_opt = get_option('prism_debug_tools')
if debug_tools_opt.auto()
  debug_tools_enabled = get_option('buildtype') != 'release'
else
  debug_tools_enabled = debug_tools_opt.enabled()
endif
if debug_tools_enabled
  prism_header_args += ['-DPRISM_DEBUG_TOOLS_ENABLED']
endif

# Profiling (doc/design/profiling.md): the PRISM_PROFILE_* marks and the global recorder. Like
# the reflection flag, these defines change what PRISM's headers declare, so they ride on
# prism_header_args: add_project_arguments covers PRISM's own targets, and prism_headers_dep's
# compile_args (src/meson.build) covers every consumer, so all of them compile the marks the
# same way.
profiling_enabled = get_option('profiling').enabled()
if profiling_enabled
  prism_header_args += ['-DPRISM_PROFILING',
                        '-DPRISM_PROFILING_EVENTS=@0@'.format(get_option('profiling_events')),
                        '-DPRISM_PROFILING_THREADS=@0@'.format(get_option('profiling_threads'))]
endif
add_project_arguments(prism_header_args, language : 'cpp')

# No sockets, no filesystem on bare metal: the optional libraries are off unless asked for.
foreach o : ['http', 'io', 'markdown']
  opt = get_option(o)
  set_variable(o + '_on', opt.enabled() or (opt.auto() and host_machine.system() != 'none'))
endforeach

prism_inc = include_directories('include')

sdl_opt = get_option('sdl')
if sdl_opt.auto()
  sdl_enabled = host_machine.system() != 'none'
else
  sdl_enabled = sdl_opt.enabled()
endif

# PRISM's own FreeRTOS kernel and POSIX layer (ports/): only for a bare-metal target with no OS
# of its own. auto means on when host_machine.system() == 'none'; a target whose SDK brings its
# own FreeRTOS (ESP-IDF, doc/design/esp32-port.md) passes -Dfreertos_port=disabled.
if get_option('freertos_port').enabled() and host_machine.system() != 'none'
  error('freertos_port=enabled only makes sense on a bare-metal target (host_machine.system() == \'none\'): ports/ would clash with the hosted system\'s own threads')
endif
freertos_port_on = get_option('freertos_port').enabled() or (get_option('freertos_port').auto() and host_machine.system() == 'none')

subdir('src')

# The prism-trace host tool (tools/prism-trace, doc/design/profiling.md): reads a profile dump
# and writes Chrome trace-event JSON. Hosted only -- bare metal has no host to run a tool on.
# Its library also backs tests/test_prism_trace.cpp, so this comes before subdir('tests').
if host_machine.system() != 'none'
  subdir('tools/prism-trace')
endif

# The FreeRTOS port and the QEMU firmwares that exercise it. tests/embedded needs ports'
# variables, so both go together; with -Dfreertos_port=disabled neither is entered and the
# freertos_* variables are not defined (a board with its own SDK, e.g. ESP-IDF, provides them).
if host_machine.system() == 'none' and freertos_port_on
  subdir('ports')
  if not get_option('tests').disabled()
    subdir('tests/embedded')
  endif
endif
if sdl_enabled and host_machine.system() != 'emscripten'
  subdir('python')
endif
if sdl_enabled and not get_option('tests').disabled()
  subdir('tests')
endif
if sdl_enabled and not get_option('examples').disabled()
  subdir('examples')
endif
if sdl_enabled and not get_option('benchmarks').disabled()
  subdir('benchmarks')
endif
