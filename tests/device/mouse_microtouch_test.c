/*
 * Device-level test of the 3M MicroTouch serial controller
 * (src/device/mouse_microtouch_touchscreen.c) against the MicroTouch Touch
 * Controllers Reference Guide. The serial port, timers and host mouse are
 * mocked: commands go in byte by byte, the controller's bytes are captured,
 * and each byte slot of the controller's transmit timer is stepped by hand.
 *
 * Build and run (MSYS2 mingw64, from the repository root):
 *   cc -std=gnu11 -DCMAKE -Isrc/include -Ibuild/src/include -Isrc/cpu \
 *      tests/device/mouse_microtouch_test.c src/device/mouse_microtouch_touchscreen.c \
 *      src/utils/fifo8.c -o build/mouse_microtouch_test.exe && build/mouse_microtouch_test.exe
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/mouse.h>
#include <86box/serial.h>
#include <86box/fifo8.h>
#include <86box/video.h>
#include <86box/nvr.h>

extern void *mtouch_init(const device_t *info);
extern void  mtouch_close(void *priv);

/* ---- mocks ------------------------------------------------------------------ */

monitor_t monitors[MONITORS_NUM];
int       enable_overscan;
int       mouse_tablet_in_proximity;

static serial_t   port;
static void     (*dev_write)(serial_t *, void *, uint8_t);
static void      *dev_priv;
static int      (*poll_fn)(void *);
static void      *poll_priv;
static uint8_t    out[4096];
static int        out_len;
static int        g_but, g_pressed, g_identity = 3;
static double     g_x = 0.5, g_y = 0.5;
static char       nvr_dir[] = "build";

#define MAX_TIMERS 8
static pc_timer_t *timers[MAX_TIMERS];
static int         timer_on[MAX_TIMERS];
static int         ntimers;

void
pclog(const char *fmt, ...)
{
    (void) fmt;
}

void
fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(2);
}

int
device_get_config_int(const char *name)
{
    if (!strcmp(name, "identity"))
        return g_identity;
    if (!strcmp(name, "irq"))
        return -1; /* leave the port's IRQ alone */
    if (!strcmp(name, "speed"))
        return 9600;
    return 0;
}

void serial_irq(serial_t *s, uint8_t irq) { (void) s; (void) irq; }

FILE *
nvr_fopen(char *str, char *mode)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/test_%s", nvr_dir, str);
    return fopen(path, mode);
}

serial_t *
serial_attach_ex(int p, void (*rcr)(serial_t *, void *), void (*w)(serial_t *, void *, uint8_t),
                 void (*tp)(serial_t *, void *, double), void (*lcr)(serial_t *, void *, uint8_t), void *priv)
{
    (void) p; (void) rcr; (void) tp; (void) lcr;
    memset(&port, 0, sizeof(port));
    port.out_new = 0xffff;
    dev_write    = w;
    dev_priv     = priv;
    return &port;
}

void serial_set_cts(serial_t *s, uint8_t v) { (void) s; (void) v; }
void serial_set_dsr(serial_t *s, uint8_t v) { (void) s; (void) v; }
void serial_set_dcd(serial_t *s, uint8_t v) { (void) s; (void) v; }

void
serial_write_fifo(serial_t *s, uint8_t dat)
{
    (void) s;
    if (out_len < (int) sizeof(out))
        out[out_len++] = dat;
}

int fifo_get_full(void *priv) { (void) priv; return 0; }

int
tablet_get_buttons_ex(void)
{
    return g_but;
}

int
tablet_take_pressed(void)
{
    int b     = g_pressed;
    g_pressed = 0;
    return b;
}

void
mouse_get_abs_coords(double *x, double *y)
{
    *x = g_x;
    *y = g_y;
}

void mouse_set_buttons(int b) { (void) b; }

void
mouse_set_poll_ex(int (*f)(void *), void *arg)
{
    poll_fn   = f;
    poll_priv = arg;
}

void
timer_add(pc_timer_t *timer, void (*callback)(void *priv), void *priv, int start_timer)
{
    timer->callback   = callback;
    timer->priv       = priv;
    timers[ntimers]   = timer;
    timer_on[ntimers] = start_timer;
    ntimers++;
}

static int
timer_index(pc_timer_t *t)
{
    for (int i = 0; i < ntimers; i++)
        if (timers[i] == t)
            return i;
    return -1;
}

void
timer_on_auto(pc_timer_t *timer, double period)
{
    int i = timer_index(timer);
    if (i >= 0) {
        timer_on[i]   = 1;
        timer->period = period;
    }
}

