# Overlay for the prebuilt wgpu-native release archives (subprojects/wgpu-native-*.wrap).
# Linked statically: no runtime library path to set, nothing extra to bundle into wheels.
project('wgpu-native', 'c', version : '29.0.1.1')

cc = meson.get_compiler('c')
lib = cc.find_library('wgpu_native', dirs : meson.current_source_dir() / 'lib', static : true)

# What the Rust static library needs from the system.
system_deps = [dependency('threads'), cc.find_library('dl', required : false), cc.find_library('m', required : false)]
if host_machine.system() == 'darwin'
  system_deps += dependency('appleframeworks', modules : ['Metal', 'QuartzCore', 'Foundation', 'CoreGraphics'])
  system_deps += cc.find_library('objc')
endif

wgpu_native_dep = declare_dependency(
  include_directories : include_directories('include'),
  dependencies : [lib] + system_deps,
)
