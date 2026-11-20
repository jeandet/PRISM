/* QEMU mps2-an500 bring-up: vector table, Reset_Handler (copies .data and the
   .tdata template, zeroes .bss, enables the FPU, runs .init_array, calls main)
   and default handlers. FreeRTOS's SVC/PendSV/SysTick handlers are mapped at
   their architectural exception slots (11/14/15). */
#include <stdint.h>

#include "qemu_log.h"

extern int main(void);
extern void __libc_init_array(void);

extern void vPortSVCHandler(void);
extern void xPortPendSVHandler(void);
extern void xPortSysTickHandler(void);

/* Symbols exported by mps2_an500.ld. */
extern uint32_t _sidata[], _sdata[], _edata[];
extern uint32_t _tdata_lma[], _tdata_start[], _tdata_end[];
extern uint32_t _sbss[], _ebss[], _estack[];

void Reset_Handler(void);
void Default_Handler(void);

typedef void (*isr_t)(void);

__attribute__((section(".vectors"), used))
static const isr_t vectors[] = {
    [0] = (isr_t)&_estack,        /* initial stack pointer */
    [1] = Reset_Handler,          /* Reset */
    [2 ... 10] = Default_Handler, /* NMI, HardFault, MemManage, BusFault, UsageFault, reserved */
    [11] = vPortSVCHandler,       /* SVCall */
    [12 ... 13] = Default_Handler,
    [14] = xPortPendSVHandler,    /* PendSV */
    [15] = xPortSysTickHandler,   /* SysTick */
    [16 ... 79] = Default_Handler, /* external IRQs: none are enabled */
};

void Reset_Handler(void) {
    uint32_t *src = _sidata;
    uint32_t *dst = _sdata;
    while (dst < _edata) *dst++ = *src++;
    src = _tdata_lma; /* .tdata template image */
    dst = _tdata_start;
    while (dst < _tdata_end) *dst++ = *src++;
    dst = _sbss;
    while (dst < _ebss) *dst++ = 0;

    /* Enable the FPU: CPACR CP10/CP11 full access (the FreeRTOS port does it
       again in xPortStartScheduler). */
    *(volatile uint32_t *)0xE000ED88u |= 0xFu << 20;
    __asm volatile ("dsb\nisb" ::: "memory");

    __libc_init_array();
    (void)main();
    for (;;) __asm volatile ("wfi");
}

void Default_Handler(void) {
    qemu_log("unexpected exception/interrupt\n");
    exit_qemu(94);
}
