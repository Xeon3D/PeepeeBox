/*
 * DONGPROBE2 -- hunt for the frame the dongle actually recognises.
 *
 * dongprobe showed STATUS frozen under every CONTROL and DATA value, on both the cabinet
 * and the Linux host, while the cabinet's own game renders its photos perfectly.  So the
 * part is alive and simply never sees a frame it accepts.
 *
 * Re-reading I.G.O. 2's FINDIT.EXE turned up two things every tool so far got wrong:
 *
 *   1. The round preamble ends with a raw write of 0x48, not 0x84 (0x3300c).  Every tool
 *      in this repo writes 0x84 -- a transposition carried since the wire was first
 *      recovered.
 *   2. The preamble is not the first thing that happens.  0x32fc2 calls an init at
 *      0x32e2f first, which writes a bare 0x80 to DATA through the raw writer, and the
 *      "seed" byte is not a constant at all: 0x32f36 computes it from state[0x18] via a
 *      table at DS:0x2f94.  Sweeping 0..256 is a stand-in for that.
 *
 * Rather than reverse the whole init chain blind, this tries the combinations against the
 * real part and reports the only thing that matters at this stage: does STATUS ever move?
 * Until some frame makes a status line change, nothing else can be measured.
 *
 * Build: cc -O2 -o dongprobe2 dongprobe2.c     Run: sudo ./dongprobe2 [base-in-hex]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/io.h>

#define POLY 0x80500062u

static unsigned g_base = 0x378;
static unsigned char g_idle;
static int g_moved;

static void io_delay(void)
{
    (void) inb(0x80);
    (void) inb(0x80);
}

/* the raw writer, FINDIT.EXE 0x32cc9 -- the byte reaches the port untouched */
static void wraw(unsigned char b)
{
    outb(b, g_base);
    io_delay();
}

/* the cooked writer, 0x32db2: bit 0 forced by the flag, bit 7 always set */
static void wcooked(unsigned char b, int flag)
{
    wraw(flag ? (unsigned char) (b | 0x81) : (unsigned char) ((b & 0xFE) | 0x80));
}

static void cmdbyte(unsigned char b)
{
    wcooked(b, 0);
    wcooked(b, 1);
    wcooked(b, 0);
}

static unsigned char rstat(void)
{
    unsigned char s = inb(g_base + 1);

    if (s != g_idle)
        g_moved = 1;
    return s;
}

static unsigned char query(unsigned char q)
{
    unsigned char pay = (unsigned char) (((q << 1) & 0x0E) | ((q << 2) & 0x60));

    pay = (unsigned char) (pay & 0xEF);
    wcooked(pay, 0);
    wcooked((unsigned char) (pay | 0x10), 0);
    wcooked(pay, 0);
    return rstat();
}

/* one round; `tail` is the byte the preamble ends on, `pre80` prepends the init write */
static uint32_t round_once(uint32_t v, unsigned char seed, unsigned char tail,
                           int pre80, int line7)
{
    unsigned char prev, st;
    int k;

    if (pre80)
        wraw(0x80);
    cmdbyte(seed);
    cmdbyte(0x4E);
    wraw(tail);

    st = query((unsigned char) (v & 0xFF));
    prev = line7 ? (unsigned char) (!((st >> 7) & 1)) : (unsigned char) ((st >> 5) & 1);
    for (k = 1; k <= 39; k++) {
        unsigned idx = (unsigned) ((prev & 1) | ((v & 1) << 1));

        v = ((idx ^ v) & 1) ? ((v >> 1) ^ POLY) : (v >> 1);
        st = query((unsigned char) ((v >> (8 * idx)) & 0xFF));
        prev = line7 ? (unsigned char) (!((st >> 7) & 1)) : (unsigned char) ((st >> 5) & 1);
    }
    return v;
}

int main(int argc, char **argv)
{
    static const unsigned char tails[] = { 0x48, 0x84, 0xC8, 0x04, 0x24 };
    static const uint32_t WANT1 = 0x32FC6611u;   /* f(504EF2AE) under 68BB/1329 */
    int ti, p8, l7, s;

    if (argc > 1)
        g_base = (unsigned) strtoul(argv[1], NULL, 16);
    if (ioperm(g_base, 3, 1) || ioperm(0x80, 1, 1)) {
        perror("ioperm");
        return 1;
    }

    g_idle = inb(g_base + 1);
    printf("base 0x%03X   idle STATUS %02X\n", g_base, g_idle);
    printf("looking for ANY frame that makes a status line move.\n\n");

    for (ti = 0; ti < (int) (sizeof tails); ti++) {
        for (p8 = 0; p8 <= 1; p8++) {
            for (l7 = 0; l7 <= 1; l7++) {
                int moved_any = 0, hit = -1;

                for (s = 0; s < 256; s++) {
                    uint32_t r;

                    g_moved = 0;
                    r = round_once(0x504EF2AE, (unsigned char) s, tails[ti], p8, l7);
                    if (g_moved)
                        moved_any = 1;
                    if (r == WANT1 && hit < 0)
                        hit = s;
                }
                printf("tail %02X  pre80 %d  line %s : STATUS moved %-3s%s\n",
                       tails[ti], p8, l7 ? "bit7inv" : "bit5   ",
                       moved_any ? "YES" : "no",
                       hit >= 0 ? "   *** SEED MATCHES 32FC6611 ***" : "");
                if (hit >= 0)
                    printf("        seed 0x%02X reproduces f(504EF2AE)\n", hit);
            }
        }
    }

    printf("\nfinal STATUS %02X (idle was %02X)\n", inb(g_base + 1), g_idle);
    return 0;
}
