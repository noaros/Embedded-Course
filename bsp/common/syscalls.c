/*
 * newlib system call stubs.
 *
 * printf() ends up in _write(), which sends bytes to the board's debug UART.
 * _sbrk() hands out memory from the .heap section the linker script reserves
 * and fails cleanly (returns -1, errno = ENOMEM) instead of growing into the
 * stack.
 *
 * Every stub is marked `used`: with -flto the optimiser would otherwise
 * discard them before newlib's references to them are resolved.
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

#include "board.h"

#define SYSCALL __attribute__((used))

extern uint8_t _sheap[];
extern uint8_t _eheap[];

SYSCALL int _write(int fd, const char *buf, int len)
{
    (void)fd;
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            board_uart_putc('\r');
        }
        board_uart_putc(buf[i]);
    }
    return len;
}

SYSCALL int _read(int fd, char *buf, int len)
{
    (void)fd;
    int n = 0;
    while (n < len) {
        int c = board_uart_getc();
        if (c < 0) {
            break;
        }
        buf[n++] = (char)c;
    }
    return n;
}

SYSCALL void *_sbrk(ptrdiff_t incr)
{
    static uint8_t *brk = _sheap;
    if (incr < 0 || (size_t)(_eheap - brk) < (size_t)incr) {
        errno = ENOMEM;
        return (void *)-1;
    }
    uint8_t *prev = brk;
    brk += incr;
    return prev;
}

SYSCALL int _close(int fd) { (void)fd; return -1; }
SYSCALL int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; return 0; }
SYSCALL int _isatty(int fd) { (void)fd; return 1; }
SYSCALL int _getpid(void) { return 1; }
SYSCALL int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }

SYSCALL int _fstat(int fd, struct stat *st)
{
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

SYSCALL void _exit(int status)
{
    (void)status;
    __BKPT(0);
    for (;;) {
    }
}
