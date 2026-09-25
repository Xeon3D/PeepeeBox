/*
 * DONGSESSION -- a funworld HASP4 part's session layer, from Linux.
 *
 * The same 194 questions as SESSION.COM (mksession_dos.py), for the dongle host, so a
 * part can be asked without booting DOS.  What it sends is fixed and the password lives
 * in the part, so one build serves every generation: 2001 (7477/7D57), I.G.O. 3/5
 * (6B91/24A3), I.G.O. 2/6/7 (68BB/1329), and anything else plugged in.
 *
 *   1. the identity ramp       00,02 .. 7E, one STATUS read each
 *   2. the 64-step sweep       the LCG payloads (x = x*0x1989 + 5 from 100, & 0x7E)
 *   3. the same sweep, bit 7 set  (I.G.O. 3's form)
 *   4. the liveness probe      1E then 1C; a real part answers 1 then 0
 *
 * Each interrogation is repeated (-n, default 5) and every run printed, because a
 * reply that changes between runs is itself a finding: it would mean the part carries
 * state across questions and a one-shot answer is not the whole story.
 *
 * Bit order matches SESSION.COM: step 0 is the most significant bit of the first byte.
 *
 * cc -O2 -o dongsession dongsession.c && sudo ./dongsession [-n runs] [-d delay] [-p wake] [base]
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

extern int ioperm(unsigned long from, unsigned long num, int turn_on);
extern int iopl(int level);

static inline void outb_(unsigned p, uint8_t v)
{
    __asm__ __volatile__("outb %0, %w1" : : "a"(v), "d"((unsigned short) p));
}

static inline uint8_t inb_(unsigned p)
{
    uint8_t v;
    __asm__ __volatile__("inb %w1, %0" : "=a"(v) : "d"((unsigned short) p));
    return v;
}

static unsigned g_base = 0x378;
static int      g_delay = 8;    /* dummy reads of port 0x80 between write and sample */

/* The command bytes a guest clocks in (on DATA bit 0: b, b|1, b) before the part will
   answer anything.  They come from the password, so each pair has its own; the default
   is I.G.O. 2's (68BB/1329), read off the real boot in
   docs/research/evidence/igo2-dongle-wire-2026-09-06.log.gz.  -p replaces it. */
static uint8_t  g_wake[64] = { 0x46, 0x0A, 0x78, 0x5A, 0x3A, 0x14, 0x48, 0x28,
                               0x00, 0x12, 0x50, 0x30, 0x0C, 0x1E, 0x5C, 0x3C };
static int      g_nwake = 16;

/* The sweep is preceded by a different burst, and the part answers it differently: with
   only the ramp's burst, a sweep step answers the identity bit at the payload's address.
   Also I.G.O. 2's, from the same boot.  -s replaces it. */
static uint8_t  g_swake[64] = { 0x46, 0x5A, 0x58, 0x1A, 0x7A, 0x54, 0x08, 0x68,
                                0x40, 0x32, 0x40, 0x20, 0x2C, 0x16, 0x1C, 0x34 };
static int      g_nswake = 16;

/* A 68BB/1329 part's answers, in this program's bit order (see mksession_dos.py). */
static const uint64_t HD_SIGNATURE = 0xCEFF0AFFCECE0A0AULL; /* bit i is address i */
static const uint64_t HS_SWEEP_A   = 0xF57A37E78F8FBDDAULL; /* step i is bit 63-i */

static int
step(uint8_t v)
{
    outb_(g_base, v);
    for (int i = 0; i < g_delay; i++)
        (void) inb_(0x80);
    return (inb_(g_base + 1) >> 5) & 1;
}

static void
wake(const uint8_t *w, int n)
{
    for (int i = 0; i < n; i++) {
        uint8_t b = w[i] & 0xFE;
        outb_(g_base, b);
        outb_(g_base, b | 1);
        outb_(g_base, b);
    }
}

static int
parsehex(char *e, uint8_t *out)
{
    int n = 0;
    while (*e && n < 64) {
        char *x;
        unsigned long v = strtoul(e, &x, 16);
        if (x == e)
            break;
        out[n++] = (uint8_t) v;
        e = x;
        while (*e == ' ' || *e == ',')
            e++;
    }
    return n;
}

static void
put8(const char *what, const uint8_t *bits)
{
    printf("%-16s: ", what);
    for (int i = 0; i < 64; i += 8) {
        int b = 0;
        for (int k = 0; k < 8; k++)
            b = (b << 1) | bits[i + k];
        printf("%02X ", b);
    }
    printf("\n");
}

int
main(int argc, char **argv)
{
    int     runs = 5;
    int     opt;
    uint8_t lcg[64], bits[64];

    while ((opt = getopt(argc, argv, "n:d:p:s:w")) != -1) {
        if (opt == 'n')
            runs = atoi(optarg);
        else if (opt == 'd')
            g_delay = atoi(optarg);
        else if (opt == 'p')
            g_nwake = parsehex(optarg, g_wake);
        else if (opt == 's')
            g_nswake = parsehex(optarg, g_swake);
        else if (opt == 'w')
            g_nwake = g_nswake = 0;     /* no wake at all: what SESSION.COM did */
        else {
            fprintf(stderr, "usage: %s [-n runs] [-d delay] [base-hex]\n", argv[0]);
            return 2;
        }
    }
    if (optind < argc)
        g_base = (unsigned) strtoul(argv[optind], NULL, 16);

    if (g_base + 3 < 0x400 ? ioperm(g_base, 3, 1) : iopl(3)) {
        perror("port access (run as root)");
        return 1;
    }
    if (ioperm(0x80, 1, 1))
        iopl(3);

    unsigned x = 100;
    for (int i = 0; i < 64; i++) {
        x = (x * 0x1989 + 5) & 0xFFFF;
        lcg[i] = (x >> 8) & 0x7E;
    }

    printf("DONGSESSION -- funworld HASP4 session layer, port %03X, delay %d, %d runs\n\n",
           g_base, g_delay, runs);

    /* CTRL is left alone, as SESSION.COM leaves it: only DATA and STATUS are used. */
    for (int r = 0; r < runs; r++) {
        printf("run %d\n", r + 1);
        wake(g_wake, g_nwake);

        for (int a = 0; a < 64; a++)
            bits[a] = step((uint8_t) (a << 1));
        put8("identity ramp", bits);

        wake(g_swake, g_nswake);
        for (int i = 0; i < 64; i++)
            bits[i] = step(lcg[i]);
        put8("sweep bit7=0", bits);

        wake(g_swake, g_nswake);
        for (int i = 0; i < 64; i++)
            bits[i] = step(lcg[i] | 0x80);
        put8("sweep bit7=1", bits);

        int l1 = step(0x1E);
        int l2 = step(0x1C);
        printf("%-16s: %d %d\n\n", "liveness 1E,1C", l1, l2);
    }

    for (int a = 0; a < 64; a++)
        bits[a] = (HD_SIGNATURE >> a) & 1;
    printf("A 68BB/1329 part answered:\n");
    put8("identity ramp", bits);
    for (int i = 0; i < 64; i++)
        bits[i] = (HS_SWEEP_A >> (63 - i)) & 1;
    put8("sweep bit7=0", bits);
    printf("%-16s: 1 0\n", "liveness 1E,1C");
    return 0;
}
