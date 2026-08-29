/*
 * PeepeeBox - funworld Photo Play / I.G.O. protection dongle emulation.
 *
 * NOT a HASP.  Earlier notes here and in Docs/01, 04, 07, 09 called it one and said the
 * later generations reached it through Aladdin's linked-in library.  That was an
 * assumption and it is wrong (Docs/13).  Five of these dongles were dumped and their
 * firmware disassembled and executed (Docs/12): the 1999 device is funworld's own
 * two-chip design -- an AT89C2051-class 8051 holding all the logic, plus a 24Cxx I2C
 * EEPROM holding the licence record.  "H" is merely one of ten dongle types MENU.EXE
 * learned to probe for over the years, and no generation has been shown to use one.
 *
 * The dongle firmware confirmed the transport and grammar below line for line, from the
 * device's side, having been recovered originally from the game binaries' side.
 *
 * TRANSPORT (recovered from the 1999-generation game binaries, which bit-bang the
 * parallel port inline; independently confirmed from the dongle firmware, Docs/12):
 *
 *   host -> dongle : two nibbles per byte, low first, on DATA bits 0-3 (bits 5-7 held
 *                    high, bit 4 low), each latched on a STROBE rising edge.
 *   dongle -> host : two nibbles per byte, low first, on STATUS bits 3-6, with STATUS
 *                    bit 7 (BUSY) as the ready flag and DATA bit 4 as the host ack:
 *                       BUSY=1, host reads nibble, host raises ack -> BUSY=0,
 *                       host drops ack -> next nibble, BUSY=1 ...
 *
 * GRAMMAR (recovered by executing the real routines offline, scripts/hasp99sim.py):
 *
 *   Every transaction begins with a type byte.  Two 4-entry tables in the game's data
 *   segment give the lengths -- DS:0x2283 for the send count, DS:0x2287 for the receive
 *   count -- both indexed by that type byte:
 *
 *       type 0 : send  0, receive  0
 *       type 1 : send 10, receive  4
 *       type 2 : send 50, receive  0
 *       type 3 : send  2, receive 48       <- the one the boot path uses
 *
 *   Type 3 is a challenge/response.  read_hasp_block() at 0x1D452 does:
 *
 *       bufA[2] = 3;  bufA[3] = rand() & 0xFF;      /_ the nonce _/
 *       poll();                                     /_ send {03,nonce}, read 48 _/
 *       decrypt(0, nonce);                          /_ 0x1D206 _/
 *       memcpy(dest, &bufA[2], 48);
 *
 *   and decrypt() is a plain XOR against a keystream seeded with the nonce:
 *
 *       for (i = 0; i < 48; i++) {
 *           buf[i] ^= k;
 *           k += 0x75;
 *           if (k < 0x28) k = 0xCB;
 *           if (k > 0xC8) k = 0x13;
 *       }
 *
 *   So the dongle must answer {03,nonce} with the 48-byte block XORed under that same
 *   keystream.  (It collapses to a 0x13/0x88 two-cycle after the first step; the nonce
 *   only really masks byte 0 and sets the phase.  Weak, but this is what the code does.)
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/lpt.h>

/* Protocol bring-up: log every transaction.  Remove once this is trusted. */
#define ENABLE_DONGLE_PHOTOPLAY_LOG 1

#define PP_OUT_MAX 512
#define PP_IN_MAX  256
#define PP_BLOCK   62 /* what KEYN.COM serves; type 3 returns the first 48 */

typedef struct {
    void *lpt;

    /* host -> dongle */
    uint8_t in_nib;
    int     in_have;
    uint8_t last_data;
    uint8_t last_ctrl;
    uint8_t cmd[PP_IN_MAX];
    int     cmd_len;
    int     have_strobe_hook; /* master calls pp_strobe: don't double-latch in write_ctrl */
    int     n_cmd;
    int     n_wd, n_wc, n_rs, n_rd;

    /* dongle -> host, as a queue of nibbles */
    uint8_t out[PP_OUT_MAX];
    int     out_len;
    int     out_pos;
    int     busy;
    int     idle_polls;

    /* NG-DONGLE sweep (Docs/09): the 2008 generation probes with a 5A/A5 pattern and
       then reads STATUS exactly once; that byte decides the verdict.  When enabled we
       answer that first read with a value taken from ngsweep.txt and pre-increment the
       file, so simply rebooting walks the whole 0..255 space unattended. */
    int     ng_sweep;
    uint8_t ng_val;
    int     ng_data;   /* sweep DATA readback transforms instead */
    int     ng_mode;

    uint8_t block[PP_BLOCK];
} pp_t;

