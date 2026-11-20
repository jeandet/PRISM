/* FreeRTOS application hooks for the QEMU firmware: each logs and exits QEMU
   with a distinct code so the host can tell internal faults from test failures
   (90 assert, 91 malloc failed, 93 stack overflow, 94 unexpected exception,
   99 watchdog). */
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "qemu_log.h"

void vApplicationMallocFailedHook(void) {
    printf("FAIL: vApplicationMallocFailedHook\n");
    exit_qemu(91);
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    printf("FAIL: stack overflow in task %s\n", pcTaskName);
    exit_qemu(93);
}

void qemu_assert_fail(const char *file, int line) {
    printf("FAIL: configASSERT %s:%d\n", file, line);
    exit_qemu(90);
}
