/*
 * PPTRACE -- log a process's parallel-port ioctls, cheaply.
 *
 * The obvious way to capture what QEMU sends to /dev/parport0 is
 * `strace -e trace=ioctl -p <qemu>`, and that works, but it stops the process on EVERY
 * ioctl -- including the display's, which on a GTK window is thousands a second.  On a
 * 1.6 GHz Atom with no KVM that dominates everything and makes a DOS boot crawl.
 *
 * This does the same job by interposing ioctl() instead: the fast path is one comparison
 * on the request's type byte, and only ppdev calls ('p', per linux/ppdev.h) are written
 * out.  Everything else is passed straight through.
 *
 *   cc -O2 -shared -fPIC -o pptrace.so pptrace.c -ldl
 *   PPTRACE_LOG=/root/dong/pp.log LD_PRELOAD=/root/dong/pptrace.so qemu-system-i386 ...
 *
 * Output is one line per access:
 *   <seconds since start> W DATA 8f
 *   <seconds since start> R STATUS 78
 *
 * which is the wire, in order, with values -- the trace this project has never had.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>

static int (*real_ioctl)(int, unsigned long, ...);
static FILE *g_log;
static double g_t0;

static double now(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void __attribute__((constructor)) init(void)
{
    const char *p = getenv("PPTRACE_LOG");

    real_ioctl = (int (*)(int, unsigned long, ...)) dlsym(RTLD_NEXT, "ioctl");
    g_t0 = now();
    if (p) {
        g_log = fopen(p, "w");
        if (g_log)
            setvbuf(g_log, NULL, _IOLBF, 0);
    }
}

/* the ppdev request numbers we care about, from linux/ppdev.h */
static const char *ppname(unsigned long req)
{
    switch (_IOC_NR(req)) {
    case 0x80: return "SETMODE";
    case 0x81: return "RSTATUS";
    case 0x83: return "RCONTROL";
    case 0x84: return "WCONTROL";
    case 0x85: return "RDATA";
    case 0x86: return "WDATA";
    case 0x8b: return "CLAIM";
    case 0x8c: return "RELEASE";
    case 0x90: return "NEGOT";
    case 0x91: return "SETPHASE";
    case 0x9e: return "DATADIR";
    default:   return NULL;
    }
}

int ioctl(int fd, unsigned long req, ...)
{
    va_list ap;
    void *arg;
    int r;

    va_start(ap, req);
    arg = va_arg(ap, void *);
    va_end(ap);

    r = real_ioctl(fd, req, arg);

    /* fast path: everything that is not a ppdev call leaves here immediately */
    if (g_log && _IOC_TYPE(req) == 'p') {
        const char *n = ppname(req);

        if (n) {
            unsigned v = 0;

            if (arg && _IOC_SIZE(req) == 1)
                v = *(unsigned char *) arg;
            else if (arg && _IOC_SIZE(req) == 4)
                v = (unsigned) *(int *) arg;
            fprintf(g_log, "%.6f %c %-8s %02x\n", now() - g_t0,
                    (_IOC_DIR(req) & _IOC_READ) ? 'R' : 'W', n, v);
        }
    }
    return r;
}
