#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* FreeRTOS config for the QEMU mps2-an500 board (Cortex-M7). QEMU models the
   MPS2 CPU clock at 25 MHz and SysTick is clocked from it. This file lives with
   the board support in ports/qemu_mps2/ — another board gets its own config. */

#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configCPU_CLOCK_HZ                      25000000UL
#define configTICK_RATE_HZ                      1000
#define configMAX_PRIORITIES                    5
#define configMINIMAL_STACK_SIZE                128
#define configTOTAL_HEAP_SIZE                   (1024 * 1024) /* heap_4's own static array */
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 1 /* index 0: struct posix_thread * */

/* heap_4's pvPortMalloc uses vTaskSuspendAll, timers need the scheduler state */
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_xTaskAbortDelay                 1
#define INCLUDE_uxTaskGetStackHighWaterMark     1

#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               3
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            256

#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0

#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
/* Run-time stats in microseconds (posix_time.c), for the timing line's CPU load: the idle
   task's share of each second (ports/freertos/system_counters.cpp). 64-bit so it never wraps. */
#define configGENERATE_RUN_TIME_STATS           1
#define configRUN_TIME_COUNTER_TYPE             uint64_t
#ifndef __ASSEMBLER__
#include <stdint.h>
#ifdef __cplusplus
extern "C"
#endif
uint64_t prism_now_us(void);
#endif
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()
#define portGET_RUN_TIME_COUNTER_VALUE()        prism_now_us()

/* newlib thread safety: each task's TCB embeds a struct _reent (the kernel's
   xTLSBlock, via include/newlib-freertos.h) and vTaskSwitchContext points
   newlib's _impure_ptr at the running task's copy, so printf & co. from any
   task use per-task state. The locks around newlib's shared objects (malloc,
   stdio, env, ...) are ours: ports/freertos/newlib_locks.c defines every
   __retarget_lock_* function and every __lock___* object, so newlib's own
   no-op libc_a-lock.o is never pulled in. In this kernel version the reent is
   embedded in the TCB (no configNEWLIB_REENTRANT_IS_DYNAMIC / separate
   allocation: that was the V10-era xNewLib_reent mechanism). */
#define configUSE_NEWLIB_REENTRANT              1

/* Raw NVIC priority register values. 0x50 is valid whether the hardware
   implements 4 or 8 priority bits (bit 0 must stay clear); PendSV/SysTick are
   set to 255 by the port, SVCall to 0. */
#define configKERNEL_INTERRUPT_PRIORITY         255
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    0x50

extern void qemu_assert_fail(const char *file, int line);
#define configASSERT(x) \
    do { if (!(x)) qemu_assert_fail(__FILE__, __LINE__); } while (0)

#endif /* FREERTOS_CONFIG_H */
