/* __aeabi_read_tp: return the current thread's TLS thread pointer in r0.
   The EABI requires this helper to clobber only r0, so r1-r3, ip and lr are
   saved around the call to posix_current_tp() (Cortex-M has no thread-pointer
   register; the pointer lives in each task's control block, reachable through
   FreeRTOS thread-local-storage pointer 0). */
    .syntax unified
    .cpu cortex-m7
    .thumb
    .section .text.__aeabi_read_tp, "ax", %progbits
    .global __aeabi_read_tp
    .type   __aeabi_read_tp, %function
    .thumb_func
__aeabi_read_tp:
    push    {r1, r2, r3, ip, lr}
    bl      posix_current_tp
    pop     {r1, r2, r3, ip, lr}
    bx      lr
    .size   __aeabi_read_tp, . - __aeabi_read_tp
