/* QEMU -M mps2-an500 (Cortex-M7) linker script.

   Memory map (as modelled by QEMU's hw/arm/mps2.c for the AN500):
     ZBT SSRAM1  0x00000000  4 MiB  - code, vectors, and the load image of .data/.tdata
     SRAM        0x20000000  4 MiB  - .data/.bss/heap, initial SP at the top
   (SSRAM2 at 0x00400000 also exists but is not needed.) */

/* The posix layer is referenced weakly by libstdc++ (gthr-posix), so --gc-sections
   would drop it; EXTERN makes every one of these symbols a GC root. */
EXTERN(
    pthread_create pthread_join pthread_detach pthread_self pthread_equal
    pthread_cancel pthread_exit pthread_once
    pthread_key_create pthread_key_delete pthread_getspecific pthread_setspecific
    pthread_attr_init pthread_attr_destroy pthread_attr_setstacksize
    pthread_attr_getstacksize pthread_attr_setdetachstate pthread_attr_getdetachstate
    pthread_mutex_init pthread_mutex_destroy pthread_mutex_lock pthread_mutex_trylock
    pthread_mutex_unlock pthread_mutex_timedlock
    pthread_mutexattr_init pthread_mutexattr_destroy pthread_mutexattr_settype
    pthread_mutexattr_gettype
    pthread_cond_init pthread_cond_destroy pthread_cond_wait pthread_cond_timedwait
    pthread_cond_signal pthread_cond_broadcast
    pthread_condattr_init pthread_condattr_destroy pthread_condattr_setclock
    pthread_condattr_getclock
    pthread_rwlock_init pthread_rwlock_destroy pthread_rwlock_rdlock
    pthread_rwlock_tryrdlock pthread_rwlock_timedrdlock pthread_rwlock_wrlock
    pthread_rwlock_trywrlock pthread_rwlock_timedwrlock pthread_rwlock_unlock
    sched_yield clock_gettime nanosleep sleep usleep gettimeofday _gettimeofday time
    __aeabi_read_tp __cxa_thread_atexit _write
)

ENTRY(Reset_Handler)

MEMORY
{
    ROM (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}

SECTIONS
{
    .text :
    {
        KEEP(*(.vectors))
        KEEP(*(.init)) KEEP(*(.fini))   /* crti/crtn pieces of _init/_fini: unreferenced, never collected */
        *(.text*)
        *(.rodata*)
        *(.glue_7) *(.glue_7t)
        . = ALIGN(8);
        _etext = .;
    } > ROM

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > ROM

    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > ROM

    /* crtbegin.o refers to .eh_frame (frame_dummy): kept, not discarded. */
    .eh_frame :
    {
        KEEP(*(.eh_frame))
    } > ROM

    .preinit_array :
    {
        PROVIDE(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE(__preinit_array_end = .);
    } > ROM

    .init_array :
    {
        PROVIDE(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array))
        PROVIDE(__init_array_end = .);
    } > ROM

    .fini_array :
    {
        PROVIDE(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*))) KEEP(*(.fini_array))
        PROVIDE(__fini_array_end = .);
    } > ROM

    .data :
    {
        . = ALIGN(8);
        _sdata = .;
        *(.data*)
        *(.got*)
        . = ALIGN(8);
        _edata = .;
        _sidata = LOADADDR(.data);
    } > RAM AT> ROM

    /* TLS template: copied per thread by ports/freertos/tls.c from its load image. */
    .tdata :
    {
        . = ALIGN(8);
        _tdata_start = .;
        *(.tdata .tdata.*)
        _tdata_end = .;
        _etdata = .;
        _tdata_lma = LOADADDR(.tdata);
    } > RAM AT> ROM

    .tbss (NOLOAD) :
    {
        _tbss_start = .;
        *(.tbss .tbss.*)
        _tbss_end = .;
    } > RAM

    .bss (NOLOAD) :
    {
        . = ALIGN(8);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        _ebss = .;
    } > RAM

    . = ALIGN(8);
    PROVIDE(end = .); /* sbrk.c grows the C heap from here */
    _heap_end = ORIGIN(RAM) + LENGTH(RAM) - 16K; /* up to here: 16 KiB below the initial stack pointer */

    _estack = ORIGIN(RAM) + LENGTH(RAM); /* initial stack pointer */

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
