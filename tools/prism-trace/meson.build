# prism-trace (doc/design/profiling.md): the host half of the profiler -- reads the framed
# binary dump that prism::profile::dump() writes (a file, or a serial device through prism-io
# when that is built) and writes Chrome trace-event JSON for Perfetto. The reader and the JSON
# writer are a static library of their own, so tests/test_prism_trace.cpp links them without
# the CLI's main(); the include path is tools/, so the test includes <prism-trace/...>.
prism_trace_lib = static_library('prism-trace-lib',
  'trace_reader.cpp',
  'chrome_json.cpp',
  dependencies : [prism_headers_dep],
  install : false,
)
prism_trace_dep = declare_dependency(
  include_directories : include_directories('..'),
  link_with : prism_trace_lib,
  dependencies : [prism_headers_dep],
)

prism_trace_deps = [prism_trace_dep, prism_core_dep]
prism_trace_args = []
if prism_io_dep.found()
  # A path under /dev/ or a COM name is read through io::SerialPort (main.cpp's
  # PRISM_TRACE_HAS_IO branch).
  prism_trace_deps += prism_io_dep
  prism_trace_args += '-DPRISM_TRACE_HAS_IO'
endif

executable('prism-trace', 'main.cpp',
  dependencies : prism_trace_deps,
  cpp_args : prism_trace_args,
  install : true,
)
