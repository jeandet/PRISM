# QEMU -M mps2-an500 (Cortex-M7): the board support for the test firmwares (tests/embedded).
qemu_board_inc = include_directories('.')
qemu_ld = meson.current_source_dir() / 'mps2_an500.ld'

qemu_freertos_inc = [freertos_kernel_inc, freertos_cm7_port_inc, qemu_board_inc]

# Vendor code: not ours to make warning-clean.
qemu_freertos = static_library('qemu_freertos',
  freertos_kernel_files, freertos_cm7_port_files,
  include_directories : qemu_freertos_inc,
  override_options : ['warning_level=1', 'werror=false'],
)

qemu_posix = static_library('qemu_posix', freertos_posix_files,
  include_directories : [qemu_freertos_inc, freertos_posix_inc, prism_inc],
)

# Vectors and Reset_Handler, semihosting log/_write/exit, the FreeRTOS hooks.
qemu_startup = static_library('qemu_startup', files('startup.c'),
  include_directories : qemu_freertos_inc,
  # The vector table uses GNU range designators and function-pointer casts.
  c_args : ['-Wno-pedantic'],
)

qemu_board = static_library('qemu_board',
  files('semihost.c', 'freertos_hooks.c'),
  include_directories : qemu_freertos_inc,
  link_whole : qemu_startup,
)
