/*
 * DONGCAP (Linux) -- capture a Photo Play / I.G.O. dongle's keyed-round outputs.
 *
 * The Linux twin of dongcap.c.  Same wire, same work list, same output file, so either
 * host can produce a capture the offline tools read without knowing which one made it.
 * The wire code is a transliteration of the Win32 version rather than a re-derivation:
 * if the two ever disagree, that is a bug in this file.
 *
 * It talks to the port directly (ioperm/iopl + in/out) rather than through ppdev.
 * ppdev would be tidier, but it owns the control lines and inverts some of them, and
 * this protocol clocks on DATA bits and reads STATUS bit 5 -- doing it raw is what keeps
 * the sequence byte-identical to the DOS original and to dongcap.c.
 *
 * The wire, from I.G.O. 2's FINDIT.EXE:
 *   command byte b : write (b & 0xFE)|0x80, b|0x81, (b & 0xFE)|0x80   -- DATA bit 0 clocks
 *   query q        : payload = ((q<<1)&0x0E) | ((q<<2)&0x60) | 0x80
 *                    write payload, payload|0x10, payload             -- DATA bit 4 clocks
 *                    answer = STATUS bit 5
 *   round preamble : cmdbyte(0x34), cmdbyte(0x7C), cmdbyte(0x4E),
 *                    sixteen SK pulses (0x84 with bit 5 clocked, not bit 0),
 *                    cmdbyte(0x4E)
 *
 * The preamble is not guesswork any more.  It was read straight off the wire while the
 * game itself drove the part, through PeepeeBox with the LPT passed through to real
 * hardware -- identical before all 118 rounds of that capture.  See docs/research/30
 * section 9.3.
 *
 * What was here before opened with cmdbyte(seed), cmdbyte(0x4E), raw(0x84) and swept all
 * 256 seeds looking for one the part answered to.  There is no seed: the opening command
 * is the constant 0x34, and two command bytes plus sixteen clock pulses were missing.
 * That is why 256 seeds against 20 frame variants never moved a status line.
 *
 * The .LST's (input, expected output) pairs are now a check rather than a search: a part
 * answering wrongly and a part not answering look identical downstream, so the run still
 * verifies before it records, and still writes DONGCAP.DIAG if it fails.
 *
 * Build:  cc -O2 -o dongcap dongcap_linux.c
 * Run:    sudo ./dongcap [base]        base in hex, default 378
 *
 * Root is required for port access.  A PCIe parallel card usually sits above 0x3FF --
 * read its base out of /proc/ioports (look for "parport") and pass it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/io.h>

#define POLY 0x80500062u
#define CB   0x803425C3u

#define LST_MAGIC 0x50414344u      /* 'DCAP' */
#define OUT_MAGIC 0x54554F44u      /* 'DOUT' */
#define DIAG_MAGIC 0x47414944u     /* 'DIAG' */

static unsigned g_base = 0x378;
static unsigned char g_seed;

/* Each keyed round consults the part forty times.  Those bits are direct observations of
   the byte-to-bit oracle, which is what the composite outputs cannot give -- see the
   header of mkdongcap_dos.py.  Staged here, five bytes per round, MSB first. */
static unsigned char g_bits[5];
static int g_bitn;

static void putbit(unsigned char b)
{
    if (g_bitn < 40)
        g_bits[g_bitn >> 3] |= (unsigned char) ((b & 1) << (7 - (g_bitn & 7)));
    g_bitn++;
}

/* ------------------------------------------------------------------ port */

/* The transport writes each byte more than once -- notes/HANDOFF2001.md section 10.4,
   the repeat count out of the state struct at 0x3664D.  The captured wire shows four
   (33,160 runs) with a few of eight, not the thirty-two section 4 once guessed.  Writing
   it once, as this did, holds the line for a quarter as long as the part is used to. */
#define PP_REPEAT 4

static void raw(unsigned char b)
{
    int i;

    for (i = 0; i < PP_REPEAT; i++) {
        outb(b, g_base);
        (void) inb(0x80);       /* the traditional I/O delay */
        (void) inb(0x80);
    }
}

