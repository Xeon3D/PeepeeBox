/*
 * DONGWIRE -- run a script of parallel-port accesses against a real part.
 *
 * The general tool behind the specific ones: a script is cut out of a wire trace, or
 * generated, on the workstation; this executes it on the dongle host and reports every
 * STATUS read, beside the value the script expected when it gives one.
 *
 * Script, one op per line, '#' to end of line is a comment:
 *
 *      W xx          write DATA
 *      C xx          write CONTROL
 *      S [xx]        read STATUS; with xx, compare the masked bit (-m, default 20) against it
 *      D n           n dummy reads of port 0x80 (about a microsecond each)
 *      M text        print a marker line
 *
 * Output, one line per STATUS read:  "<n> <value> [<expected> ok|BAD]", then a summary.
 * With -q only the markers, mismatches and the summary are printed, and -b packs the
 * reads of each marked section into hex bytes, MSB first, for reading answers by eye.
 *
 * cc -O2 -o dongwire dongwire.c && sudo ./dongwire [-q] [-b] [-d delay] [-m mask] [base] < script
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
static int      g_delay = 2;    /* dummy reads after every write */
static int      g_quiet, g_bytes;
static int      g_mask = 0x20;  /* the STATUS bit compared and packed: -m 40 for CDONGLE */

static int      bitn, bitacc;
static char     bitline[4096];

static void
flushbits(void)
{
    if (!g_bytes || !bitn)
        return;
    if (bitn % 8) {
        size_t l = strlen(bitline);
        snprintf(bitline + l, sizeof bitline - l, "%02X(%d) ", bitacc << (8 - bitn % 8),
                 bitn % 8);
    }
    printf("  bits: %s\n", bitline);
    bitn = bitacc = 0;
    bitline[0] = 0;
}

int
main(int argc, char **argv)
{
    char     line[512];
    long     nreads = 0, ncmp = 0, nbad = 0, lineno = 0;
    int      opt;

    while ((opt = getopt(argc, argv, "qbd:m:")) != -1) {
        if (opt == 'q')
            g_quiet = 1;
        else if (opt == 'b')
            g_bytes = 1;
        else if (opt == 'd')
            g_delay = atoi(optarg);
        else if (opt == 'm')
            g_mask = (int) strtoul(optarg, NULL, 16);
        else {
            fprintf(stderr, "usage: %s [-q] [-b] [-d delay] [-m mask] [base-hex] < script\n",
                    argv[0]);
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

    while (fgets(line, sizeof line, stdin)) {
        char    *c = strchr(line, '#');
        char     op;
        unsigned v;
        int      n;

        lineno++;
        if (c)
            *c = 0;
        c = line;
        while (*c == ' ' || *c == '\t')
            c++;
        op = *c;
        if (!op || op == '\n' || op == '\r')
            continue;
        c++;
        switch (op) {
        case 'W':
        case 'C':
            if (sscanf(c, "%x", &v) != 1)
                goto bad;
            outb_(g_base + (op == 'C' ? 2 : 0), (uint8_t) v);
            for (int i = 0; i < g_delay; i++)
                (void) inb_(0x80);
            break;
        case 'S': {
            uint8_t st = inb_(g_base + 1);
            int     b = (st & g_mask) ? 1 : 0;

            nreads++;
            bitacc = (bitacc << 1) | b;
            if (++bitn % 8 == 0) {
                size_t l = strlen(bitline);
                if (l < sizeof bitline - 4)
                    snprintf(bitline + l, sizeof bitline - l, "%02X ", bitacc & 0xFF);
                bitacc = 0;
            }
            n = sscanf(c, "%x", &v);
            if (n == 1) {
                int ok = (((v & g_mask) ? 1 : 0) == b);
                ncmp++;
                nbad += !ok;
                if (!g_quiet || !ok)
                    printf("%ld %02X %02X %s\n", nreads, st, v, ok ? "ok" : "BAD");
            } else if (!g_quiet)
                printf("%ld %02X\n", nreads, st);
            break;
        }
        case 'D':
            if (sscanf(c, "%d", &n) != 1)
                goto bad;
            for (int i = 0; i < n; i++)
                (void) inb_(0x80);
            break;
        case 'M':
            flushbits();
            while (*c == ' ')
                c++;
            c[strcspn(c, "\r\n")] = 0;
            printf("== %s\n", c);
            break;
        default:
bad:
            fprintf(stderr, "line %ld: cannot parse\n", lineno);
            return 2;
        }
    }
    flushbits();
    printf("reads %ld, compared %ld, bit 5 disagrees on %ld\n", nreads, ncmp, nbad);
    return nbad ? 1 : 0;
}
