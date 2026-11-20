# The FreeRTOS kernel has no build system for Meson: this only names its files and include
# directories. Each board compiles them itself, against its own FreeRTOSConfig.h.
project('freertos-kernel', 'c', version : '11.2.0')

freertos_kernel_files = files(
  'tasks.c', 'queue.c', 'list.c', 'timers.c', 'event_groups.c',
  'portable/MemMang/heap_4.c',
)
freertos_kernel_inc = include_directories('include')

freertos_cm7_port_files = files('portable/GCC/ARM_CM7/r0p1/port.c')
freertos_cm7_port_inc = include_directories('portable/GCC/ARM_CM7/r0p1')
