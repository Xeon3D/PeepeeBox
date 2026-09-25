/*
 * CDONG -- talk to a funworld CDONGLE-family part (Photo Play 2000, I.G.O. 4) directly.
 *
 * The wire is docs/research-v2/04 § 4.1, and every step below was checked against a real
 * boot: I.G.O. 4 ES on its own 2004 dongle through the passthrough, 2026-09-24
 * (traces/igo4es-2004dongle-boot.log.gz on the dongle host).  That boot made three
 * transactions -- AC CB -> 1 byte, A0 86 2E D0 -> 93 46, AD A6 8C 0D 00 30 00 00 00 ->
 * the 48-byte record -- and this reproduces them.
 *
 *   transaction   write 55, CONTROL 04, then the BF 7F BF pulse train
 *   byte b        F|lo  C|lo  F|lo  9|hi  8|hi  trailer (D0 command, DF anything else)
 *   scramble      the nonce goes out as-is; every later byte, both ways, is XOR nonce^D3
 *   reply claim   ACK (STATUS bit 6) high -> CF -> ACK low -> DF EF (ACK 1) BF CF (0)
 *                 9F EF (1) BF 8F (0) -> DF -> 8 x { CF, read bit 6, FF } MSB first per
 *                 byte -> BF.  The licence query streams both reply bytes in one claim;
 *                 the autodetect and the record read claim every byte separately.
 *   pacing        about 200 us after every write, CONTROL included -- the emulated guest
 *                 the trace came from was that slow, and faster loses the part
 *
 * Usage:  cdong [-n nonce] [-d delay] [-g gap-us] [-b base] [-t log] CMD[:ARGS[:LEN[/CHUNK]]] ...
 *
 *   CMD and ARGS are hex, ARGS as one string of byte pairs; LEN in decimal (default 2),
 *   CHUNK the bytes per claim (default 1).  A transaction whose ARGS is '-' sends none.
 *   A leading '+' continues the current transaction instead of opening a new one.
 *   e.g.  cdong AC:CB:1 A0:862ED0:2/2 A0:862ED0:2/2 +AD:A68C0D0030000000:48
 *
 * Each reply is printed raw and unscrambled.  A poll that times out is reported as such
 * and the transaction abandoned -- that is the part declining to answer, which is data.
 *
 * cc -O2 -o cdong cdong.c && sudo ./cdong ...
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
static int      g_delay = 200;  /* dummy reads of 0x80 after every write, ~1 us each */
static int      g_verbose;
static FILE    *g_log;         /* -t: every access, in dongwire's script format */
static int      g_gap = 2000;   /* microseconds before each reply byte is claimed */

static void
w(uint8_t v)
{
    outb_(g_base, v);
    if (g_log)
        fprintf(g_log, "W %02X\n", v);
    for (int i = 0; i < g_delay; i++)
        (void) inb_(0x80);
}

static int
ack(void)
{
    uint8_t st = inb_(g_base + 1);

    if (g_log)
        fprintf(g_log, "S %02X\n", st);
    return (st >> 6) & 1;
}

static void
ctrl(uint8_t v)
{
    outb_(g_base + 2, v);
    if (g_log)
        fprintf(g_log, "C %02X\n", v);
    /* the part must see CONTROL settle before the BF 7F BF that follows it: without this
       pause the first transaction after the opening works and every later one does not */
    for (int i = 0; i < g_delay; i++)
        (void) inb_(0x80);
}

/* wait for ACK == want; 0 on success */
static int
waitack(int want)
{
    for (long i = 0; i < 200000; i++) {
        if (ack() == want)
            return 0;
        (void) inb_(0x80);
    }
    return -1;
}

static void
sendbyte(uint8_t b, int command)
{
    uint8_t lo = b & 0x0F, hi = b >> 4;

    w(0xF0 | lo);
    w(0xC0 | lo);
    w(0xF0 | lo);
    w(0x90 | hi);
    w(0x80 | hi);
    w(command ? 0xD0 : 0xDF);
}

/* One claim: n reply bytes streamed after a single claim and handshake, into out[].
   Returns the number of bytes read; fewer than n means a poll timed out at *where.
   How many bytes one claim carries is the call's, not the part's: the licence query
   streams its two bytes together, the record read claims every byte on its own. */