/* A command byte that is NOT put through the cooked writer: bit 0 is still the clock,
   but bit 7 is left alone.  The session init uses this form; the round preamble uses the
   cooked one.  Both appear in the capture. */
static void rawcmd(unsigned char b)
{
    raw(b);
    raw((unsigned char) (b | 0x01));
    raw(b);
}

static void cmdbyte(unsigned char b)
{
    raw((unsigned char) ((b & 0xFE) | 0x80));
    raw((unsigned char) (b | 0x81));
    raw((unsigned char) ((b & 0xFE) | 0x80));
}

static unsigned char query(unsigned char q)
{
    unsigned char pay = (unsigned char) (((q << 1) & 0x0E) | ((q << 2) & 0x60) | 0x80);

    raw(pay);
    raw((unsigned char) (pay | 0x10));
    raw(pay);
    return (unsigned char) ((inb(g_base + 1) >> 5) & 1);
}

/* The once-per-session opening, taken verbatim off the wire (docs/research/30 § 9.3).
   The game does this before its first round and never again -- 118 rounds followed it in
   the capture, and only the first was preceded by this.  Firing rounds at a part that has
   not been through it is what every silent probe was doing.

   Two loose writes, then fourteen bit-0-clocked bytes in the uncooked form, then a
   64-step sweep that clocks 64 bits back out.  The bits are read and discarded here: the
   capture shows the game reading 50 50 73 73 FF 50 FF 73, but nothing establishes what
   they mean, and the part evidently wants them clocked out either way. */
static const unsigned char init_bytes[14] = {
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
        raw((unsigned char) (i * 2));
        (void) inb(g_base + 1);
    }
}

/* One SK pulse: the clock here is DATA bit 5, not bit 0 as in a command byte.
   docs/research/30 section 9.3 -- the preamble uses both clocks. */
static void sk_pulse(void)
{
    raw(0x84);
    raw(0x84 | 0x20);
    raw(0x84);
}

/* The real preamble, read straight off the wire while the game drove the part
   (docs/research/30 section 9.3).  Identical before all 118 captured rounds:

        cmdbyte(0x34)  cmdbyte(0x7C)  cmdbyte(0x4E)
        sixteen SK pulses
        cmdbyte(0x4E)

   What was here before -- cmdbyte(seed), cmdbyte(0x4E), raw(0x84) -- was missing two
   command bytes and all sixteen pulses, and swept a seed byte that does not exist:
   the opening command is the constant 0x34.  That is why 256 seeds against 20 frame
   variants never moved a status line. */
static void preamble(void)
{
    int i;

    cmdbyte(0x34);
    cmdbyte(0x7C);
    cmdbyte(0x4E);
    for (i = 0; i < 16; i++)
        sk_pulse();
    cmdbyte(0x4E);
}

/* ------------------------------------------------- the keyed round itself */

/* 39 shift steps, 40 consultations; the byte offered to the part is picked by the
   previous answer and the bit about to be shifted out. */
static uint32_t keyed_round(uint32_t v)
{
    unsigned char prev;
    int k;

    memset(g_bits, 0, sizeof g_bits);
    g_bitn = 0;
    preamble();
    prev = query((unsigned char) (v & 0xFF));
    putbit(prev);
    for (k = 1; k <= 39; k++) {
        unsigned idx = (unsigned) ((prev & 1) | ((v & 1) << 1));

        v = ((idx ^ v) & 1) ? ((v >> 1) ^ POLY) : (v >> 1);
        prev = query((unsigned char) ((v >> (8 * idx)) & 0xFF));
        putbit(prev);
    }
    return v;
}

/* ------------------------------------------------------- keyless helpers */

static uint32_t rol(uint32_t v, int s)
{
    s &= 31;
    return s ? ((v << s) | (v >> (32 - s))) : v;
}

