/*
 * DONGREPLAY -- replay the captured bring-up, then ask the part a question.
 *
 * dongcap emits the right preamble and the right query framing and still gets a line that
 * never moves, and neither contention nor timing explains it (docs/research/30 § 9.5).
 * What it cannot fake is the state the game had already put the part into: the picture
 * path starts 97,602 accesses into a session, after the BIOS has probed LPT and the HASP
 * library has done its own initialisation.
 *
 * So stop reconstructing that and replay it.  replay.bin is every port access the capture
 * recorded before the first picture round, in order:
 *
 *      op 0  write DATA val      op 2  read CONTROL
 *      op 1  read STATUS         op 3  write CONTROL val
 *
 * Read values are discarded -- we are reproducing the sequence the part saw, not the
 * conversation.  Then one keyed round of a known input says whether it worked.
 *
 * If this answers 32FC6611, the bring-up is sufficient and bisecting the prefix will find
 * the minimum that is.  If it still answers BDC587AC, then replaying the wire is not
 * enough and something outside the DATA/STATUS/CONTROL registers is involved.
 *
 * cc -O2 -o dongreplay dongreplay.c && sudo ./dongreplay [base] [prefix-fraction]
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

extern int ioperm(unsigned long from, unsigned long num, int turn_on);

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

#define POLY 0x80500062u

static unsigned g_base = 0x378;

static void raw(uint8_t b)
{
    int i;

    for (i = 0; i < 4; i++) {          /* the transport's repeat count */
        outb_(g_base, b);
        (void) inb_(0x80);
        (void) inb_(0x80);
    }
}

static void cmdbyte(uint8_t b)
{
    raw((uint8_t) ((b & 0xFE) | 0x80));
    raw((uint8_t) (b | 0x81));
    raw((uint8_t) ((b & 0xFE) | 0x80));
}

static uint8_t query(uint8_t q)
{
    uint8_t pay = (uint8_t) (((q << 1) & 0x0E) | ((q << 2) & 0x60) | 0x80);

    raw(pay);
    raw((uint8_t) (pay | 0x10));
    raw(pay);
    return (uint8_t) ((inb_(g_base + 1) >> 5) & 1);
}

/* The whole preamble, off the wire and identical in all 118 captured rounds: eighteen
   command bytes, sixteen SK pulses, one more command byte.  docs/research/30 § 9.3 had
   only the last three command bytes -- the first fifteen were missing. */
static const uint8_t pre_cmds[18] = {
    0x46, 0x5A, 0x68, 0x7A, 0x3E, 0x34, 0x58, 0x38, 0x20,
    0x32, 0x40, 0x20, 0x2C, 0x16, 0x1C, 0x34, 0x7C, 0x4E
};

static void preamble(void)
{
    int i;

    for (i = 0; i < 18; i++)
        cmdbyte(pre_cmds[i]);
    raw(0x84);
    for (i = 0; i < 16; i++) {
        raw(0x84 | 0x20);
        raw(0x84);
    }
    cmdbyte(0x4E);
}

/* skip_pre: the replay may already have emitted the round preamble, because it runs up to
   the first query and the preamble sits immediately before that.  Issuing a second one
   would talk over the part mid-transaction. */
static uint32_t keyed_round(uint32_t v, int *moved, int skip_pre)
{
    uint8_t prev, first;
    int k;

    if (!skip_pre)
        preamble();
    first = inb_(g_base + 1);
    prev = query((uint8_t) (v & 0xFF));
    for (k = 1; k <= 39; k++) {
        unsigned idx = (unsigned) ((prev & 1) | ((v & 1) << 1));
        uint8_t st;

        v = ((idx ^ v) & 1) ? ((v >> 1) ^ POLY) : (v >> 1);
        prev = query((uint8_t) ((v >> (8 * idx)) & 0xFF));
        st = inb_(g_base + 1);
        if (st != first)
            *moved = 1;
    }
    return v;
}

int main(int argc, char **argv)
{
    FILE *f;
    uint32_t n, i, upto;
    uint8_t *ops;
    uint32_t out;
    int moved = 0;
    double frac = 1.0;

    if (argc > 1)
        g_base = (unsigned) strtoul(argv[1], NULL, 16);
    if (argc > 2)
        frac = atof(argv[2]);
    if (ioperm(g_base, 3, 1) || ioperm(0x80, 1, 1)) {
        perror("ioperm");
        return 1;
    }

    f = fopen("replay.bin", "rb");
    if (!f) {
        perror("replay.bin");
        return 1;
    }
    if (fread(&n, 4, 1, f) != 1) {
        fputs("short replay.bin\n", stderr);
        return 1;
    }
    ops = malloc((size_t) n * 2);
    if (!ops || fread(ops, 2, n, f) != n) {
        fputs("short replay.bin\n", stderr);
        return 1;
    }
    fclose(f);

    upto = (uint32_t) (n * frac);
    printf("base %03X   replaying %u of %u captured accesses   idle STATUS %02X\n",
           g_base, upto, n, inb_(g_base + 1));

    for (i = 0; i < upto; i++) {
        uint8_t op = ops[i * 2], val = ops[i * 2 + 1];

        switch (op) {
            case 0: outb_(g_base, val); (void) inb_(0x80); (void) inb_(0x80); break;
            case 1: (void) inb_(g_base + 1); break;
            case 2: (void) inb_(g_base + 2); break;
            case 3: outb_(g_base + 2, val); (void) inb_(0x80); break;
            case 4: (void) inb_(g_base); break;
            default: break;
        }
    }

    printf("replayed.  STATUS now %02X\n", inb_(g_base + 1));

    {
        int skip_pre = (argc > 3) && (argv[3][0] == 110);   /* 'n' for nopre */

        printf("preamble: %s" , skip_pre ? "skipped (the replay already sent one)" : "issued");
        putchar(10);
        out = keyed_round(0x504EF2AEu, &moved, skip_pre);
    }
    printf("f(504EF2AE) = %08X   expected 32FC6611   %s   (status moved: %s)\n",
           out, out == 0x32FC6611u ? "*** MATCH ***" : "no", moved ? "yes" : "no");
    return out == 0x32FC6611u ? 0 : 2;
}