#define PP_SWEEP_FILE "ngsweep.txt"

/* bytes the host sends / expects back, indexed by the type byte */
static const int pp_send_len[4] = { 0, 10, 50, 2 };
static const int pp_recv_len[4] = { 0, 4, 0, 48 };

/* The licence record, as the real 1999 dongles hold it in their EEPROM (Docs/12):

       char     banner[16];    NUL-terminated, exactly filling the field
       uint32_t v[8];          little-endian

   48 bytes, which is exactly what a type-3 read returns.

   This used to be built KEYN.COM's way -- banner padded to *30* bytes, then the
   dwords -- which put every dword 14 bytes too late.  That was not cosmetic.
   Docs/14 showed each 1999 photo game reads one specific dword straight out of
   this block and uses it as the LCG seed that decrypts its picture database:

       FINDIT  block+0x1C = v[3]
       MOSAIC  block+0x20 = v[4]
       FMEMO   block+0x24 = v[5]

   Under the old layout those offsets landed mid-dword, so a game got a wrong
   but non-zero key, took the decrypting path, and read noise.  That is the
   "photo games stall while loading pictures" defect previously written off as
   pre-existing and unrelated to the dongle.  It was the dongle.

   v[1]..v[6] are byte-identical on every unit dumped and across generations --
   funworld's fixed per-title content keys, not per-site values.  v[0] and v[7]
   do vary per unit and no game is known to read either; these are r3_alt's, so
   for a 1999 SP image this device now serves a block that is byte-for-byte that
   physical dongle's. */
static const uint32_t pp_dwords[8] = {
    0x00000000, /* v[0]  per-unit; uninitialised host memory on a real dongle */
    0x0000038B, /* v[1] */
    0x000181CD, /* v[2] */
    0x0001D760, /* v[3]  FINDIT picture database key */
    0x00029B92, /* v[4]  MOSAIC picture database key */
    0x0001287E, /* v[5]  FMEMO  picture database key */
    0x0000089D, /* v[6] */
    0xBAE8A135  /* v[7]  per-unit */
};

/* A 1999 banner is "Version 99 (XX)" -- 15 characters, so the 16-byte field is
   exact.  The later banners offered below do not fit, and they belong to dongle
   families this device is not (Docs/13); for those the block keeps the old
   30-byte-banner layout so that whatever worked before still does. */
#define PP_BANNER_1999 16
#define PP_BANNER_KEYN 30

/* The selectable banners.  The value is an index into pp_banners[]. */
static const char *pp_banners[] = {
    "Version 99",   "Version 2000", "Version 2001", "Version 2002",
    "Version 2003", "Version 2004", "Version 2005B", "Version 2006",
    "Version 2007", "Version 2008"
};
#define PP_NBANNERS ((int) (sizeof(pp_banners) / sizeof(pp_banners[0])))

static const char *pp_terrs[] = { "AT", "DE", "ES", "IT", "NL", "PT", "SE", "ZA", "GB", "FR" };
#define PP_NTERRS ((int) (sizeof(pp_terrs) / sizeof(pp_terrs[0])))

#ifdef ENABLE_DONGLE_PHOTOPLAY_LOG
int dongle_photoplay_do_log = ENABLE_DONGLE_PHOTOPLAY_LOG;