static int
recvchunk(uint8_t *out, int n, const char **where)
{
    static const uint8_t hs[4][2] = { { 0xDF, 0xEF }, { 0xBF, 0xCF },
                                      { 0x9F, 0xEF }, { 0xBF, 0x8F } };

    /* After a claim's closing BF the part is idle and ACK just follows DATA bit 5, which BF
       has set -- so "ready" can read true before the part has the next byte. */
    usleep(g_gap);
    *where = "ready";
    if (waitack(1))
        return 0;
    w(0xCF);
    *where = "claim";
    if (waitack(0))
        return 0;
    for (int i = 0; i < 4; i++) {
        w(hs[i][0]);
        w(hs[i][1]);
        *where = "handshake";
        if (waitack((hs[i][1] >> 5) & 1))
            return 0;
    }
    w(0xDF);
    for (int b = 0; b < n; b++) {
        int v = 0;
        for (int i = 0; i < 8; i++) {
            w(0xCF);
            v = (v << 1) | ack();
            w(0xFF);
        }
        out[b] = (uint8_t) v;
    }
    w(0xBF);
    return n;
}

int
main(int argc, char **argv)
{
    int     opt;
    uint8_t nonce = 0x00;

    while ((opt = getopt(argc, argv, "n:d:b:g:t:v")) != -1) {
        if (opt == 'n')
            nonce = (uint8_t) strtoul(optarg, NULL, 16);
        else if (opt == 'd')
            g_delay = atoi(optarg);
        else if (opt == 'b')
            g_base = (unsigned) strtoul(optarg, NULL, 16);
        else if (opt == 'g')
            g_gap = atoi(optarg);
        else if (opt == 't')
            g_log = fopen(optarg, "w");
        else if (opt == 'v')
            g_verbose = 1;
        else {
            fprintf(stderr, "usage: %s [-n nonce] [-d delay] [-b base] CMD[:ARGS[:LEN]] ...\n",
                    argv[0]);
            return 2;
        }
    }
    if (ioperm(g_base, 3, 1) || ioperm(0x80, 1, 1)) {
        perror("ioperm (run as root)");
        return 1;
    }

    const uint8_t key = nonce ^ 0xD3;

    /* The boot's own opening, before the first transaction: without it the part raises
       ACK but never answers a claim.  CONTROL moving through 00, 08, 0C and then 04 in the
       transaction is the part of it that matters most; the DATA pattern is kept as seen. */
    for (int r = 0; r < 2; r++) {
        w(0xFF);
        ctrl(0x00);
        w(0x5A);
        w(0x00);
        w(0xA5);
        w(0x00);
    }
    ctrl(0x08);
    usleep(1000);
    ctrl(0x0C);
    usleep(1000);

    for (int a = optind; a < argc; a++) {
        char    spec[512], *cmd, *args, *len, *slash;
        uint8_t ab[128];
        int     na = 0, rl = 2, chunk = 1;

        /* P<cmd>:<const>:<name hex> -- a whole picture-key query as the 2000 games make
           it (Photo Play 2000 PT on its own dongle, 2026-09-25): the licence query that
           goes with the command (A0 for AA, A1 for AB), then in the same transaction a
           nonce, the command, 08 00 and the case constant low byte first, then the eight
           name bytes each followed by a one-byte reply, one extra reply, and the command
           byte again unscrambled.  Prints the nine wire bytes and the eight values they
           carry once the guest's backwards nibble merge (0x08A9) is undone. */
        if (argv[a][0] == 'P') {
            char     ps[512], *pc, *pk, *pn;
            uint8_t  name[8], wire[9], lic[2];
            const char *where = "";
            int      got = 0, ok = 1;

            snprintf(ps, sizeof ps, "%s", argv[a] + 1);
            pc = strtok(ps, ":");
            pk = strtok(NULL, ":");
            pn = strtok(NULL, ":");
            if (!pc || !pk || !pn || strlen(pn) != 16) {
                fprintf(stderr, "picture query: P<AA|AB>:<const hex4>:<16 hex digits>\n");
                return 2;
            }
            const uint8_t  pcmd = (uint8_t) strtoul(pc, NULL, 16);
            const unsigned k16  = (unsigned) strtoul(pk, NULL, 16);
            for (int i = 0; i < 8; i++) {
                char h[3] = { pn[2 * i], pn[2 * i + 1], 0 };
                name[i] = (uint8_t) strtoul(h, NULL, 16);
            }
            static const uint8_t lic_aa[4] = { 0xA0, 0x86, 0x2E, 0xD0 };
            static const uint8_t lic_ab[4] = { 0xA1, 0xA6, 0x4B, 0xD0 };
            const uint8_t *lq = (pcmd == 0xAB) ? lic_ab : lic_aa;

            w(0x55);
            ctrl(0x04);
            w(0xBF);
            w(0x7F);
            w(0xBF);
            sendbyte(nonce, 0);
            sendbyte(lq[0] ^ key, 1);
            for (int i = 1; i < 4; i++)
                sendbyte(lq[i] ^ key, 0);
            if (recvchunk(lic, 2, &where) < 2)
                ok = 0;
            if (ok) {
                sendbyte(nonce, 0);
                sendbyte(pcmd ^ key, 1);
                sendbyte(0x08 ^ key, 0);
                sendbyte(0x00 ^ key, 0);
                sendbyte((uint8_t) (k16 & 0xFF) ^ key, 0);
                sendbyte((uint8_t) (k16 >> 8) ^ key, 0);
                for (int i = 0; i < 8 && ok; i++) {
                    sendbyte(name[i] ^ key, 0);
                    if (recvchunk(&wire[i], 1, &where) < 1)
                        ok = 0;
                    else
                        got++;
                }
                if (ok && recvchunk(&wire[8], 1, &where) == 1)
                    got++;
                else
                    ok = 0;
                sendbyte(pcmd, 0);          /* the terminator: the command, unscrambled */
            }
            printf("P%02X %04X", pcmd, k16);
            for (int i = 0; i < 8; i++)
                printf(" %02X", name[i]);
            if (!ok) {
                printf("  [timeout at %s after %d of 9]\n", where, got);
                continue;
            }
            printf("  wire");
            for (int i = 0; i < 9; i++)
                printf(" %02X", wire[i] ^ key);
            printf("  r");
            for (int i = 0; i < 8; i++)
                printf(" %02X", (uint8_t) (((wire[i] ^ key) & 0x0F) | ((wire[i + 1] ^ key) & 0xF0)));
            printf("\n");
            continue;
        }

        /* '+' continues the current transaction: no 55 / CONTROL / BF 7F BF, just a
           fresh nonce and the command.  The game reads the record that way, straight
           after a licence query. */
        const char *sp = argv[a];
        int cont = (*sp == '+');
        if (cont)
            sp++;
        snprintf(spec, sizeof spec, "%s", sp);
        cmd = strtok(spec, ":");
        args = strtok(NULL, ":");
        len = strtok(NULL, ":");
        if (len) {
            rl = atoi(len);
            if ((slash = strchr(len, '/')) != NULL)
                chunk = atoi(slash + 1);
        }
        if (chunk < 1)
            chunk = 1;
        if (args && strcmp(args, "-"))
            for (char *p = args; p[0] && p[1] && na < 128; p += 2) {
                char h[3] = { p[0], p[1], 0 };
                ab[na++] = (uint8_t) strtoul(h, NULL, 16);
            }
        uint8_t c = (uint8_t) strtoul(cmd, NULL, 16);

        /* open the transaction exactly as the library does */
        if (!cont) {
            w(0x55);
            ctrl(0x04);
            w(0xBF);
            w(0x7F);
            w(0xBF);
        }
        sendbyte(nonce, 0);                 /* in the clear: the key is still zero */
        sendbyte(c ^ key, 1);
        for (int i = 0; i < na; i++)
            sendbyte(ab[i] ^ key, 0);

        printf("%02X", c);
        for (int i = 0; i < na; i++)
            printf(" %02X", ab[i]);
        printf("  ->");
        uint8_t raw[512];
        int     got = 0;
        const char *where = "";
        if (rl > 512)
            rl = 512;
        while (got < rl) {
            int want = (rl - got < chunk) ? rl - got : chunk;
            int n = recvchunk(raw + got, want, &where);
            got += n;
            if (n < want)
                break;
        }
        for (int i = 0; i < got; i++)
            printf(" %02X", raw[i] ^ key);
        if (got < rl)
            printf("  [timeout at %s after %d of %d]", where, got, rl);
        if (g_verbose) {
            printf("   raw");
            for (int i = 0; i < got; i++)
                printf(" %02X", raw[i]);
        }
        printf("\n");
        if (rl >= 16 && got) {
            printf("    \"");
            for (int i = 0; i < got; i++) {
                uint8_t ch = raw[i] ^ key;
                putchar((ch >= 32 && ch < 127) ? ch : '.');
            }
            printf("\"\n");
        }
        /* nothing after the last BF: the game goes straight to the next 55, and an FF
           here reads to the part as "advance" */
    }
    return 0;
}
