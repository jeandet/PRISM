# prism-trace as a web page (trace.html, copied to the site by scripts/build_web_site.sh).
#
# Built by a direct em++ call, not as a Meson target: the cross file compiles everything with
# -pthread, and a threaded module needs a cross-origin isolated page (COOP/COEP). Such a page
# cannot talk to a window it opens on another site, and the page hands its trace to
# ui.perfetto.dev that way (postMessage). This module has no threads, so the page needs no
# isolation.
prism_trace_web = custom_target('prism-trace-web',
  input : ['trace_web.cpp', '../trace_reader.cpp', '../chrome_json.cpp'],
  output : ['prism-trace-web.js', 'prism-trace-web.wasm'],
  command : [find_program('em++'), '-std=c++26', '-O2', '-fwasm-exceptions',
             '-I' + meson.project_source_root() / 'include', '@INPUT@', '-o', '@OUTPUT0@',
             '-sMODULARIZE', '-sEXPORT_NAME=PrismTrace', '-sALLOW_MEMORY_GROWTH',
             '-sENVIRONMENT=web,node',
             '-sEXPORTED_FUNCTIONS=_malloc,_free',
             '-sEXPORTED_RUNTIME_METHODS=HEAPU8,UTF8ToString'],
  depend_files : files('../trace_reader.hpp', '../chrome_json.hpp',
                       meson.project_source_root() / 'include/prism/profile/trace_format.hpp'),
  build_by_default : true,
)