void
timer_stop(pc_timer_t *timer)
{
    int i = timer_index(timer);
    if (i >= 0)
        timer_on[i] = 0;
}

/* ---- driving the device ----------------------------------------------------- */

static int slot; /* byte slots so far (~1 ms each at 9600 baud) */

/* One byte slot: the transmit timer fires; every 10 slots the 100 Hz mouse poll;
   the reset timer fires after 500 slots (its 500 ms). */
static void
step(int n)
{
    static int reset_left = -1;
    for (int k = 0; k < n; k++, slot++) {
        if ((slot % 10) == 0)
            poll_fn(poll_priv);
        /* timers[0] = transmit, timers[1] = reset */
        if (timer_on[1]) {
            if (reset_left < 0)
                reset_left = 500;
            if (--reset_left == 0) {
                timer_on[1] = 0;
                reset_left  = -1;
                timers[1]->callback(timers[1]->priv);
            }
        }
        if (timer_on[0]) {
            timer_on[0] = 0;
            timers[0]->callback(timers[0]->priv);
        }
    }
}

static void
send(const char *cmd)
{
    dev_write(&port, dev_priv, 0x01);
    for (const char *p = cmd; *p; p++)
        dev_write(&port, dev_priv, (uint8_t) *p);
    dev_write(&port, dev_priv, 0x0d);
}

static void
clear(void)
{
    out_len = 0;
}

/* Send a command and return what came back within `slots` byte slots. */
static int
cmd(const char *c, int slots)
{
    clear();
    send(c);
    step(slots);
    return out_len;
}

static void
touch(double x, double y, int ms)
{
    g_x   = x;
    g_y   = y;
    g_but = 1;
    step(ms);
    g_but = 0;
}

/* ---- checks ----------------------------------------------------------------- */

static int failures, checks;

static void
dump(void)
{
    fprintf(stderr, "      got %d bytes:", out_len);
    for (int i = 0; i < out_len && i < 48; i++)
        fprintf(stderr, " %02X", out[i]);
    fprintf(stderr, "%s\n", (out_len > 48) ? " ..." : "");
}

#define CHECK(cond, what)                                   \
    do {                                                    \
        checks++;                                           \
        if (!(cond)) {                                      \
            failures++;                                     \
            fprintf(stderr, "FAIL %s (line %d)\n", what, __LINE__); \
            dump();                                         \
        } else                                              \
            printf("ok   %s\n", what);                      \
    } while (0)

static int
out_is(const char *bytes, int n)
{
    return (out_len == n) && !memcmp(out, bytes, n);
}

/* Count Tablet packets in out[] with a given status byte. */
static int
count_status(uint8_t status)
{
    int n = 0;
    for (int i = 0; i < out_len; i++)
        if (out[i] == status)
            n++;
    return n;
}

static void *
fresh(int identity)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/test_mtouch_%s.nvr", nvr_dir, (identity == 2) ? "P50100" : ((identity == 3) ? "Q10100" : ((identity == 1) ? "A40100" : "A30100")));
    remove(path);
    g_identity = identity;
    ntimers    = 0;
    g_but = g_pressed = 0;
    return mtouch_init(NULL);
}

static void *
reopen(void *dev, int identity)
{
    mtouch_close(dev);
    g_identity = identity;
    ntimers    = 0;
    return mtouch_init(NULL);
}

