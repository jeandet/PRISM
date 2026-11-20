# What a board must provide to use these files (ports/qemu_mps2/ is the reference board):
#  - linker symbols: `end` (start of the C heap, after .bss) and `_heap_end` (its limit), used by
#    sbrk.c and system_counters.cpp; `_tdata_start`, `_tdata_end`, `_tbss_end` and `_tdata_lma`
#    (the TLS template and the load address of its .tdata image), used by tls.c.
#  - FreeRTOSConfig.h: configNUM_THREAD_LOCAL_STORAGE_POINTERS >= 1 (slot 0 holds the thread),
#    configUSE_NEWLIB_REENTRANT 1, configGENERATE_RUN_TIME_STATS 1 with
#    configRUN_TIME_COUNTER_TYPE uint64_t and portGET_RUN_TIME_COUNTER_VALUE() = prism_now_us(),
#    and INCLUDE_vTaskDelete, INCLUDE_vTaskDelay, INCLUDE_uxTaskPriorityGet,
#    INCLUDE_xTaskGetSchedulerState, INCLUDE_xTaskGetCurrentTaskHandle and
#    INCLUDE_xTaskGetIdleTaskHandle (ulTaskGetIdleRunTimeCounter); configTOTAL_HEAP_SIZE with a
#    heap_4-style xPortGetFreeHeapSize().
#  - the link argument alloc_count_link_arg (-Wl,--wrap=_malloc_r), so allocations are counted.
#
# The POSIX layer (pthreads, clocks, newlib locks, TLS) over FreeRTOS, the allocation counter
# and guard, newlib's _sbrk, and the platform hooks the frame runtime needs. Compiled by each
# board with its own FreeRTOSConfig.h.
freertos_posix_files = files(
  'freertos_posix.c',
  'posix_mutex.c',
  'posix_cond.c',
  'posix_rwlock.c',
  'posix_time.c',
  'newlib_locks.c',
  'alloc_count.c',
  'tls.c',
  'tls_support.S',
  'system_counters.cpp',
  'sbrk.c',
)
freertos_posix_inc = include_directories('.')

# The posix and board objects are linked whole: libstdc++'s gthr-posix references the pthread_*
# functions weakly (a plain archive would never be pulled in), and newlib's retargetable locks
# in newlib_locks.c must be defined before libc.a is scanned.
# Heap allocations go through alloc_count.c first, which counts them (the timing line).
alloc_count_link_arg = '-Wl,--wrap=_malloc_r'
