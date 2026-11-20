# Firmwares for QEMU (mps2-an500, Cortex-M7): the POSIX layer's tests and PRISM's core with the
# rasterizer. They run as meson tests when qemu-system-arm is installed.
qemu_system_arm = find_program('qemu-system-arm', required : false)

# --specs=nosys.specs and -Wl,--gc-sections come from the cross file.
# nosys.specs' stubs (_close, _read, ...) warn "not implemented" at link time, and werror makes
# the linker warnings fatal.
qemu_link_args = ['-T', qemu_ld, alloc_count_link_arg, '-Wl,--no-fatal-warnings']
qemu_ld_dep = files(qemu_ld)

posix_tests = executable('posix_tests.elf', 'posix_qemu/main.cpp',
  include_directories : [qemu_freertos_inc, prism_inc],
  link_with : qemu_freertos,
  link_whole : [qemu_posix, qemu_board],
  link_depends : qemu_ld_dep,
  link_args : qemu_link_args,
)

prism_tests = executable('prism_tests.elf', 'prism_qemu/main.cpp',
  include_directories : qemu_freertos_inc,
  dependencies : [prism_core_dep, prism_raster_dep],
  link_with : qemu_freertos,
  link_whole : [qemu_posix, qemu_board],
  link_depends : qemu_ld_dep,
  link_args : qemu_link_args,
)

if qemu_system_arm.found()
  qemu_args = ['-M', 'mps2-an500', '-nographic', '-semihosting-config', 'enable=on,target=native']
  test('posix_qemu', qemu_system_arm, args : qemu_args + ['-kernel', posix_tests], timeout : 120)
  test('prism_qemu', qemu_system_arm, args : qemu_args + ['-kernel', prism_tests], timeout : 120)
endif
