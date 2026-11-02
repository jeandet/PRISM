# Perf-lab benchmarks (doc/review-2026-08-28.md) -- standalone executables that print numbers,
# not doctest cases. Run manually, e.g.: ./builddir/benchmarks/bench_stall_latency

executable('bench_stall_latency', 'stall_latency.cpp',
  dependencies : [prism_software_backend_dep],
)

executable('bench_widget_scale', 'widget_scale.cpp',
  dependencies : [prism_core_dep, fmt_dep],
)
