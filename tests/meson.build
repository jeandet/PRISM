# simplify: doctest 2.4.12 (latest on wrapdb) trips two clang-only warnings in every test TU:
# TEST_CASE expands __COUNTER__ (-Wc2y-extensions, renamed -Wcounter-extension in newer clang)
# and it always includes the removed <ciso646> header (-W#warnings). Upstream 2.5.x fixes the
# header; drop the -W#warnings silencing once wrapdb ships it. get_supported_arguments drops
# whichever flags the compiler doesn't know (all of them on GCC).
doctest_clang_args = ['-Wno-c2y-extensions', '-Wno-counter-extension', '-Wno-#warnings']
# Under emscripten the tests run in node: main() on a worker so it may block (join, wait),
# direct host filesystem for tests that touch files, and the process exits with main's code.
test_node_link_args = host_machine.system() == 'emscripten' ? ['-sPROXY_TO_PTHREAD', '-sEXIT_RUNTIME', '-sNODERAWFS'] : []
# Tests that open a real SDL window: node has no DOM/canvas, so under emscripten they are
# built (compile/link coverage) but not run.
can_open_window = host_machine.system() != 'emscripten'

doctest_dep = declare_dependency(
  dependencies : dependency('doctest', fallback : ['doctest', 'doctest_dep']),
  compile_args : cpp.get_supported_arguments(doctest_clang_args),
  link_args : test_node_link_args,
)

# Headless tests — no SDL, no backend
headless_tests = {
  'mpsc_queue' : files('test_mpsc_queue.cpp'),
  'atomic_cell' : files('test_atomic_cell.cpp'),
  'draw_list' : files('test_draw_list.cpp'),
  'input_event' : files('test_input_event.cpp'),
  'pixel_buffer' : files('test_pixel_buffer.cpp'),
  'software_renderer' : files('test_software_renderer.cpp'),
  'layout' : files('test_layout.cpp'),
  'hit_test' : files('test_hit_test.cpp'),
  'connection' : files('test_connection.cpp'),
  'connection_thread_safety' : files('test_connection_thread_safety.cpp'),
  'field' : files('test_field.cpp'),
  'state' : files('test_state.cpp'),
  'list' : files('test_list.cpp'),
  'reflect' : files('test_reflect.cpp'),
  'widget_tree' : files('test_widget_tree.cpp'),
  'widget' : files('test_widget.cpp'),
  'exec' : files('test_exec.cpp'),
  'text_field' : files('test_text_field.cpp'),
  'dropdown' : files('test_dropdown.cpp'),
  'password' : files('test_password.cpp'),
  'text_area' : files('test_text_area.cpp'),
  'types' : files('test_types.cpp'),
  'view' : files('test_view.cpp'),
  'node' : files('test_node.cpp'),
  'scroll' : files('test_scroll.cpp'),
  'split' : files('test_split.cpp'),
  'virtual_list' : files('test_virtual_list.cpp'),
  'animation' : files('test_animation.cpp'),
  'table' : files('test_table.cpp'),
  'tabs' : files('test_tabs.cpp'),
  'window' : files('test_window.cpp'),
  'window_registry' : files('test_window_registry.cpp'),
  'window_chrome' : files('test_window_chrome.cpp'),
  'theme' : files('test_theme.cpp'),
  'svg_export' : files('test_svg_export.cpp'),
  'plot' : files('test_plot.cpp'),
  'transaction' : files('test_transaction.cpp'),
  'transaction_cycle_guard' : files('test_transaction_cycle_guard.cpp'),
  'nested_scroll_bubble' : files('test_nested_scroll_bubble.cpp'),
  'table_hover_alias' : files('test_table_hover_alias.cpp'),
  'derived' : files('test_derived.cpp'),
  'shared' : files('test_shared.cpp'),
  'channel' : files('test_channel.cpp'),
  'field_mirror' : files('test_field_mirror.cpp'),
  'inspector' : files('test_inspector.cpp'),
  'fixed_string' : files('test_fixed_string.cpp'),
  'cursor' : files('test_cursor.cpp'),
  'debug_name' : files('test_debug_name.cpp'),
  'flatten_tree' : files('test_flatten_tree.cpp'),
  'tree' : files('test_tree.cpp'),
  'tree_inspector_model' : files('test_tree_inspector_model.cpp'),
  'tree_inspector_controller' : files('test_tree_inspector_controller.cpp'),
  'tree_widget' : files('test_tree_widget.cpp'),
  'tree_controller' : files('test_tree_controller.cpp'),
  'tree_view_builder' : files('test_tree_view_builder.cpp'),
  'tree_reflect' : files('test_tree_reflect.cpp'),
  'file_tree_source' : files('test_file_tree_source.cpp'),
  'proc_metrics' : files('test_proc_metrics.cpp'),
  'process_tree_source' : files('test_process_tree_source.cpp'),
  'snapshot_stats' : files('test_snapshot_stats.cpp'),
  'record_reuse' : files('test_record_reuse.cpp'),
  'perf_lab' : files('test_perf_lab.cpp'),
  'frame_pacer' : files('test_frame_pacer.cpp'),
}

