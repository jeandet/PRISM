# QEMU -M mps2-an500 (Cortex-M7): the board support for the test firmwares (tests/embedded).
qemu_board_inc = include_directories('.')
qemu_ld = meson.current_source_dir() / 'mps2_an500.ld'

firmware_options = ['warning_level=1', 'werror=false']
qemu_freertos_inc = [freertos_kernel_inc, freertos_cm7_port_inc, qemu_board_inc]

qemu_freertos = static_library('qemu_freertos',
  freertos_kernel_files, freertos_cm7_port_files,
  include_directories : qemu_freertos_inc,
  override_options : firmware_options,
)

qemu_posix = static_library('qemu_posix', freertos_posix_files,
  include_directories : [qemu_freertos_inc, freertos_posix_inc, prism_inc],
  override_options : firmware_options,
)

# Vectors and Reset_Handler, semihosting log/_write/exit, the FreeRTOS hooks.
qemu_board = static_library('qemu_board',
  files('startup.c', 'semihost.c', 'freertos_hooks.c'),
  include_directories : qemu_freertos_inc,
  override_options : firmware_options,
)
