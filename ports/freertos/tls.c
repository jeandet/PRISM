/* Per-thread TLS blocks for arm-none-eabi native TLS (local-exec model).
   GCC emits calls to __aeabi_read_tp (tls_support.S), which returns the block
   address kept in the thread's control block.

   ARM EABI convention used: the thread pointer points 8 bytes BEFORE the TLS
   data (a two-word TCB header), i.e. the first .tdata variable is at TP+8.
   So each block is laid out as [8-byte TCB][tdata][tbss]. */
#include "posix_internal.h"

/* TLS template exported by the linker script (ports/qemu_mps2/mps2_an500.ld). */
extern char _tdata_start[];
extern char _tdata_end[];
extern char _tbss_end[];
extern char _tdata_lma[]; /* load address of the .tdata image */

void posix_tls_setup(struct posix_thread *t) {
    size_t tdata_size = (size_t)(_tdata_end - _tdata_start);
    size_t span = (size_t)(_tbss_end - _tdata_start); /* tdata + alignment gap + tbss */
    uint8_t *block = posix_alloc(8 + span);
    if (!block) {
        /* No memory for TLS: nothing sensible left to do. */
        for (;;) { }
    }
    memset(block, 0, 8 + span); /* TCB, the gap and .tbss are zero */
    memcpy(block + 8, _tdata_lma, tdata_size);
    t->tls = block; /* this is the thread pointer value */
}

void *posix_current_tp(void) {
    struct posix_thread *t = posix_current_thread();
    return t ? t->tls : NULL;
}
