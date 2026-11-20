/* newlib's _sbrk: the C heap grows in the board's RAM from `end` up to _heap_end, both
   symbols of the board's linker script (it places _heap_end below the initial stack
   pointer, so the heap cannot run into the main stack). At the limit it fails cleanly with ENOMEM instead of running
   into the main stack.

   Thread safety comes for free: _sbrk is only called from malloc, and newlib's malloc is
   serialized by the retargetable locks in ports/freertos/newlib_locks.c (a FreeRTOS mutex per lock
   object). This strong definition is linked whole (before the -lnosys that nosys.specs
   appends), so libnosys's own _sbrk is never pulled in. */
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

/* From the board's linker script. */
extern char end[];      /* end of .bss: where the heap starts */
extern char _heap_end[]; /* top of the heap, below the main stack */

caddr_t _sbrk(int incr)
{
    static char *heap;
    if (heap == NULL)
        heap = end;
    if (incr > 0 && heap + incr > _heap_end) {
        errno = ENOMEM;
        return (caddr_t)-1;
    }
    char *prev = heap;
    heap += incr; /* incr <= 0: no shrink support, the previous break is returned */
    return (caddr_t)prev;
}