static void
pp_log(const char *fmt, ...)
{
    va_list ap;

    if (dongle_photoplay_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define pp_log(fmt, ...)
#endif

/* the game's keystream: seeded with the nonce, then a clamped +0x75 walk */
static uint8_t
pp_next_key(uint8_t k)
{
    k = (uint8_t) (k + 0x75);
    if (k < 0x28)
        k = 0xCB;
    if (k > 0xC8)
        k = 0x13;
    return k;
}

/* the 4-byte keystream type 1 answers under: a clamped +0x25 walk (Docs/12) */
static uint8_t
pp_next_key1(uint8_t k)
{
    k = (uint8_t) (k + 0x25);
    if (k < 0x1E)
        k = 0x7B;
    if (k > 0xAE)
        k = 0x17;
    return k;
}

/* Type 1: a keyed hash of the 8-byte name the host sends, recovered from the
   dongle firmware (Docs/12).  It touches no EEPROM state at all -- the answer is
   a pure function of the name and the nonce, identical on every dongle of this
   generation, which is why it can be reproduced exactly here.  All arithmetic is
   mod 256.  No 1999 binary is known to call it; this is correctness, not a
   dependency. */
static void
pp_type1(const uint8_t *name, uint8_t *out)
{
    uint8_t v0 = (uint8_t) (4 * name[0] + 0x11 + 3 * name[1]);
    uint8_t v1 = (uint8_t) (7 * name[2] + 0xA7 + 2 * name[3]);
    uint8_t v2 = (uint8_t) (4 * name[4] + 0x75 + 7 * name[5]);
    uint8_t v3 = (uint8_t) (name[6] + 0x17 + 4 * name[7]);

    /* one extra round, selected by the data itself */
    switch ((v0 + v1) & 3) {
        case 0:
            v3 = (uint8_t) (6 * v3 + v1 + 0x75);
            break;
        case 1:
            v2 = (uint8_t) (v0 + 2 * v3 + 0x0C);
            break;
        case 2:
            v1 = (uint8_t) (4 * v0 + 0x37 + 4 * v1);
            break;
        default:
            v0 = (uint8_t) (5 * v2 + 0x64);
            break;
    }

    out[0] = v0;
    out[1] = v1;
    out[2] = v2;
    out[3] = v3;
}

static void
pp_queue(pp_t *dev, uint8_t b)
{
    if (dev->out_len + 2 > PP_OUT_MAX)
        return;
    dev->out[dev->out_len++] = b & 0x0f;
    dev->out[dev->out_len++] = (b >> 4) & 0x0f;
}

/* answer a completed command */
static void
pp_respond(pp_t *dev)
{
    const uint8_t type = dev->cmd[0];

    dev->out_len = 0;
    dev->out_pos = 0;

    if (type == 3) {
        /* challenge/response: encrypt the block under keystream(nonce) */
        uint8_t k = dev->cmd[1];

        for (int i = 0; i < pp_recv_len[3]; i++) {
            pp_queue(dev, (uint8_t) (dev->block[i] ^ k));
            k = pp_next_key(k);
        }
        pp_log("PP: type 3, nonce %02X -> 48 encrypted bytes\n", dev->cmd[1]);
    } else if (type == 1) {
        /* keyed hash of the 8-byte name, then XORed under the 4-byte keystream */
        uint8_t h[4];
        uint8_t k = dev->cmd[1];

        pp_type1(&dev->cmd[2], h);
        for (int i = 0; i < pp_recv_len[1]; i++) {
            pp_queue(dev, (uint8_t) (h[i] ^ k));
            k = pp_next_key1(k);
        }
        pp_log("PP: type 1, nonce %02X, name %02X%02X%02X%02X%02X%02X%02X%02X"
               " -> %02X %02X %02X %02X\n", dev->cmd[1],
               dev->cmd[2], dev->cmd[3], dev->cmd[4], dev->cmd[5],
               dev->cmd[6], dev->cmd[7], dev->cmd[8], dev->cmd[9],
               h[0], h[1], h[2], h[3]);
    } else if (type == 2) {
        /* Programming: the host sends the 48-byte record encrypted under the same
           keystream type 3 answers with, and the dongle writes it to EEPROM.  The
           XOR is its own inverse.  There is no authentication on this command --
           that is how the real hardware behaves (Docs/12).

           Applied in memory only: a real dongle keeps it, but here the profile
           rebuilds the block from the configured banner on every hard reset, and
           nothing but funworld's own programming tool is known to send this. */
        uint8_t k = dev->cmd[1];

        for (int i = 0; i < 48; i++) {
            dev->block[i] = (uint8_t) (dev->cmd[2 + i] ^ k);
            k = pp_next_key(k);
        }
        pp_log("PP: type 2, record reprogrammed in memory, banner now \"%.16s\"\n",
               (const char *) dev->block);
    } else {
        /* type 0 and type 2 expect nothing back */
        pp_log("PP: type %d -- no response expected\n", type);
    }

    dev->busy = (dev->out_len > 0);
}

static void
pp_host_byte(pp_t *dev, uint8_t b)
{
    if (dev->cmd_len == 0 && b > 3) {
        /* not a valid type byte: we are out of sync, drop it rather than framing garbage */
        pp_log("PP: resync, discarding stray %02X\n", b);
        return;
    }

    if (dev->cmd_len < PP_IN_MAX)
        dev->cmd[dev->cmd_len++] = b;

    if (dev->n_cmd++ < 120)
        pp_log("PP: host->dongle %02X (byte %d of type %d)\n", b, dev->cmd_len, dev->cmd[0]);

    if (dev->cmd_len >= pp_send_len[dev->cmd[0]]) {
        pp_respond(dev);
        dev->cmd_len = 0;
    }
}

static void
pp_latch_nibble(pp_t *dev)
{
    /* Read the data register straight off the port.  86Box stores every DATA write in
       lpt->dat but only calls write_data when the port is not in bidirectional-input
       mode -- and the guest keeps control bit 5 set, so that callback never fires. */
    const uint8_t cur = (dev->lpt != NULL) ? ((lpt_t *) dev->lpt)->dat : dev->last_data;
    const uint8_t nib = cur & 0x0f;

    if (dev->in_have == 0) {
        dev->in_nib  = nib;
        dev->in_have = 1;
    } else {
        pp_host_byte(dev, (uint8_t) (dev->in_nib | (nib << 4)));
        dev->in_have = 0;
    }
}

/* the read handshake: DATA bit 4 is the host's acknowledge */
static void
pp_ack_edge(pp_t *dev, uint8_t cur)
{
    const uint8_t was = dev->last_data & 0x10;
    const uint8_t now = cur & 0x10;

    if (!was && now) {
        if (dev->out_pos < dev->out_len) {
            dev->out_pos++;
            /* the decisive signal: the host took every nibble we offered */
            if (dev->out_pos == dev->out_len && dev->out_len > 0)
                pp_log("PP: *** host drained all %d nibbles (%d bytes) ***\n",
                       dev->out_len, dev->out_len / 2);
        }
        dev->busy = 0;
    } else if (was && !now)
        dev->busy = (dev->out_pos < dev->out_len);

    dev->last_data = cur;
}

static void
pp_write_data(uint8_t val, void *priv)
{
    pp_t *dev = (pp_t *) priv;

    /* Raw trace.  The 2001+ generations reach the dongle through Aladdin's library, whose
       framing is not known -- if it differs from the 1999 bit-bang, byte-level logging
       shows nothing at all and looks deceptively like "no traffic".  Log the first
       few hundred raw accesses so the real wire behaviour is visible either way. */
    if (dev->n_wd++ < 300)
        pp_log("PP: raw write_data %02X\n", val);
    pp_ack_edge(dev, val);
}

static void
pp_write_ctrl(uint8_t val, void *priv)
{
    pp_t *dev = (pp_t *) priv;

    /* hasp_init() toggles CTRL bits 2 and 3 (and raises STROBE while doing so, which
       would otherwise latch a bogus leading nibble and put every following byte one
       nibble out of phase).  Treat any bit 2/3 movement as "the link is being reset"
       and drop all framing state. */
    if (dev->n_wc++ < 300)
        pp_log("PP: raw write_ctrl %02X\n", val);

    if ((val ^ dev->last_ctrl) & 0x0c) {
        dev->in_have = 0;
        dev->cmd_len = 0;
        dev->out_len = 0;
        dev->out_pos = 0;
        dev->busy    = 0;
        dev->last_ctrl = val;
        return;
    }

    /* STROBE (bit 0) rising edge latches the nibble on DATA bits 0-3.  Only do it here
       if the master strobe hook is not already delivering the transition, or every
       nibble would be latched twice. */
    if (!dev->have_strobe_hook && !(dev->last_ctrl & 0x01) && (val & 0x01))
        pp_latch_nibble(dev);

    dev->last_ctrl = val;
}

static void
pp_strobe(uint8_t old, uint8_t val, void *priv)
{
    pp_t *dev = (pp_t *) priv;

    dev->have_strobe_hook = 1;
    if (!(old & 0x01) && (val & 0x01))
        pp_latch_nibble(dev);
}

static uint8_t
pp_read_status(void *priv)
{
    pp_t   *dev = (pp_t *) priv;
    uint8_t st  = 0;

    /* The ack line reaches us the same way the data nibbles do, so sample it from the
       port here rather than relying on write_data edges that 86Box filters out. */
    if (dev->lpt != NULL)
        pp_ack_edge(dev, ((lpt_t *) dev->lpt)->dat);

    if (dev->n_rs++ < 60)
        pp_log("PP: raw read_status #%d\n", dev->n_rs);

    /* NG-DONGLE sweep: answer the probe's single STATUS read with the swept value. */
    if (dev->ng_sweep && dev->n_rs == 1) {
        pp_log("PP: NG sweep -- answering probe with STATUS %02X\n", dev->ng_val);
        return dev->ng_val;
    }

    if (dev->busy) {
        if (dev->out_pos < dev->out_len)
            st |= (uint8_t) ((dev->out[dev->out_pos] & 0x0f) << 3);
        st |= 0x80; /* BUSY */
    } else if (dev->out_pos >= dev->out_len) {
        /* Diagnostic: the guest is polling us with nothing left to give.  A few of these
           are normal at the end of a transfer; a flood means it is waiting on data we do
           not know to send. */
        if ((++dev->idle_polls % 200000) == 0)
            pp_log("PP: guest has polled STATUS %d times with an empty queue\n",
                   dev->idle_polls);
    }

    return st;
}

static uint8_t
pp_read_ctrl(void *priv)
{
    const pp_t *dev = (const pp_t *) priv;

    return dev->last_ctrl;
}

/* DATA-line readback (PeepeeBox extension -- stock 86Box always returns its own write
   latch).  The 2008 NG-DONGLE probe writes 5A / A5 complement pairs, which is the shape
   of a presence test that looks for the cable altering the echo -- exactly how the
   DS1982 reset detects a slave.  Sweeping all 32 meaningful STATUS values changed
   nothing, so measure whether the guest reads DATA back at all before modelling it. */
/* Candidate ways a dongle on the cable could alter the readback.  Index 0 is the stock
   pass-through, which is known to fail.  Selected by ngsweep.txt when ngdata is on. */
static uint8_t
pp_data_transform(int mode, uint8_t x)
{
    switch (mode) {
        case 1:  return (uint8_t) ~x;
        case 2:  return 0x00;
        case 3:  return 0xFF;
        case 4:  return (uint8_t) (x | 0x0F);
        case 5:  return (uint8_t) (x & 0xF0);
        case 6:  return (uint8_t) (x ^ 0x0F);
        case 7:  return (uint8_t) (x ^ 0xF0);
        case 8:  return (uint8_t) ((x >> 4) | (x << 4));   /* nibble swap */
        case 9:  return (uint8_t) (x >> 1);
        case 10: return (uint8_t) (x << 1);
        case 11: return (uint8_t) (x & 0x7F);
        case 12: return (uint8_t) (x | 0x80);
        case 13: return (uint8_t) (x ^ 0x5A);
        case 14: return (uint8_t) (x ^ 0xA5);
        case 15: return (uint8_t) (x & 0x0F);
        case 16: return (uint8_t) (x | 0xF0);
        case 17: return (uint8_t) (x ^ 0x01);
        case 18: return (uint8_t) (x ^ 0x80);
        case 19: return (uint8_t) (x & 0xFE);
        default: return x;                                  /* 0 = pass-through */
    }
}
#define PP_NTRANSFORMS 20

static uint8_t
pp_read_data(void *priv)
{
    pp_t         *dev = (pp_t *) priv;
    const uint8_t latch = (dev->lpt != NULL) ? ((lpt_t *) dev->lpt)->dat : dev->last_data;
    const uint8_t out   = dev->ng_data ? pp_data_transform(dev->ng_mode, latch) : latch;

    if (dev->n_rd++ < 200)
        pp_log("PP: raw read_data latch %02X -> %02X (mode %d)\n", latch, out, dev->ng_mode);

    return out;
}

/* ------------------------------------------------------------------------------------
 * The SECOND token: a Dallas DS1982 iButton on 1-Wire, reached through a 16550-class
 * UART at I/O 0x268 (Maxim AN214 "1-Wire over a UART").  Both tokens are mandatory --
 * the games abort with "DS1982 not found" if this one is missing, no matter how well the
 * HASP half answers.  Full spec in Docs/05; this is a direct port of the state machine in
 * scripts/ds1982sim.py, which was validated against the real software on 2026-08-27.
 *
 * On the wire the host only ever sends three literal byte values, so no baud tracking is
 * needed to tell them apart:
 *     0xF0  reset pulse       -> answer anything != 0xF0 to signal a slave is present
 *     0xFF  read-slot / write-1
 *     0x00  write-0
 * Every UART byte carries exactly one 1-Wire bit, LSB first; bit 0 of the byte we hand
 * back is the level of the wire.
 * ---------------------------------------------------------------------------------- */

#define IB_BASE    0x268
#define IB_MEMSIZE 128

/* Maxim/Dallas CRC8, reflected polynomial 0x8C -- byte-identical to the table the games
   carry at DS:0x2176. */
static uint8_t ib_crc8_tab[256];
static int     ib_crc8_ready = 0;

static void
ib_crc8_init(void)
{
    if (ib_crc8_ready)
        return;
    for (int i = 0; i < 256; i++) {
        uint8_t c = (uint8_t) i;
        for (int b = 0; b < 8; b++)
            c = (c & 1) ? (uint8_t) ((c >> 1) ^ 0x8C) : (uint8_t) (c >> 1);
        ib_crc8_tab[i] = c;
    }
    ib_crc8_ready = 1;
}

static uint8_t
ib_crc8(const uint8_t *d, int n, uint8_t crc)
{
    for (int i = 0; i < n; i++)
        crc = ib_crc8_tab[crc ^ d[i]];
    return crc;
}

enum { IB_ST_CMD = 0, IB_ST_TA, IB_ST_DONE };

typedef struct {
    /* minimal 16550 */
    uint8_t lcr, mcr, ier, scr, dll, dlm;
    uint8_t rbr;
    int     rx_full;

    /* 1-Wire slave */
    uint8_t rom[8];
    uint8_t mem[IB_MEMSIZE];
    uint8_t inbits;   /* bits assembled from the host so far  */
    int     nbits;
    uint8_t outbuf[IB_MEMSIZE + 16]; /* bytes queued to shift out */
    int     out_len;
    int     out_pos;  /* bit cursor into outbuf */
    int     state;
    uint8_t ta[2];
    int     nta;
    int     idle_polls;
} ib_t;

static ib_t ib_dev;

static void
ib_reset_state(ib_t *ib)
{
    ib->inbits = 0;
    ib->nbits  = 0;
    ib->out_len = ib->out_pos = 0;
    ib->state  = IB_ST_CMD;
    ib->nta    = 0;
}

static void
ib_queue(ib_t *ib, const uint8_t *d, int n)
{
    if (n > (int) sizeof(ib->outbuf))
        n = (int) sizeof(ib->outbuf);
    memcpy(ib->outbuf, d, n);
    ib->out_len = n;
    ib->out_pos = 0;
}

/* a whole byte arrived from the host */
static void
ib_on_byte(ib_t *ib, uint8_t val)
{
    switch (ib->state) {
        case IB_ST_CMD:
            if (val == 0x33) { /* READ ROM */
                pp_log("IB: READ ROM\n");
                ib_queue(ib, ib->rom, 8);
                ib->state = IB_ST_DONE;
            } else if (val == 0xCC) { /* SKIP ROM -- another command follows */
                pp_log("IB: SKIP ROM\n");
                ib->state = IB_ST_CMD;
            } else if (val == 0xF0) { /* READ MEMORY */
                pp_log("IB: READ MEMORY\n");
                ib->nta   = 0;
                ib->state = IB_ST_TA;
            } else {
                pp_log("IB: unhandled command %02X\n", val);
                ib->state = IB_ST_DONE;
            }
            break;

        case IB_ST_TA:
            ib->ta[ib->nta++] = val;
            if (ib->nta == 2) {
                const int ta  = ib->ta[0] | (ib->ta[1] << 8);
                uint8_t   hdr[3] = { 0xF0, ib->ta[0], ib->ta[1] };
                uint8_t   buf[IB_MEMSIZE + 1];
                int       n = 0;

                /* the device answers with its CRC8 of command+address, then the page */
                buf[n++] = ib_crc8(hdr, 3, 0);
                if (ta < IB_MEMSIZE) {
                    memcpy(buf + n, ib->mem + ta, (size_t) (IB_MEMSIZE - ta));
                    n += IB_MEMSIZE - ta;
                }
                pp_log("IB: addr %04X, crc %02X, streaming %d bytes\n", ta, buf[0], n - 1);
                ib_queue(ib, buf, n);
                ib->state = IB_ST_DONE;
            }
            break;

        default:
            break;
    }
}

/* one UART byte = one 1-Wire bit slot; returns the byte to hand back */
static uint8_t
ib_on_slot(ib_t *ib, uint8_t host)
{
    if (ib->out_pos < ib->out_len * 8) {
        /* we are driving the wire: present the next queued bit, LSB first */
        const int bit = (ib->outbuf[ib->out_pos >> 3] >> (ib->out_pos & 7)) & 1;
        ib->out_pos++;
        return bit ? 0xFF : 0x00;
    }

    /* otherwise the host is writing a bit to us */
    if (host)
        ib->inbits |= (uint8_t) (1 << ib->nbits);
    if (++ib->nbits == 8) {
        const uint8_t v = ib->inbits;
        ib->inbits = 0;
        ib->nbits  = 0;
        ib_on_byte(ib, v);
    }
    return host ? 0xFF : 0x00;
}

static void
ib_tx(ib_t *ib, uint8_t val)
{
    if (val == 0xF0) {
        /* reset pulse: a present slave corrupts the echo */
        ib_reset_state(ib);
        ib->rbr = 0xE0;
    } else
        ib->rbr = ib_on_slot(ib, val);

    ib->rx_full = 1;
}

static void
ib_out(uint16_t port, uint8_t val, void *priv)
{
    ib_t *ib = (ib_t *) priv;

    switch (port - IB_BASE) {
        case 0:
            if (ib->lcr & 0x80)
                ib->dll = val;
            else
                ib_tx(ib, val);
            break;
        case 1:
            if (ib->lcr & 0x80)
                ib->dlm = val;
            else
                ib->ier = val;
            break;
        case 3:
            ib->lcr = val;
            break;
        case 4:
            ib->mcr = val;
            break;
        case 7:
            ib->scr = val;
            break;
        default: /* FCR and the read-only registers */
            break;
    }
}

static uint8_t
ib_in(uint16_t port, void *priv)
{
    ib_t *ib = (ib_t *) priv;

    switch (port - IB_BASE) {
        case 0:
            if (ib->lcr & 0x80)
                return ib->dll;
            ib->rx_full = 0;
            return ib->rbr;
        case 1:
            return (ib->lcr & 0x80) ? ib->dlm : ib->ier;
        case 2:
            return 0x01; /* no interrupt pending */
        case 3:
            return ib->lcr;
        case 4:
            return ib->mcr;
        case 5:
            /* THRE|TEMT are always set -- we consume instantly -- plus DR when a byte
               is waiting.  The games poll exactly these bits and use no interrupts. */
            if (!ib->rx_full && (++ib->idle_polls % 200000) == 0)
                pp_log("IB: guest has polled LSR %d times with no byte pending\n",
                       ib->idle_polls);
            return (uint8_t) (0x60 | (ib->rx_full ? 0x01 : 0x00));
        case 6:
            return 0xB0; /* DSR|CTS|DCD asserted */
        case 7:
            return ib->scr;
        default:
            return 0xFF;
    }
}

static void
ib_start(void)
{
    ib_t *ib = &ib_dev;
    /* ROM: family 0x09 (DS1982/DS2502), 6-byte serial, CRC8 over all 8 == 0.
       The games check only the CRC -- the serial is never compared. */
    static const uint8_t serial[6] = { 0x50, 0x50, 0x42, 0x4F, 0x58, 0x00 };
    static const char    text[]    = "Photo Play 2000 Version 3";

    ib_crc8_init();
    memset(ib, 0, sizeof(*ib));

    ib->rom[0] = 0x09;
    memcpy(ib->rom + 1, serial, 6);
    ib->rom[7] = ib_crc8(ib->rom, 7, 0);

    /* the page: the games compare memory[5:] against this string */
    memcpy(ib->mem + 5, text, strlen(text));

    ib_reset_state(ib);

    io_sethandler(IB_BASE, 8, ib_in, NULL, NULL, ib_out, NULL, NULL, ib);
    pp_log("IB: DS1982 iButton at I/O %03X, ROM %02X %02X %02X %02X %02X %02X %02X %02X\n",
           IB_BASE, ib->rom[0], ib->rom[1], ib->rom[2], ib->rom[3],
           ib->rom[4], ib->rom[5], ib->rom[6], ib->rom[7]);
}

static void *
pp_init(const device_t *info)
{
    pp_t      *dev = calloc(1, sizeof(pp_t));
    const int  bi  = device_get_config_int("banner");
    const int  ti  = device_get_config_int("territory");
    char       banner[31];

    snprintf(banner, sizeof(banner), "%s (%s)",
             pp_banners[(bi >= 0 && bi < PP_NBANNERS) ? bi : 0],
             pp_terrs[(ti >= 0 && ti < PP_NTERRS) ? ti : 0]);

    /* Lay the record out the way the hardware does. */
    const size_t blen = strlen(banner);
    const size_t voff = (blen < PP_BANNER_1999) ? PP_BANNER_1999 : PP_BANNER_KEYN;

    memset(dev->block, 0, sizeof(dev->block));
    memcpy(dev->block, banner, blen);
    for (size_t n = 0; n < 8; n++) {
        const size_t o = voff + (n * 4);

        if ((o + 4) > sizeof(dev->block))
            break;
        dev->block[o]     = (uint8_t) (pp_dwords[n]);
        dev->block[o + 1] = (uint8_t) (pp_dwords[n] >> 8);
        dev->block[o + 2] = (uint8_t) (pp_dwords[n] >> 16);
        dev->block[o + 3] = (uint8_t) (pp_dwords[n] >> 24);
    }

    pp_log("PP: Photo Play dongle attached, banner \"%s\", dwords at +%02X\n",
           banner, (unsigned) voff);

    dev->lpt = lpt_attach(pp_write_data, pp_write_ctrl, pp_strobe,
                          pp_read_status, pp_read_ctrl, NULL, NULL, dev);

    /* The cabinets use a plain SPP port: control bit 5 is a don't-care there and the
       data lines are always driven.  86Box otherwise treats bit 5 as the bidirectional
       direction bit and suppresses write_data -- and bit 5 is exactly what the guest
       leaves set, so without this the dongle never sees a single data nibble. */
    if (dev->lpt != NULL) {
        lpt_set_ext((lpt_t *) dev->lpt, 0);
        lpt_attach_read_data(pp_read_data);
    }

    /* Both tokens are mandatory, so bring up the iButton alongside the HASP half. */
    if (device_get_config_int("ibutton"))
        ib_start();

    /* NG-DONGLE sweep: take this run's candidate from the file and leave the next one
       behind, so an unattended reboot loop walks the whole space. */
    dev->ng_sweep = device_get_config_int("ngsweep");
    if (dev->ng_sweep) {
        FILE *f = fopen(PP_SWEEP_FILE, "r");
        int   v = 0;

        if (f != NULL) {
            if (fscanf(f, "%d", &v) != 1)
                v = 0;
            fclose(f);
        }
        dev->ng_val = (uint8_t) (v & 0xFF);

        f = fopen(PP_SWEEP_FILE, "w");
        if (f != NULL) {
            fprintf(f, "%d\n", (v + 1) & 0xFF);
            fclose(f);
        }
        pp_log("PP: NG sweep ARMED, this run answers STATUS %02X\n", dev->ng_val);
    }

    /* DATA-readback transform sweep: same file, same self-advancing trick.  The 2008
       probe writes 5A/A5 and reads each straight back, so what the cable does to that
       echo is what decides presence -- see Docs/09. */
    dev->ng_data = device_get_config_int("ngdata");
    if (dev->ng_data) {
        FILE *f = fopen(PP_SWEEP_FILE, "r");
        int   v = 0;

        if (f != NULL) {
            if (fscanf(f, "%d", &v) != 1)
                v = 0;
            fclose(f);
        }
        dev->ng_mode = v % PP_NTRANSFORMS;

        f = fopen(PP_SWEEP_FILE, "w");
        if (f != NULL) {
            fprintf(f, "%d\n", (v + 1) % PP_NTRANSFORMS);
            fclose(f);
        }
        pp_log("PP: NG DATA sweep ARMED, transform mode %d of %d\n",
               dev->ng_mode, PP_NTRANSFORMS);
    }

    return dev;
}

static void
pp_close(void *priv)
{
    pp_t *dev = (pp_t *) priv;

    pp_log("PP: detached after %d command bytes\n", dev->n_cmd);
    free(dev);
}

static const device_config_t pp_config[] = {
    // clang-format off
    {
        .name           = "banner",
        .description    = "Version",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Photo Play 1999",  .value = 0 },
            { .description = "Photo Play 2000",  .value = 1 },
            { .description = "Photo Play 2001",  .value = 2 },
            { .description = "I.G.O. 2002",      .value = 3 },
            { .description = "I.G.O. 2003",      .value = 4 },
            { .description = "I.G.O. 2004",      .value = 5 },
            { .description = "I.G.O. 2005B",     .value = 6 },
            { .description = "I.G.O. 2006",      .value = 7 },
            { .description = "I.G.O. 2007",      .value = 8 },
            { .description = "I.G.O. 2008",      .value = 9 },
            { .description = ""                             }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "territory",
        .description    = "Territory",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "AT - Austria",       .value = 0 },
            { .description = "DE - Germany",       .value = 1 },
            { .description = "ES - Spain",         .value = 2 },
            { .description = "IT - Italy",         .value = 3 },
            { .description = "NL - Netherlands",   .value = 4 },
            { .description = "PT - Portugal",      .value = 5 },
            { .description = "SE - Sweden",        .value = 6 },
            { .description = "ZA - South Africa",  .value = 7 },
            { .description = "GB - Great Britain", .value = 8 },
            { .description = "FR - France",        .value = 9 },
            { .description = ""                               }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "ngdata",
        .description    = "NG-DONGLE data-readback sweep (research)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "ngsweep",
        .description    = "NG-DONGLE probe sweep (research)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "ibutton",
        .description    = "Emulate the DS1982 iButton at I/O 268h",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t lpt_dongle_photoplay_device = {
    .name          = "Protection Dongle for Photo Play / I.G.O.",
    .internal_name = "dongle_photoplay",
    .flags         = DEVICE_LPT | DEVICE_HOTPLUG,
    .local         = 0,
    .init          = pp_init,
    .close         = pp_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pp_config
};