foreach name, src : headless_tests
  exe = executable('test_' + name, src,
    dependencies : [prism_core_dep, doctest_dep],
  )
  test(name, exe)
endforeach

# Tests that need BackendBase vtable but not SDL
backend_base_tests = {
  'null_backend' : files('test_null_backend.cpp'),
  'test_backend' : files('test_test_backend.cpp'),
  'ui' : files('test_ui.cpp'),
  'model_app' : files('test_model_app.cpp'),
  'golden_snapshot' : files('test_golden_snapshot.cpp'),
  'mutation_queue' : files('test_mutation_queue.cpp'),
}

foreach name, src : backend_base_tests
  exe = executable('test_' + name, src,
    dependencies : [prism_software_backend_dep, doctest_dep],
  )
  test(name, exe)
endforeach

# SoftwareBackend::request_window includes <prism/backends/software_backend.hpp>
# directly, which pulls in SDL3/SDL3_ttf headers — prism_software_backend_dep only
# forwards prism_core_dep, so this one needs sdl3_dep/sdl3_ttf_dep explicitly too.
test_software_backend_request_window = executable('test_software_backend_request_window',
  files('test_software_backend_request_window.cpp'),
  dependencies : [prism_software_backend_dep, sdl3_dep, sdl3_ttf_dep, doctest_dep],
)
test('software_backend_request_window', test_software_backend_request_window)

test_sdl_window = executable('test_sdl_window',
  files('test_sdl_window.cpp'),
  dependencies : [prism_software_backend_dep, sdl3_dep, sdl3_ttf_dep, doctest_dep],
)
if can_open_window
  test('sdl_window', test_sdl_window)
endif

test_software_backend_chrome_cursor = executable('test_software_backend_chrome_cursor',
  files('test_software_backend_chrome_cursor.cpp'),
  dependencies : [prism_software_backend_dep, sdl3_dep, sdl3_ttf_dep, doctest_dep],
)
if can_open_window
  test('software_backend_chrome_cursor', test_software_backend_chrome_cursor)
endif

test_software_backend_concurrent = executable('test_software_backend_concurrent',
  files('test_software_backend_concurrent.cpp'),
  dependencies : [prism_software_backend_dep, sdl3_dep, sdl3_ttf_dep, doctest_dep],
)
test('software_backend_concurrent', test_software_backend_concurrent)

test_software_backend_present_stats = executable('test_software_backend_present_stats',
  files('test_software_backend_present_stats.cpp'),
  dependencies : [prism_software_backend_dep, sdl3_dep, sdl3_ttf_dep, doctest_dep],
)
test('software_backend_present_stats', test_software_backend_present_stats)

test_model_app_chrome_cursor = executable('test_model_app_chrome_cursor',
  files('test_model_app_chrome_cursor.cpp'),
  dependencies : [prism_software_backend_dep, sdl3_dep, sdl3_ttf_dep, doctest_dep],
)
if can_open_window
  test('model_app_chrome_cursor', test_model_app_chrome_cursor)
endif

# Tests that need a backend (SDL)
backend_tests = {
  'app' : files('test_app.cpp'),
}

foreach name, src : backend_tests
  exe = executable('test_' + name, src,
    dependencies : [prism_dep, doctest_dep],
  )
  if can_open_window
    test(name, exe)
  endif
endforeach
