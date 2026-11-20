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
