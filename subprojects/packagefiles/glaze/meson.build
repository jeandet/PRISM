# Our own build file for Glaze (JSON and TOML from a struct, header-only): upstream builds with
# CMake only. Its headers are system headers here, so our warning flags do not apply to them.
project('glaze', 'cpp', version : '9.1.0')

# GLZ_REFLECTION26=0: Glaze's own C++20 reflection in every build. Left to itself it switches to
# C++26 reflection under -freflection, where 9.1.0 takes a field with a glz::custom meta for a
# TOML table and fails to compile. Its own reflection works in all our builds, so they agree.
glaze_dep = declare_dependency(include_directories : include_directories('include', is_system : true),
                               compile_args : ['-DGLZ_REFLECTION26=0'])
meson.override_dependency('glaze', glaze_dep)
