/*
 * DONGTIMING -- is the part simply being clocked too fast?
 *
 * dongcap now emits the session init and the preamble exactly as captured off the wire,
 * and the part still answers a constant 1.  The one variable left uncontrolled is speed:
 * the capture was taken through PeepeeBox emulating a 486DX4 at 22% of its rate, so the
 * guest's gap between port accesses was orders of magnitude longer than a C loop on a
 * 1.6 GHz Atom with two I/O delays.
 *
 * This runs one keyed round of a known input at a range of delays and reports what comes
 * back.  Success is 32FC6611 for 504EF2AE; BDC587AC is the signature of a line stuck at 1.
 *
 * cc -O2 -o dongtiming dongtiming.c && sudo ./dongtiming [base]
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

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
static int      g_rep = 4;      /* writes per byte */
static int      g_pad = 2;      /* I/O delays after each write */

static void raw(uint8_t b)
{
    int i, j;

    for (i = 0; i < g_rep; i++) {
        outb_(g_base, b);
        for (j = 0; j < g_pad; j++)
            (void) inb_(0x80);
    }
}

static void rawcmd(uint8_t b) { raw(b); raw((uint8_t) (b | 1)); raw(b); }
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

static const uint8_t init_bytes[14] = {
    0x58, 0x1A, 0x7A, 0x54, 0x08, 0x68, 0x40,
    0x32, 0x50, 0x20, 0x2C, 0x16, 0x1C, 0x34
};

static void session_init(void)
{
    int i;

    raw(0x5B);
    raw(0x5A);
    for (i = 0; i < 14; i++)
        rawcmd(init_bytes[i]);
    for (i = 0; i < 64; i++) {
        raw((uint8_t) (i * 2));
        (void) inb_(g_base + 1);
    }
}

static void preamble(void)
{
    int i;

    cmdbyte(0x34);
    cmdbyte(0x7C);
    cmdbyte(0x4E);
    for (i = 0; i < 16; i++) {
        raw(0x84);
        raw(0x84 | 0x20);
        raw(0x84);
    }
    cmdbyte(0x4E);
}

static uint32_t keyed_round(uint32_t v, int *moved)
{
    uint8_t prev, st;
    uint8_t first;
    int k;

    preamble();
    first = inb_(g_base + 1);
    prev = query((uint8_t) (v & 0xFF));
    for (k = 1; k <= 39; k++) {
        unsigned idx = (unsigned) ((prev & 1) | ((v & 1) << 1));

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
    static const int reps[] = { 1, 4, 8, 16 };
    static const int pads[] = { 2, 8, 32, 128, 512, 2048 };
    int r, p;

    if (argc > 1)
        g_base = (unsigned) strtoul(argv[1], NULL, 16);
    if (ioperm(g_base, 3, 1) || ioperm(0x80, 1, 1)) {
        perror("ioperm");
        return 1;
    }

    printf("base %03X   idle STATUS %02X\n", g_base, inb_(g_base + 1));
    printf("looking for f(504EF2AE) = 32FC6611;  BDC587AC means the line never moved\n\n");
    printf("  rep  pad        result   moved\n");

    for (r = 0; r < (int) (sizeof reps / sizeof reps[0]); r++) {
        for (p = 0; p < (int) (sizeof pads / sizeof pads[0]); p++) {
            uint32_t out;
            int moved = 0;

            g_rep = reps[r];
            g_pad = pads[p];
            session_init();
            out = keyed_round(0x504EF2AEu, &moved);
            printf("  %3d %4d    %08X   %s%s\n", g_rep, g_pad, out,
                   moved ? "yes" : "no ",
                   out == 0x32FC6611u ? "   *** MATCH ***" : "");
            fflush(stdout);
        }
    }
    return 0;
}
