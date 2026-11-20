/* Semihosting-based logging/exit for the QEMU firmware (ports/qemu_mps2). */
#ifndef QEMU_LOG_H
#define QEMU_LOG_H

void qemu_log(const char *s);
void exit_qemu(int code) __attribute__((noreturn));

#endif /* QEMU_LOG_H */
