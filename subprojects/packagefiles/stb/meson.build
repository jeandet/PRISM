# stb is single-header libraries with no build system of their own: the dependency is the
# include directory. prism-raster uses stb_truetype.h.
project('stb', 'c', version : '2.34')

stb_dep = declare_dependency(include_directories : include_directories('.', is_system : true))
meson.override_dependency('stb', stb_dep)
