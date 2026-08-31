/*
 * PeepeeBox   A fork of 86Box that emulates the funworld Photo Play / I.G.O.
 *             arcade kiosk hardware, including its protection tokens.
 *
 *             The 2008 generation's dongle: a serial smart-card reader on COM2.
 *
 *             IGO 8 does not use the parallel port for protection at all.  Its
 *             games open a reader at I/O 2F8h (9600 8N1) and push ISO 7816-4
 *             APDUs at a card behind it, under two layers of obfuscation keyed
 *             by a fresh four-byte nonce per frame.  Docs/18 has the protocol
 *             as recovered from the unpatched image; this is a port of the
 *             model in igo8_dongle.py, which was validated against the guest's
 *             own checks.
 *
 *             What the guest ultimately keeps out of the whole exchange is
 *             three fields of a 100-byte record: a family string, a two-letter
 *             territory and a title.  From those it composes
 *             "Version 2008 (XX)" and compares it against its own MAIN.SET.
 *             Serve the right record and an untouched image boots; serve the
 *             wrong one and every game reports "Wrong Version".
 *
 *             There is no failure path on the dongle side to get wrong.  A
 *             reader that never answers produces no message at all -- it just
 *             leaves the banner empty, and the version comparison downstream is
 *             what complains.  That is why this file logs what it serves.
 *
 * Authors:    The HUEG PP team.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/serial.h>
#include <86box/fifo8.h>
#include <86box/fifo.h>
#include <86box/plat_unused.h>
#include <86box/photoplay.h>

#define ENABLE_DONGLE_IGO8_LOG 1

#define SC_PORT     1           /* COM2 -- where the cabinet wiring puts the reader */
#define SC_BAUD     9600
#define SC_RECORD   100         /* what the guest's READ BINARY asks for */
#define SC_LCG_MUL  0x08088405U /* the Borland/Delphi LCG the whole product uses */
#define SC_LCG_SEED 0x01BB253AU /* the record key, identical in all 61 executables */
#define SC_IN_MAX   192
#define SC_GAP_US   5000.0      /* ~5 byte times at 9600: a lull means a new frame */

#ifdef ENABLE_DONGLE_IGO8_LOG
int dongle_igo8_do_log = ENABLE_DONGLE_IGO8_LOG;

