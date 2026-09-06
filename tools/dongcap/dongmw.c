/*
 * DONGMW -- talk to the dongle as a Microwire part, which is what it actually is.
 *
 * Why this and not the oracle
 * ---------------------------
 * Every probe so far has fired the picture oracle at the part from cold and got silence,
 * even with the port proven live by a pin 9 -> pin 10 jumper.  That is not surprising in
 * hindsight: the game only reaches the oracle deep inside the HASP library, with the part
 * already initialised.  Firing the innermost exchange at a device that has never been
 * addressed is not a fair test.
 *
 * notes/HANDOFF2001.md section 16 says the record path is plain Microwire -- the 93C46/93C66
 * shape -- and gives the pin map read off the library's own primitives:
 *
 *      CS   DATA bit 1        (0x38007, mask 0x02)
 *      SK   DATA bit 5        (0x380A1, mask 0x20)   the clock
 *      DI   DATA bit 6        (0x38344, mask 0x60)
 *      DO   STATUS bit 5      (0x381BA, sampled after the clock goes low again)
 *
 * and section 16.2 the frames: start bit 1, opcode 10 = READ, then an 8-bit address, then
 * 16 clocks sampling DO, MSB first.
 *
 * That is a far better test than the oracle, for one reason: we already know the answer.
 * The 2002 PT dongle is dumped in PhotoPlay2000_h5dmp/, so a correct read is checkable
 * rather than merely "something moved".
 *
 * This sweeps the handful of things section 16 leaves open -- the idle bits held on the
 * other DATA lines, address width, and clock polarity -- and reports any transaction in
 * which DO ever changes.  A single "DO moved" is the whole objective; everything after
 * that is detail.
 *
 * Build: cc -O2 -o dongmw dongmw.c        Run: sudo ./dongmw [base-in-hex]
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/io.h>

#define CS_BIT   0x02
#define SK_BIT   0x20
#define DI_BIT   0x40

static unsigned g_base = 0x378;
static unsigned char g_idle_mask;      /* bits held on every write */
static unsigned char g_latch;
static unsigned char g_idle_status;
static int g_do_moved;

static void settle(void)
{
    (void) inb(0x80);
    (void) inb(0x80);
}

static void put(unsigned char v)
{
    g_latch = (unsigned char) (v | g_idle_mask);
    outb(g_latch, g_base);
    settle();
}

static void setbit(unsigned char mask, int on)
{
    unsigned char v = (unsigned char) (on ? (g_latch | mask) : (g_latch & ~mask));

    put((unsigned char) (v & ~g_idle_mask));
}

static int sample_do(void)
{
    unsigned char s = inb(g_base + 1);

    if (s != g_idle_status)
        g_do_moved = 1;
    return (s >> 5) & 1;
}

/* one bit out: put it on DI, clock high, clock low */
static void send_bit(int b)
{
    setbit(DI_BIT, b);
    setbit(SK_BIT, 1);
    setbit(SK_BIT, 0);
}

/* one bit in: clock high, clock low, then sample (16.1: sampled after the clock drops) */
static int recv_bit(void)
{
    setbit(SK_BIT, 1);
    setbit(SK_BIT, 0);
    return sample_do();
}

static unsigned read_word(unsigned addr, int abits)
{
    unsigned v = 0;
    int i;

    put(0);
    setbit(CS_BIT, 1);
    send_bit(1);                       /* start */
    send_bit(1);                       /* opcode 10 = READ */
    send_bit(0);
    for (i = abits - 1; i >= 0; i--)
        send_bit((addr >> i) & 1);
    for (i = 0; i < 16; i++)
        v = (v << 1) | (unsigned) recv_bit();
    setbit(CS_BIT, 0);
    put(0);
    return v;
}

int main(int argc, char **argv)
{
    static const unsigned char idles[] = { 0x00, 0x80, 0x81, 0x01 };
    int im, abits, a;

    if (argc > 1)
        g_base = (unsigned) strtoul(argv[1], NULL, 16);
    if (ioperm(g_base, 3, 1) || ioperm(0x80, 1, 1)) {
        perror("ioperm");
        return 1;
    }
    g_idle_status = inb(g_base + 1);
    printf("base 0x%03X   idle STATUS %02X\n", g_base, g_idle_status);
    printf("CS=DATA1  SK=DATA5  DI=DATA6  DO=STATUS5\n\n");

    for (im = 0; im < (int) sizeof idles; im++) {
        for (abits = 6; abits <= 8; abits++) {
            unsigned w[8];
            int moved_any = 0;

            g_idle_mask = idles[im];
            for (a = 0; a < 8; a++) {
                g_do_moved = 0;
                w[a] = read_word((unsigned) a, abits);
                if (g_do_moved)
                    moved_any = 1;
            }
            printf("idle %02X  addr %d bits :", idles[im], abits);
            for (a = 0; a < 8; a++)
                printf(" %04X", w[a]);
            printf("   %s\n", moved_any ? "*** DO MOVED ***" : "(no movement)");
        }
    }

    printf("\nfinal STATUS %02X\n", inb(g_base + 1));
    return 0;
}
