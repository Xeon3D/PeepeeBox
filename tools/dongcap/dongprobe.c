/*
 * DONGPROBE -- find out what the Photo Play / I.G.O. dongle actually responds to.
 *
 * Written after DONGCAP came back empty on the cabinet while the game itself renders its
 * photos perfectly.  That combination says the part is alive and our sequence is missing
 * something, so this stops assuming and measures.
 *
 * The suspicion it was built to test: every tool so far writes ONLY the DATA register.
 * The parallel port's CONTROL register (base+2) is never touched, so whatever state the
 * BIOS or the Linux parport driver happened to leave it in is the state we ran in.  Two
 * bits there can silence the port outright:
 *
 *   bit 5  direction.  Set = the DATA drivers are tri-stated, so every byte we "write"
 *          never reaches a pin.  A dongle would see nothing and answer nothing.
 *   bit 2  nINIT.  Held low, many parallel-port parts sit in reset.
 *
 * So phase C sweeps CONTROL and repeats the DATA sweep under each value, looking for a
 * setting where STATUS starts moving.  If one exists, that is the missing step.
 *
 * Build: cc -O2 -o dongprobe dongprobe.c        Run: sudo ./dongprobe [base-in-hex]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/io.h>

#define POLY 0x80500062u

static unsigned g_base = 0x378;

static void io_delay(void)
{
    (void) inb(0x80);
    (void) inb(0x80);
}

static void wdata(unsigned char b)
{
    outb(b, g_base);
    io_delay();
}

static void wctrl(unsigned char b)
{
    outb(b, g_base + 2);
    io_delay();
}

static unsigned char rstat(void)
{
    return inb(g_base + 1);
}

static void raw(unsigned char b)      { wdata(b); }

static void cmdbyte(unsigned char b)
{
    raw((unsigned char) ((b & 0xFE) | 0x80));
    raw((unsigned char) (b | 0x81));
    raw((unsigned char) ((b & 0xFE) | 0x80));
}

/* the query, exactly as FINDIT.EXE 0x32f59 builds it */
static unsigned char query_raw(unsigned char q)
{
    unsigned char pay = (unsigned char) (((q << 1) & 0x0E) | ((q << 2) & 0x60) | 0x80);

    raw(pay);
    raw((unsigned char) (pay | 0x10));
    raw(pay);
    return rstat();
}

/* ------------------------------------------------------------------ phases */

static int sweep_data(unsigned char *seen, int verbose)
{
    int hist[256];
    int i, distinct = 0;

    memset(hist, 0, sizeof hist);
    for (i = 0; i < 256; i++) {
        wdata((unsigned char) i);
        seen[i] = rstat();
        hist[seen[i]]++;
    }
    for (i = 0; i < 256; i++)
        if (hist[i])
            distinct++;
    if (verbose) {
        printf("      STATUS values seen:");
        for (i = 0; i < 256; i++)
            if (hist[i])
                printf(" %02X(x%d)", i, hist[i]);
        printf("\n");
    }
    return distinct;
}

int main(int argc, char **argv)
{
    unsigned char seen[256];
    unsigned char ctrl0;
    int i, d;

    if (argc > 1)
        g_base = (unsigned) strtoul(argv[1], NULL, 16);
    if (ioperm(g_base, 3, 1) || ioperm(0x80, 1, 1)) {
        perror("ioperm");
        return 1;
    }

    printf("base 0x%03X\n", g_base);

    /* --- A: what is the port doing right now --- */
    ctrl0 = inb(g_base + 2);
    printf("\nA. idle state\n");
    printf("   CONTROL reads %02X   (bit5 direction=%d, bit2 nINIT=%d)\n",
           ctrl0, (ctrl0 >> 5) & 1, (ctrl0 >> 2) & 1);
    printf("   STATUS  reads");
    for (i = 0; i < 8; i++)
        printf(" %02X", rstat());
    printf("\n");

    /* --- B: does STATUS respond to DATA as things stand --- */
    printf("\nB. DATA sweep at the current CONTROL\n");
    d = sweep_data(seen, 1);
    printf("   -> %d distinct STATUS value%s\n", d, d == 1 ? " (nothing responding)" : "s");

    /* --- C: the same sweep under every CONTROL value --- */
    printf("\nC. DATA sweep under each CONTROL value\n");
    for (i = 0; i < 32; i++) {
        wctrl((unsigned char) i);
        d = sweep_data(seen, 0);
        printf("   CONTROL %02X -> %d distinct", i, d);
        if (d > 1) {
            int j;

            printf("   *** RESPONDS ***  e.g.");
            for (j = 0; j < 256 && j < 8; j++)
                printf(" %02X:%02X", j, seen[j]);
        }
        printf("\n");
    }

    /* --- D: the documented round, both answer-line readings --- */
    printf("\nD. one keyed round under each CONTROL value (seed 0, input 504EF2AE)\n");
    for (i = 0; i < 32; i++) {
        uint32_t v5 = 0x504EF2AE, v7 = 0x504EF2AE;
        unsigned char p5, p7, st;
        int k, moved = 0;
        unsigned char first;

        wctrl((unsigned char) i);

        /* bit 5, taken straight -- what every tool so far implements */
        cmdbyte(0x00); cmdbyte(0x4E); raw(0x84);
        st = query_raw((unsigned char) (v5 & 0xFF));
        first = st;
        p5 = (unsigned char) ((st >> 5) & 1);
        for (k = 1; k <= 39; k++) {
            unsigned idx = (unsigned) ((p5 & 1) | ((v5 & 1) << 1));

            v5 = ((idx ^ v5) & 1) ? ((v5 >> 1) ^ POLY) : (v5 >> 1);
            st = query_raw((unsigned char) ((v5 >> (8 * idx)) & 0xFF));
            if (st != first)
                moved = 1;
            p5 = (unsigned char) ((st >> 5) & 1);
        }

        /* bit 7, inverted -- the other branch of FINDIT.EXE 0x32d17 */
        cmdbyte(0x00); cmdbyte(0x4E); raw(0x84);
        st = query_raw((unsigned char) (v7 & 0xFF));
        p7 = (unsigned char) (!((st >> 7) & 1));
        for (k = 1; k <= 39; k++) {
            unsigned idx = (unsigned) ((p7 & 1) | ((v7 & 1) << 1));

            v7 = ((idx ^ v7) & 1) ? ((v7 >> 1) ^ POLY) : (v7 >> 1);
            st = query_raw((unsigned char) ((v7 >> (8 * idx)) & 0xFF));
            p7 = (unsigned char) (!((st >> 7) & 1));
        }

        printf("   CONTROL %02X  bit5:%08X  bit7inv:%08X  STATUS moved:%s%s\n",
               i, v5, v7, moved ? "YES" : "no",
               (v5 == 0x32FC6611 || v7 == 0x32FC6611) ? "   *** MATCHES 32FC6611 ***" : "");
    }

    wctrl(ctrl0);       /* leave the port as we found it */
    printf("\nCONTROL restored to %02X\n", ctrl0);
    return 0;
}