/* B_rounds(b0,b1) -> b0, which is L3 */
static uint32_t b_rounds_first(uint32_t b0, uint32_t b1)
{
    int s;

    for (s = 10; s >= 0; s -= 2) {
        uint32_t t = rol(b0 ^ CB, s) ^ b1;

        b1 = b0;
        b0 = t;
    }
    return b0;
}

/* The same stage the other way: six rounds ascending, second output.  Feeding it
   (L3,R3) returns L1 -- checked against b_rounds_first on 2000 random pairs. */
static uint32_t b_rounds_fwd_second(uint32_t b0, uint32_t b1)
{
    int s;

    for (s = 0; s <= 10; s += 2) {
        uint32_t t = rol(b1 ^ CB, s) ^ b0;

        b0 = b1;
        b1 = t;
    }
    return b1;
}

/* ------------------------------------------------------------------ i/o */

static void *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    void *p;
    long n;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        fclose(f);
        return NULL;
    }
    p = calloc(1, (size_t) n + 16);
    if (p && fread(p, 1, (size_t) n, f) != (size_t) n) {
        free(p);
        p = NULL;
    }
    fclose(f);
    if (p)
        *len = (size_t) n;
    return p;
}

static int spill(const char *path, const void *p, size_t n)
{
    FILE *f = fopen(path, "wb");
    int ok;

    if (!f)
        return 0;
    ok = fwrite(p, 1, n, f) == n;
    ok &= fclose(f) == 0;
    return ok;
}