static void
sc_log(const char *fmt, ...)
{
    va_list ap;

    if (dongle_igo8_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sc_log(fmt, ...)
#endif

/* ------------------------------------------------------------------------------------
 * The two keystreams.  Docs/18 section 2.
 * ---------------------------------------------------------------------------------- */

/* Host -> reader: four nibbles seeded from the two plaintext nonce bytes, emitting two
   bytes per round.  The stream's first output is consumed by the third nonce byte. */
typedef struct {
    uint8_t n0, n1, n2, n3;
    int     half;
} sc_tx_t;

static void
sc_tx_seed(sc_tx_t *s, uint8_t lo, uint8_t hi)
{
    s->n0   = lo & 0x0F;
    s->n1   = (lo >> 4) & 0x0F;
    s->n2   = hi & 0x0F;
    s->n3   = (hi >> 4) & 0x0F;
    s->half = 0;
}

static uint8_t
sc_tx_next(sc_tx_t *s)
{
    if (s->half) {
        s->half = 0;
        return (uint8_t) (s->n0 | (s->n1 << 4));
    }
    if (++s->n0 > 0x0F) {
        s->n0 &= 0x0F;
        s->n1 = (uint8_t) ((s->n1 + 1) & 0x0F);
    }
    s->n3 ^= s->n0;
    s->n0 ^= s->n1;
    s->n1 ^= s->n2;
    s->n2 ^= s->n3;
    s->half = 1;
    return (uint8_t) (s->n2 | (s->n3 << 4));
}

/* Reader -> host: two byte registers whose walk never depends on the data, so the same
   routine encodes here and decodes in the guest. */
typedef struct {
    uint8_t a, b;
} sc_rx_t;

static void
sc_rx_seed(sc_rx_t *c, uint8_t a, uint8_t b)
{
    c->a = a;
    c->b = b;
}

static uint8_t
sc_rx_apply(sc_rx_t *c, uint8_t x)
{
    const uint8_t old_b = c->b;

    x ^= c->a;
    c->a = (uint8_t) (c->b + 0x25);
    if (old_b < 0x1E)
        c->a = 0xE9;
    if (c->a > 0xAE)
        c->b = 0x17;
    x ^= c->b;
    c->b = (uint8_t) (c->a + 0x75);
    if (c->a < 0x38)
        c->b = 0x39;
    if (c->b > 0x7B)
        c->a = 0xD5;
    return x;
}

/* ------------------------------------------------------------------------------------
 * The reader
 * ---------------------------------------------------------------------------------- */

typedef struct {
    serial_t  *serial;
    pc_timer_t tx_timer;  /* paces the reply out at the wire's own rate      */
    pc_timer_t gap_timer; /* a lull on the line resynchronises the assembler */
    Fifo8      resp;

    sc_tx_t tx;
    sc_rx_t rx;

    uint8_t in[SC_IN_MAX]; /* the frame so far, already deobfuscated */
    int     n;
    int     want; /* total frame length once byte 4 has told us; 0 = not yet */

    uint8_t record[SC_RECORD];
    int     n_frames;
} sc_t;

/* The record cipher: XOR with the high byte of the LCG.  Symmetric, so this both
   encrypts what we serve and would decrypt what a real card holds. */
static void
sc_lcg(uint8_t *dst, const uint8_t *src, int n)
{
    uint32_t x = SC_LCG_SEED;

    for (int i = 0; i < n; i++) {
        x      = (uint32_t) ((x * SC_LCG_MUL) + 1);
        dst[i] = (uint8_t) (src[i] ^ (x >> 24));
    }
}

static void
sc_reply(sc_t *sc, const uint8_t *p, int n)
{
    for (int i = 0; i < n; i++)
        fifo8_push(&sc->resp, sc_rx_apply(&sc->rx, p[i]));
}

/* Wrap a card response in the reader's frame and queue it.  The guest reads a fixed
   byte count per transaction and returns early only once that many have arrived, so a
   short frame is not a protocol error -- it just costs the guest its full timeout,
   which is eleven seconds.  Every length below is the one the guest asks for. */
static void
sc_reply_frame(sc_t *sc, const uint8_t *body, int n)
{
    uint8_t f[SC_IN_MAX];
    uint8_t chk = 0;
    int     k   = 0;

    if ((n < 0) || (n > ((int) sizeof(f) - 4)))
        return;

    f[k++] = 0x00;        /* address                 */
    f[k++] = 0x00;        /* I-frame, sequence 0     */
    f[k++] = (uint8_t) n; /* payload length          */
    memcpy(f + k, body, (size_t) n);
    k += n;
    for (int i = 0; i < k; i++)
        chk ^= f[i];
    f[k++] = chk;         /* XOR of everything above */

    sc_reply(sc, f, k);
}

/* The card itself.  Three instructions is the whole of what the guest ever issues. */
static void
sc_card(sc_t *sc, const uint8_t *apdu, int len)
{
    uint8_t body[SC_RECORD + 2];

    if (len < 2) {
        body[0] = 0x6D;
        body[1] = 0x00;
        sc_reply_frame(sc, body, 2);
        return;
    }

    switch (apdu[1]) {
        case 0xA4: /* SELECT FILE: an FCI the guest never looks at, then 9000 */
            memset(body, 0, 22);
            body[22] = 0x90;
            body[23] = 0x00;
            sc_reply_frame(sc, body, 24); /* 28 bytes on the wire */
            break;

        case 0x20: /* VERIFY, PIN 01..08 */
            body[0] = 0x90;
            body[1] = 0x00;
            sc_reply_frame(sc, body, 2); /* 6 bytes */
            break;

        case 0xB0: /* READ BINARY, 100 bytes from offset 0 */
            sc_lcg(body, sc->record, SC_RECORD);
            body[SC_RECORD]     = 0x90;
            body[SC_RECORD + 1] = 0x00;
            sc_reply_frame(sc, body, SC_RECORD + 2); /* 106 bytes */
            sc_log("SC: READ BINARY -> \"%.3s\" / \"%.2s\" / \"%.6s\"\n",
                   sc->record, sc->record + 3, sc->record + 0x13);
            break;

        default:
            sc_log("SC: unhandled INS %02X\n", apdu[1]);
            body[0] = 0x6D;
            body[1] = 0x00;
            sc_reply_frame(sc, body, 2);
            break;
    }
}

/* A complete frame has been deobfuscated into sc->in.  Byte 4 is 00 for a command
   addressed to the reader and the body length for a frame addressed to the card. */
static void
sc_frame(sc_t *sc)
{
    const uint8_t r2 = sc->in[2];
    const uint8_t r3 = sc->in[3];

    sc->n_frames++;

    if (sc->in[4] == 0x00) {
        const uint8_t cmd = sc->in[5];
        const uint8_t arg = sc->in[6];

        /* Command 4 is the one case that answers under a fixed seed. */
        if (cmd == 0x04)
            sc_rx_seed(&sc->rx, 0xAB, 0xD9);
        else
            sc_rx_seed(&sc->rx, r2, r3);

        if (cmd == 0x0A) {
            /* The reader's hello.  The guest counts the bytes and reads none of
               them, so the string is ours to choose -- but there must be nine. */
            sc_log("SC: reader hello (cmd %02X arg %02X)\n", cmd, arg);
            sc_reply(sc, (const uint8_t *) "PEEPEEBOX", 9);
        } else
            sc_log("SC: unhandled reader command %02X arg %02X\n", cmd, arg);
        return;
    }

    sc_rx_seed(&sc->rx, r2, r3);

    {
        const uint8_t ctrl = sc->in[6];
        const int     len  = sc->in[7];

        if (ctrl & 0x80) {
            /* Addressed to the reader rather than the card: echo the payload.  The
               guest checks nothing but that five bytes arrive and XOR to zero. */
            sc_log("SC: link frame, payload %02X\n", sc->in[8]);
            sc_reply_frame(sc, &sc->in[8], 1);
        } else if ((len >= 0) && ((8 + len) <= SC_IN_MAX))
            sc_card(sc, &sc->in[8], len);
    }
}

/* One byte written to the reader's transmit register. */
static void
sc_write(UNUSED(serial_t *serial), void *priv, uint8_t data)
{
    sc_t *sc = (sc_t *) priv;

    timer_on_auto(&sc->gap_timer, SC_GAP_US);

    if (sc->n >= SC_IN_MAX) {
        sc->n    = 0;
        sc->want = 0;
    }

    if (sc->n < 2) {
        /* The nonce halves travel in the clear; the second one seeds the stream. */
        sc->in[sc->n] = data;
        if (sc->n == 1)
            sc_tx_seed(&sc->tx, sc->in[0], sc->in[1]);
    } else
        sc->in[sc->n] = (uint8_t) (data ^ sc_tx_next(&sc->tx));

    sc->n++;

    if (sc->n == 5) {
        /* Byte 4 decides the frame kind, and with it how much more to expect. */
        sc->want = (sc->in[4] == 0x00) ? 8 : (5 + sc->in[4]);
        if (sc->want > SC_IN_MAX) {
            sc_log("SC: frame length %d out of range, resynchronising\n", sc->want);
            sc->n    = 0;
            sc->want = 0;
        }
    }

    if (sc->want && (sc->n >= sc->want)) {
        sc_frame(sc);
        sc->n    = 0;
        sc->want = 0;
    }
}

/* A lull on the line means whatever we are holding is not the start of a frame: the
   wake byte the guest writes while setting the port up, or an abandoned retry. */
static void
sc_gap(void *priv)
{
    sc_t *sc = (sc_t *) priv;

    if (sc->n != 0)
        sc_log("SC: %d stray byte(s) discarded on line idle\n", sc->n);
    sc->n    = 0;
    sc->want = 0;
}

/* Hand the queued reply over one byte at a time, at the rate the wire would. */
static void
sc_write_to_host(void *priv)
{
    sc_t *sc = (sc_t *) priv;

    if (sc->serial == NULL)
        goto again;

    if ((sc->serial->type >= SERIAL_16550) && sc->serial->fifo_enabled) {
        if (fifo_get_full(sc->serial->rcvr_fifo))
            goto again;
    } else if (sc->serial->lsr & 1)
        goto again;

    if (fifo8_num_used(&sc->resp))
        serial_write_fifo(sc->serial, fifo8_pop(&sc->resp));

again:
    timer_on_auto(&sc->tx_timer, (1000000.0 / (double) SC_BAUD) * (1 + 8 + 1));
}

/* ------------------------------------------------------------------------------------
 * The record
 * ---------------------------------------------------------------------------------- */

/* "Version 2008 (ES)" -> "IGO 08", which is what the 2008 games compare the record's
   title field against.
 *
 * Take the version TOKEN -- the text between "Version " and the territory -- and drop
 * a leading "20" from it.  For every 2008 image that is "2008" -> "08", exactly what
 * the digit-run rule produced before, so those records are unchanged.  It also carries
 * a suffix, which the digit rule silently dropped: the I.G.O. Italy image asks for
 * "Version 08IT (IT)" and needs the token "08IT", where "IGO 08" composed
 * "Version 08 (IT)" and the guest reported NDONGLE not found. */
static void
sc_title_from_banner(const char *banner, char *out, size_t outsz)
{
    const char *v = strstr(banner, "Version ");
    char        tok[16];
    size_t      n = 0;

    if (v != NULL) {
        v += 8;
        while (v[n] && (v[n] != ' ') && (v[n] != '(') && (n < sizeof(tok) - 1))
            n++;
        memcpy(tok, v, n);
    }
    tok[n] = 0;

    /* "2008" -> "08"; a bare "08IT" is already the token */
    if ((n > 2) && (tok[0] == '2') && (tok[1] == '0'))
        memmove(tok, tok + 2, n - 1);

    if (tok[0])
        snprintf(out, outsz, "IGO %s", tok);
    else
        snprintf(out, outsz, "IGO 08");
}

/* Pull the two halves out of the banner the disk image itself carries.  A cabinet's
   dongle always matched the disk it shipped with, so MAIN.SET is the authority --
   never the image's filename, which lies often enough to have its own warning in
   Docs/08. */
static void
sc_build_record(sc_t *sc)
{
    char banner[64] = "";
    char terr[16]   = "";
    char title[16];
    char code[3];

    if (!photoplay_image_ident(banner, sizeof(banner), terr, sizeof(terr)))
        sc_log("SC: the disk image did not identify itself; serving the ES default\n");

    snprintf(code, sizeof(code), "%s", terr[0] ? terr : "ES");
    sc_title_from_banner(banner, title, sizeof(title));

    memset(sc->record, 0, sizeof(sc->record));
    memcpy(sc->record + 0x00, "IGO", 3);
    memcpy(sc->record + 0x03, code, strlen(code));
    memcpy(sc->record + 0x13, title, (strlen(title) > 8) ? 8 : strlen(title));

    sc_log("SC: record \"IGO\" / \"%s\" / \"%s\", so the guest composes"
           " \"Version %s (%s)\"\n",
           code, title, title + 4, code);
    if (banner[0])
        sc_log("SC: the image's MAIN.SET asks for \"%s\"\n", banner);
}

/* ------------------------------------------------------------------------------------
 * Device plumbing
 * ---------------------------------------------------------------------------------- */

static void *
sc_init(UNUSED(const device_t *info))
{
    sc_t *sc = calloc(1, sizeof(sc_t));

    if (sc == NULL)
        return NULL;

    sc->serial = serial_attach(SC_PORT, NULL, sc_write, sc);
    if (sc->serial == NULL) {
        /* Nothing else in the cabinet wants COM2, so this only happens if the
           profile has been changed out from under us. */
        sc_log("SC: COM%d unavailable, the 2008 card reader is NOT attached\n",
               SC_PORT + 1);
        free(sc);
        return NULL;
    }

    fifo8_create(&sc->resp, 256);
    timer_add(&sc->tx_timer, sc_write_to_host, sc, 0);
    timer_add(&sc->gap_timer, sc_gap, sc, 0);
    timer_on_auto(&sc->tx_timer, (1000000.0 / (double) SC_BAUD) * (1 + 8 + 1));

    /* Where the reply codec sits before the first frame re-seeds it. */
    sc_rx_seed(&sc->rx, 0xAB, 0xD9);

    sc_build_record(sc);

    sc_log("SC: 2008 card reader attached to COM%d (%03Xh), %d baud\n",
           SC_PORT + 1, COM2_ADDR, SC_BAUD);
    return sc;
}

static void
sc_close(void *priv)
{
    sc_t *sc = (sc_t *) priv;

    if (sc == NULL)
        return;

    timer_stop(&sc->tx_timer);
    timer_stop(&sc->gap_timer);
    fifo8_destroy(&sc->resp);
    sc_log("SC: detached after %d frames\n", sc->n_frames);
    free(sc);
}

const device_t igo8_reader_device = {
    .name          = "I.G.O. 2008 Card Reader Dongle",
    .internal_name = "dongle_igo8",
    .flags         = 0,
    .local         = 0,
    .init          = sc_init,
    .close         = sc_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
