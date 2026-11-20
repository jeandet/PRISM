# Ports: what a target needs under the frame runtime (platform/): an RTOS, a libc glue, a board.
# The kernel files and include directories are variables here (and so reachable by
# subproject('prism').get_variable()): a board compiles them with its own FreeRTOSConfig.h.
add_languages('c', required : true, native : false)

freertos_kernel = subproject('freertos-kernel')
freertos_kernel_files = freertos_kernel.get_variable('freertos_kernel_files')
freertos_kernel_inc = freertos_kernel.get_variable('freertos_kernel_inc')
freertos_cm7_port_files = freertos_kernel.get_variable('freertos_cm7_port_files')
freertos_cm7_port_inc = freertos_kernel.get_variable('freertos_cm7_port_inc')

subdir('freertos')
subdir('qemu_mps2')