int
main(void)
{
    void *dev = fresh(3); /* Q1: Serial/SMT3 */

    /* Reset and identity */
    cmd("R", 600);
    CHECK(out_is("\x01" "0\r", 3), "R answers <SOH>0<CR> after the reset time");
    cmd("OI", 20);
    CHECK(out_is("\x01Q10100\r", 8), "OI = Q10100");
    cmd("OS", 20);
    CHECK(out_is("\x01\x40\x60\r", 4), "OS after a reset: 40 60");
    cmd("Z", 20);
    CHECK(out_is("\x01" "0\r", 3), "Z (null command)");
    cmd("NM", 20);
    CHECK(out_is("\x01" "0\r", 3), "NM (TouchWare, undocumented) answers 0");
    cmd("UT", 20);
    CHECK(out_is("\x01QM****00\r", 10), "UT = QM****00");

    /* Format Tablet, Mode Stream */
    cmd("FT", 20);
    cmd("MS", 20);
    clear();
    touch(0.25, 0.5, 60);
    step(30);
    CHECK(out_len >= 15 && (out_len % 5) == 0, "Tablet stream: whole 5-byte packets");
    CHECK(out[0] == 0xC8, "Tablet touchdown status C8");
    CHECK(out[out_len - 5] == 0x88, "Tablet liftoff status 88 (last packet)");
    CHECK(count_status(0x88) == 1, "exactly one liftoff");
    {
        int x = out[1] | (out[2] << 7), y = out[3] | (out[4] << 7);
        CHECK(x == 4095 && y == 8191, "Tablet coordinates 14-bit, y from the bottom");
    }

    /* Mode Point: only the touchdown */
    cmd("MP", 20);
    clear();
    touch(0.5, 0.5, 60);
    step(30);
    CHECK(out_len == 5 && out[0] == 0xC8, "Mode Point: one touchdown packet");

    /* Mode Down/Up: touchdown and liftoff */
    cmd("MDU", 20);
    clear();
    touch(0.5, 0.5, 60);
    step(30);
    CHECK(out_len == 10 && out[0] == 0xC8 && out[5] == 0x88, "Mode Down/Up: touchdown + liftoff");

    /* Mode Inactive */
    cmd("MI", 20);
    clear();
    touch(0.5, 0.5, 60);
    step(30);
    CHECK(out_len == 0, "Mode Inactive: nothing");

    /* Quick tap between two polls still gives touchdown + liftoff */
    cmd("MS", 20);
    clear();
    g_pressed = 1; /* pressed and released since the last poll */
    step(40);
    CHECK(count_status(0xC8) >= 1 && count_status(0x88) == 1, "quick tap between polls: touchdown and liftoff");

    /* Format Decimal (+ Mode Status) */
    cmd("FD", 20);
    clear();
    touch(0.25, 0.5, 25);
    step(40);
    CHECK(out_len >= 9 && !memcmp(out, "\x01" "249,499\r", 9), "Decimal: <SOH>249,499<CR>");
    cmd("MT", 20);
    clear();
    touch(0.25, 0.5, 25);
    step(40);
    CHECK(out[0] == 0x19 && out[out_len - 9] == 0x18, "Mode Status: ^Y touchdown ... ^R liftoff");
    CHECK(out_len >= 27 && out[9] == 0x1C, "Mode Status: ^\\ continued touch");

    /* Format Hexadecimal resets Mode Status */
    cmd("FH", 20);
    clear();
    touch(1.0, 0.0, 15);
    step(40);
    CHECK(out_len >= 9 && !memcmp(out, "\x01" "3FF,3FF\r", 9), "Hexadecimal: <SOH>3FF,3FF<CR>, Mode Status off");

    /* Format Binary: Mode Polled + Mode Status; packets only on XON */
    cmd("FB", 20);
    clear();
    g_x = 0.5; g_y = 0.5; g_but = 1;
    step(40);
    CHECK(out_len == 0, "Binary (polled): nothing without XON");
    dev_write(&port, dev_priv, 0x11);
    step(20);
    CHECK(out_len == 10 && !memcmp(out, "\x17\x20\x20\x20\x20\x19", 6), "Binary: XON -> 17 20 20 20 20 + ^Y packet");
    {
        int x = ((out[6] & 0x1f) << 5) | (out[7] & 0x1f);
        CHECK((out[6] & 0x20) && (out[7] & 0x20) && x == 511, "Binary coordinates 10-bit in 5-bit groups");
    }
    clear();
    dev_write(&port, dev_priv, 0x11);
    step(20);
    CHECK(out_len == 5 && out[0] == 0x1C, "Binary: XON while touching -> ^\\ continued");
    g_but = 0;
    step(20);
    clear();
    step(20);
    CHECK(out_len == 0, "Binary: liftoff held until XON");
    dev_write(&port, dev_priv, 0x11);
    step(20);
    CHECK(out_len == 5 && out[0] == 0x18, "Binary: XON -> ^R liftoff");

    /* Format Binary Stream */
    cmd("FBS", 20);
    clear();
    touch(0.5, 0.5, 30);
    step(30);
    CHECK(out_len >= 15 && out[0] == 0x17 && out[5] == 0x19 && out[out_len - 5] == 0x18, "Binary Stream: streams, ^Y ... ^R");

    /* Format Zone: D/B/A inside the calibrated area */
    cmd("FZ", 20);
    clear();
    touch(0.5, 0.5, 30);
    step(30);
    CHECK(out[0] == 'D' && out[5] == 'B' && out[out_len - 5] == 'A', "Zone: D touchdown, B continued, A liftoff");

    /* Format Raw: 7-byte corner packets, bit 7 on the first byte only */
    cmd("FR", 20);
    clear();
    step(30);
    CHECK(out_len >= 14 && (out[0] & 0x80) && !(out[1] & 0x88) && (out[7] & 0x80), "Raw: 7-byte packets synchronised by bit 7");
    cmd("R", 600);
    clear();
    step(30);
    CHECK(out_len == 0, "Reset ends Format Raw");

    /* Calibrate Extended in Tablet: too short -> 2, wrong place -> 0, then 1, 1 */
    cmd("FT", 20);
    cmd("MS", 20);
    cmd("CX", 20);
    CHECK(out_is("\x01" "0\r", 3), "CX acknowledged");
    clear();
    g_pressed = 1;
    step(40);
    CHECK(out_is("\x01" "2\r", 3), "CX: too short a touch -> 2 (SMT3)");
    clear();
    touch(0.5, 0.5, 100);
    step(20);
    CHECK(out_is("\x01" "0\r", 3), "CX: touch off the target -> 0");
    /* the raw screen is offset: targets land at 0.2/0.8 instead of 0.125/0.875 */
    clear();
    touch(0.2, 0.8, 100);
    step(20);
    CHECK(out_is("\x01" "1\r", 3), "CX: lower left accepted");
    clear();
    touch(0.8, 0.2, 100);
    step(20);
    CHECK(out_is("\x01" "1\r", 3), "CX: upper right accepted");
    clear();
    touch(0.2, 0.8, 30);
    step(20);
    {
        int x = out[1] | (out[2] << 7), y = out[3] | (out[4] << 7);
        CHECK(abs(x - 2048) < 20 && abs(y - 2048) < 20, "after CX the lower-left target reports 1/8, 1/8");
    }
    /* Format Zone: outside the calibrated area -> L/J/I */
    cmd("FZ", 20);
    clear();
    touch(0.05, 0.5, 30);
    step(30);
    CHECK(out[0] == 'L' && out[out_len - 5] == 'I', "Zone outside the calibrated area: L ... I");

    /* Calibrate New in Decimal: upper right positive answer is 0 */
    cmd("FD", 20);
    cmd("CN", 20);
    clear();
    touch(0.02, 0.98, 100);
    step(20);
    CHECK(out_is("\x01" "1\r", 3), "CN (Decimal): lower left positive = 1");
    clear();
    touch(0.98, 0.02, 100);
    step(20);
    CHECK(out_is("\x01" "0\r", 3), "CN (Decimal): upper right positive = 0");

    /* Parameter Lock stores format and mode; a new power-up uses them */
    cmd("FH", 20);
    cmd("MDU", 20);
    cmd("PL", 20);
    dev = reopen(dev, 3);
    clear();
    touch(0.5, 0.5, 40);
    step(30);
    CHECK(out_len == 18 && out[0] == 0x01 && out[4] == ',', "after PL + power cycle: Hexadecimal, Down/Up");

    /* Restore Defaults: Tablet, Stream, factory calibration */
    cmd("RD", 600);
    CHECK(out_is("\x01" "0\r", 3), "RD answers 0 after the reset");
    clear();
    touch(0.25, 0.5, 30);
    step(30);
    {
        int x = out[1] | (out[2] << 7);
        CHECK(out[0] == 0xC8 && x == 4095, "after RD: Tablet, factory calibration");
    }

    /* Parameter Set */
    cmd("PN812", 20);
    CHECK(out_is("\x01" "0\r", 3), "Parameter Set PN812 answered");

    /* SMT2 (A3): Decimal by default; OS power-on flag once */
    dev = reopen(dev, 0);
    dev = reopen(dev, 0);
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/test_mtouch_A30100.nvr", nvr_dir);
        remove(path);
    }
    dev = reopen(dev, 0);
    cmd("OS", 20);
    CHECK(out_is("\x01\x60\x40\r", 4), "SMT2 OS at power-on: power-on flag set");
    cmd("OS", 20);
    CHECK(out_is("\x01\x40\x40\r", 4), "SMT2 OS again: flag cleared");
    clear();
    touch(0.5, 0.5, 20);
    step(40);
    CHECK(out_len >= 9 && out[0] == 0x01 && out[4] == ',', "SMT2 default format: Decimal");

    /* TouchPen: UT reset flag */
    dev = fresh(2);
    cmd("UT", 20);
    CHECK(out_is("\x01TP****20\r", 10), "TouchPen UT after power-up: reset flag 20");
    cmd("UT", 20);
    CHECK(out_is("\x01TP****00\r", 10), "TouchPen UT again: 00");

    mtouch_close(dev);
    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