static double now(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ----------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    uint32_t *hdr, *cal, *work, *enc, *out;
    unsigned char *bits, *bp;
    uint32_t ncal, count, nenc, i;
    unsigned char *lst;
    size_t len = 0;
    int seed;
    double t0;

    if (argc > 1)
        g_base = (unsigned) strtoul(argv[1], NULL, 16);

    /* ioperm covers the first 0x400 ports; a PCIe card above that needs iopl. */
    if (g_base + 2 < 0x400) {
        if (ioperm(g_base, 3, 1) || ioperm(0x80, 1, 1)) {
            perror("ioperm");
            fputs("Run as root.\n", stderr);
            return 1;
        }
    } else if (iopl(3)) {
        perror("iopl");
        fputs("Run as root.\n", stderr);
        return 1;
    }

    /* Take the part through its opening sequence once, before anything else. */
    session_init();

    lst = slurp("DONGCAP.LST", &len);
    if (!lst) {
        fputs("DONGCAP.LST not found -- keep it next to this program.\n", stderr);
        return 1;
    }
    hdr = (uint32_t *) lst;
    if (len < 16 || hdr[0] != LST_MAGIC) {
        fputs("DONGCAP.LST is not a capture list.\n", stderr);
        return 1;
    }
    ncal  = hdr[1];
    count = hdr[2];
    nenc  = hdr[3];                  /* EncodeData blocks, e.g. I.G.O. 3's boot check */
    cal   = hdr + 4;                 /* ncal pairs of (input, expected) */
    work  = cal + ncal * 2;          /* count pairs of (L1, R1)         */
    enc   = work + count * 2;        /* nenc pairs of (P0, P1)          */
    if (len < 16 + 8 * (size_t) (ncal + count + nenc)) {
        fputs("DONGCAP.LST is truncated.\n", stderr);
        return 1;
    }

    printf("LPT base 0x%03X, %u buffers, %u encode blocks, %u calibration pairs\n",
           g_base, count, nenc, ncal);

    /* --- verify against answers already known, rather than hunting a seed ---
       The seed sweep this used to do was chasing something that is not there: the
       preamble opens with a constant 0x34 (docs/research/30 section 9.3).  What is
       worth doing is still checking, because a part that answers wrongly and a part
       that does not answer look identical downstream. */
    fputs("Checking against known answers", stdout);
    fflush(stdout);
    seed = 0;
    for (i = 0; i < ncal; i++) {
        uint32_t got = keyed_round(cal[i * 2]);

        printf("\n   f(%08X) = %08X   expected %08X   %s",
               cal[i * 2], got, cal[i * 2 + 1],
               got == cal[i * 2 + 1] ? "ok" : "MISMATCH");
        if (got != cal[i * 2 + 1])
            seed = -1;
    }
    fputc('\n', stdout);
    if (seed < 0) {
        /* Calibration failing is itself information, so leave data behind rather than
           nothing: every seed against the first few inputs, for working out offline
           what the part is actually doing. */
        uint32_t nd = ncal < 3 ? ncal : 3;
        uint32_t *dg = calloc(256u * 4 + 8, 4);
        uint32_t w = 0;

        fputs("\nNo seed reproduces the known answers.\n"
              "Writing DONGCAP.DIAG instead -- every seed against the first inputs.\n",
              stdout);
        dg[w++] = DIAG_MAGIC;
        dg[w++] = nd;
        for (i = 0; i < 256; i++) {
            uint32_t j;

            g_seed = (unsigned char) i;
            for (j = 0; j < nd; j++)
                dg[w++] = keyed_round(cal[j * 2]);
        }
        if (spill("DONGCAP.DIAG", dg, w * 4))
            fputs("DONGCAP.DIAG written. Send it back -- it says what the part answers.\n",
                  stdout);
        return 2;
    }
    printf("\nSeed 0x%02X reproduces every calibration pair.\n", (unsigned) seed);
    g_seed = (unsigned char) seed;

    /* --- capture --- */
    out = calloc((size_t) (count + nenc) * 2 + 8, 4);
    if (!out) {
        fputs("out of memory\n", stderr);
        return 1;
    }
    bits = calloc((size_t) (count + nenc) * 2 + 1, 5);
    if (!out || !bits) {
        fputs("out of memory\n", stderr);
        return 1;
    }
    bp = bits;
    out[0] = OUT_MAGIC;
    out[1] = count;
    out[2] = (uint32_t) seed;
    out[3] = nenc;

    t0 = now();
    fputs("Capturing", stdout);
    fflush(stdout);
    for (i = 0; i < count; i++) {
        uint32_t L1 = work[i * 2];
        uint32_t R1 = work[i * 2 + 1];
        uint32_t f1 = keyed_round(L1);
        uint32_t L3;

        memcpy(bp, g_bits, 5); bp += 5;
        L3 = b_rounds_first(f1 ^ R1, L1);
        {
            uint32_t f2 = keyed_round(L3);

            memcpy(bp, g_bits, 5); bp += 5;
            out[4 + i * 2 + 1] = f2;
        }
        out[4 + i * 2]     = f1;
        if ((i & 511) == 0) {
            fputc('.', stdout);
            fflush(stdout);
        }
    }

    /* The encode direction starts from the plaintext instead of the ciphertext, so the
       two keyed inputs are found in the opposite order: L3 is the plaintext's second
       dword outright, and L1 falls out of the ascending B rounds once its answer is in
       hand.  They are stored in the same slots as above -- f1 is L1's answer, f2 is
       L3's -- so a consumer reads both kinds of entry the same way. */
    for (i = 0; i < nenc; i++) {
        uint32_t P0 = enc[i * 2];
        uint32_t P1 = enc[i * 2 + 1];
        uint32_t L3 = P1;
        uint32_t f2 = keyed_round(L3);
        uint32_t R3, L1, f1;

        memcpy(bp, g_bits, 5); bp += 5;
        R3 = P0 ^ f2;
        L1 = b_rounds_fwd_second(L3, R3);
        f1 = keyed_round(L1);
        memcpy(bp, g_bits, 5); bp += 5;

        out[4 + (count + i) * 2]     = f1;
        out[4 + (count + i) * 2 + 1] = f2;
    }
    fputc('\n', stdout);

    if (!spill("DONGCAP.BIN", out, ((size_t) (count + nenc) * 2 + 4) * 4)) {
        fputs("cannot create DONGCAP.BIN\n", stderr);
        return 1;
    }
    printf("Done -- DONGCAP.BIN written, %u rounds captured in %.0f seconds. "
           "Send DONGCAP.BIN back.\n", (count + nenc) * 2, now() - t0);
    return 0;
}
