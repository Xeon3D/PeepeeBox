/*
 * PPLOOP -- does this parallel port's connector actually reach the chip?
 *
 * The registers on a Super I/O answer whether or not anything is wired to the DB25, so a
 * clean DATA readback proves nothing about the pins.  This drives each output line on its
 * own and reports which input lines follow it, which turns a single jumper wire into an
 * unambiguous answer.
 *
 * Put a wire between ANY data pin and ANY status pin and run it -- the matrix says which
 * pair it found, so the jumper does not have to go anywhere in particular.
 *
 *   DB25   pin 2..9   = DATA 0..7
 *          pin 10     = ACK    -> STATUS bit 6
 *          pin 11     = BUSY   -> STATUS bit 7 (inverted by the port)
 *          pin 12     = PE     -> STATUS bit 5   <- the line the dongle answers on
 *          pin 13     = SELECT -> STATUS bit 4
 *          pin 15     = ERROR  -> STATUS bit 3
 *          pin 18..25 = ground
 *
 * A good first jumper is pin 2 to pin 12: DATA bit 0 to the dongle's own answer line.
 *
 * Build: cc -O2 -o pploop pploop.c      Run: sudo ./pploop [base-in-hex]
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/io.h>

static unsigned g_base = 0x378;

static void settle(void)
{
    int i;

    for (i = 0; i < 8; i++)
        (void) inb(0x80);
}

static unsigned char stat_after_data(unsigned char v)
{
    outb(v, g_base);
    settle();
    return inb(g_base + 1);
}

static unsigned char stat_after_ctrl(unsigned char v)
{
    outb(v, g_base + 2);
    settle();
    return inb(g_base + 1);
}

static void show(const char *what, int bit, const char *pin, unsigned char diff)
{
    int b;

    printf("  %s bit %d (pin %-2s) ->", what, bit, pin);
    if (!diff) {
        printf(" nothing follows it\n");
        return;
    }
    for (b = 7; b >= 0; b--)
        if (diff & (1 << b))
            printf(" STATUS bit %d%s", b,
                   b == 5 ? " (PE, pin 12 -- the dongle's answer line)" :
                   b == 7 ? " (BUSY, pin 11)" :
                   b == 6 ? " (ACK, pin 10)" :
                   b == 4 ? " (SELECT, pin 13)" :
                   b == 3 ? " (ERROR, pin 15)" : "");
    printf("   *** CONNECTED ***\n");
}

int main(int argc, char **argv)
{
    static const char *dpin[8] = { "2", "3", "4", "5", "6", "7", "8", "9" };
    static const char *cpin[4] = { "1", "14", "16", "17" };
    unsigned char ctrl0;
    unsigned char idle;
    int i, any = 0;

    if (argc > 1)
        g_base = (unsigned) strtoul(argv[1], NULL, 16);
    if (ioperm(g_base, 3, 1) || ioperm(0x80, 1, 1)) {
        perror("ioperm");
        return 1;
    }
    ctrl0 = inb(g_base + 2);
    idle = inb(g_base + 1);
    printf("base 0x%03X   CONTROL %02X   idle STATUS %02X\n\n", g_base, ctrl0, idle);

    printf("DATA lines:\n");
    for (i = 0; i < 8; i++) {
        unsigned char lo = stat_after_data((unsigned char) (0x00));
        unsigned char hi = stat_after_data((unsigned char) (1u << i));
        unsigned char lo2 = stat_after_data((unsigned char) (0xFF & ~(1u << i)));
        unsigned char hi2 = stat_after_data(0xFF);
        unsigned char diff = (unsigned char) ((lo ^ hi) | (lo2 ^ hi2));

        show("DATA", i, dpin[i], diff);
        any |= diff;
    }

    printf("\nCONTROL lines:\n");
    for (i = 0; i < 4; i++) {
        unsigned char lo = stat_after_ctrl((unsigned char) (ctrl0 & ~(1u << i)));
        unsigned char hi = stat_after_ctrl((unsigned char) (ctrl0 | (1u << i)));
        unsigned char diff = (unsigned char) (lo ^ hi);

        show("CONTROL", i, cpin[i], diff);
        any |= diff;
    }

    outb(ctrl0, g_base + 2);
    outb(0x00, g_base);

    printf("\n");
    if (any)
        printf("VERDICT: the connector is live -- at least one pin pair is wired through.\n"
               "         With a jumper fitted this is the expected result; with NOTHING\n"
               "         fitted it means a device is driving a status line.\n");
    else
        printf("VERDICT: nothing on any input line moved for any output line.\n"
               "         With a jumper fitted between a data pin and a status pin, that\n"
               "         means the DB25 is not connected to the chip -- a header/bracket\n"
               "         wiring problem, not a dongle or protocol problem.\n");
    return 0;
}
