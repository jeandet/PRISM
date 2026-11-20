/* Semihosting (bkpt 0xAB) for the QEMU mps2-an500 firmware: SYS_WRITE0 for
   logging, SYS_WRITE for newlib's _write (fd 1/2), SYS_EXIT_EXTENDED so the
   test's exit code reaches the host as QEMU's exit status. */
#include <stddef.h>
#include <stdint.h>

#include "qemu_log.h"

#define SYS_WRITEC          0x03
#define SYS_WRITE0          0x04
#define SYS_WRITE           0x05
#define SYS_EXIT            0x18
#define SYS_EXIT_EXTENDED   0x20

static int semihost(int op, void *arg) {
    register int r0 __asm("r0") = op;
    register void *r1 __asm("r1") = arg;
    __asm volatile ("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}

void qemu_log(const char *s) { (void)semihost(SYS_WRITE0, (void *)s); }

void exit_qemu(int code) {
    /* SYS_EXIT_EXTENDED takes a block {reason, subcode}; ADP_Stopped_ApplicationExit = 0x20026. */
    unsigned long block[2] = { 0x20026ul, (unsigned long)code };
    (void)semihost(SYS_EXIT_EXTENDED, block);
    for (;;) { } /* noreturn */
}

/* newlib's stdio calls _write_r -> _write. libnosys.a provides a failing _write, but this
   strong definition sits in an object linked before libnosys, so it wins. QEMU does not
   pre-open semihosting handles for fds 1/2, so SYS_WRITE on them goes nowhere: the bytes go
   to the console with SYS_WRITE0, in NUL-terminated chunks. */
int _write(int fd, const void *buf, size_t len) {
    if (fd != 1 && fd != 2) return -1;
    const char *p = buf;
    char chunk[65];
    for (size_t done = 0; done < len;) {
        size_t n = len - done < 64 ? len - done : 64;
        for (size_t i = 0; i < n; ++i) chunk[i] = p[done + i] ? p[done + i] : ' ';
        chunk[n] = '\0';
        (void)semihost(SYS_WRITE0, chunk);
        done += n;
    }
    return (int)len;
}
