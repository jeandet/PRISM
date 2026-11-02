# Our own build of SheenBidi (UAX #9 bidi): upstream's meson.build runs a Python script in
# project() and builds a shared library. This compiles its single unity source, statically and
# position-independent, so it links into the backend library and the Python extension alike.
project('sheenbidi', 'c', version : '3.0.0')

threads_dep = dependency('threads')   # its scratch memory is per thread (pthread keys or TLS)

sheenbidi_lib = static_library('sheenbidi', 'Source/SheenBidi.c',
  c_args : ['-DSB_CONFIG_UNITY'],
  include_directories : include_directories('Headers', 'Source'),
  dependencies : threads_dep,
  pic : true,
  override_options : ['werror=false', 'warning_level=0'])   # third-party code: not our warnings

sheenbidi_dep = declare_dependency(include_directories : include_directories('Headers'),
  link_with : sheenbidi_lib, dependencies : threads_dep)
meson.override_dependency('sheenbidi', sheenbidi_dep)
