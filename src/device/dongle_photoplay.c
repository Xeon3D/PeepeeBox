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
#include <86box/photoplay.h>

/* Protocol bring-up: log every transaction.  Remove once this is trusted. */
#define ENABLE_DONGLE_PHOTOPLAY_LOG 1

#define PP_OUT_MAX 512
#define PP_IN_MAX  256
#define PP_BLOCK   62 /* what KEYN.COM serves; type 3 returns the first 48 */

/* The 2000 generation's second token -- see the CDONGLE section below. */
#define CD_RECORD 48 /* what the guest asks for, and what the record layout gives */

enum {
    CD_IDLE = 0, /* listening for command bytes; ACK low                 */
    CD_READY,    /* a reply is queued; ACK high so the poll loop exits   */
    CD_ARMED,    /* host acknowledged with CF; ACK back low              */
    CD_HS,       /* the four-pair attention handshake; ACK follows bit 5 */
    CD_STREAM    /* clocking the reply out, one bit per CF               */
};

typedef struct {
    int     active; /* the reset pulse train has been seen: this guest is a 2000 */
    int     state;

    uint8_t hist[5]; /* the five-write window a byte is assembled from */
    int     nhist;
    uint8_t prev1, prev2; /* the last two DATA writes, for the frame marker */
    uint8_t last;

    uint8_t key;

    uint8_t cmd; /* the byte 0x11FA sends, descrambled */
    int     have_cmd;
    uint8_t arg[12]; /* the payload after it, descrambled */
    int     nargs;

    uint8_t pending;       /* a byte is decoded; its trailer says what it was */
    int     await_trailer;
    int     claimable;     /* a trailer has passed and no frame has started since */
    uint8_t nonce_raw;     /* the last plain byte before a command: the nonce */

    uint8_t tx[CD_RECORD];
    int     tx_len;
    int     tx_bit;
    int     attn;
    int     pic_ready;   /* the picture-key reply is built; do not rebuild it */
} cd_t;

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
    int     n_raw;             /* full raw-wire trace, see pp_raw() */

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

    cd_t    cd;

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

/* The releases, and the banner each one's MAIN.SET carries.  Where an image was
   available the banner was read out of it directly (Docs/08); the rest follow the
   year pattern those establish.  The guest string-matches this, so it has to be
   right -- and note 2005 is "Version 2005B" on both IGO 5 images to hand, which
   also satisfies a plain "Version 2005" test because that is a prefix of it.

   "IGO 1" and "Photo Play 2001" are the same release under two names: the
   IGO <n> -> "Version 200<n>" mapping is confirmed for 2, 3 and 5, so there is no
   separate banner for a first I.G.O.  An IGO Italy build runs on the IGO 8 IT
   dongle, so it is Version 2008 with the territory set to IT rather than an
   entry of its own. */
static const char *pp_banners[] = {
    "Version 99",    /* Photo Play 99                     - read from an image */
    "Version 2000",  /* Photo Play 2000                   - read from an image */
    "Version 2001",  /* Photo Play 2001 / IGO 1                                */
    "Version 2002",  /* IGO 2                             - read from an image */
    "Version 2003",  /* IGO 3                             - read from an image */
    "Version 2004",  /* IGO 4                                                  */
    "Version 2005B", /* IGO 5                             - read from an image */
    "Version 2006",  /* IGO 6                                                  */
    "Version 2007",  /* IGO 7                                                  */
    "Version 2008"   /* IGO 8, and IGO Italy with territory IT                 */
};
#define PP_NBANNERS ((int) (sizeof(pp_banners) / sizeof(pp_banners[0])))

/* Territories, sorted by code.  ES and SP are both here on purpose: the Spanish
   dongles dumped for this project carry "Version 99 (SP)" in their EEPROM, while
   the 2003 Spanish image's MAIN.SET says "Version 2003 (ES)".  funworld changed
   the code between generations, and the banner has to match the image exactly,
   so both have to be offered.  SE is included because an IGO 3 image reads
   "Version 2003 (SE)". */
static const char *pp_terrs[] = {
    "AT", "BE", "CY", "CZ", "DE", "ES", "FR",
    "GR", "IT", "NL", "PT", "SE", "SP"
};
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
        /* The host sends { 01, NAME[8], nonce } -- the uppercased, space-padded
           8-character basename of a file it is about to open, then a random byte.
           The dongle answers a 4-byte code derived from the name alone, XORed under
           a keystream seeded with the nonce.

           This is not the idle curiosity Docs/12 and Docs/14 took it for.  Those
           concluded no 1999 binary calls type 1; they are wrong.  It is fetched once
           per picture, immediately between reading a PCX header and validating it,
           and the code it returns is the seed for the Turbo Pascal LCG that decrypts
           that header -- the first 128 bytes of every PCX inside a GWAD archive.  Get
           it wrong and the game reports "not a PCX-File".

           Verified end to end against shipped data: every one of the 731 pictures in
           FMEMO/PICS/FOTOPLAY.WAD decrypts to a valid 320x220 8-bit PCX header using
           the code this returns for its filename, and the five keys recovered
           independently by seed-cracking reproduce exactly, little-endian.

           Archives packed without a dongle are keyed with the vendor default
           0x00012345 instead of by name -- FMEMO's own GRAFIX archive and all of
           FINDIT's pictures are like that -- so not every PCX in the game goes
           through this path. */
        uint8_t h[4];
        uint8_t k = dev->cmd[9];

        pp_type1(&dev->cmd[1], h);
        for (int i = 0; i < pp_recv_len[1]; i++) {
            pp_queue(dev, (uint8_t) (h[i] ^ k));
            k = pp_next_key1(k);
        }
        pp_log("PP: type 1, name \"%.8s\", nonce %02X -> code %02X%02X%02X%02X\n",
               (const char *) &dev->cmd[1], dev->cmd[9], h[3], h[2], h[1], h[0]);
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

/* ------------------------------------------------------------------------------------
 * The 2000 generation's token: "CDONGLE" to the menu, "PDONGLE" to the games, one
 * device either way (Docs/15).  A second, later dongle than the 1999 one above, sharing
 * the same parallel port but nothing else -- the 1999 build contains none of this code.
 *
 * The host writes to the DATA port and the only line coming back is STATUS bit 6, the
 * ACK.  A byte goes out as five writes, low nibble first, and comes back as eight bits
 * MSB-first, each sampled off ACK.  Both directions are XORed with one key that the host
 * derives from a nonce -- and transmits in clear before the key is in force, so this end
 * can derive the same key instead of guessing it.
 * ---------------------------------------------------------------------------------- */

/* The attention handshake the library insists on before it will read (0x1067).  Each
   write is answered on ACK with its own bit 5, which is what the real device evidently
   does; getting one wrong makes the library give up with its error 0x17. */
static const uint8_t cd_attention[] = { 0xDF, 0xEF, 0xBF, 0xCF, 0x9F, 0xEF, 0xBF, 0x8F };
#define CD_ATTN_LEN ((int) (sizeof(cd_attention) / sizeof(cd_attention[0])))

static void
cd_reset(cd_t *cd)
{
    cd->state      = CD_IDLE;
    cd->nhist      = 0;
    cd->key        = 0;
    cd->have_cmd      = 0;
    cd->nargs         = 0;
    cd->await_trailer = 0;
    cd->nonce_raw     = 0;
    cd->tx_len     = 0;
    cd->tx_bit     = 0;
    cd->attn       = 0;
    cd->pic_ready  = 0;
}

/* Reassemble a byte from the five-write nibble pattern: F<lo> C<lo> F<lo> 9<hi> 8<hi>.
   The window slides rather than resetting on a mismatch, because each byte is followed
   by a trailer write and transactions carry stray writes between frames. */
static int
cd_assemble(cd_t *cd, uint8_t val, uint8_t *out)
{
    if (cd->nhist >= 5) {
        memmove(cd->hist, cd->hist + 1, 4);
        cd->nhist = 4;
    }
    cd->hist[cd->nhist++] = val;
    if (cd->nhist < 5)
        return 0;

    const uint8_t *h  = cd->hist;
    const uint8_t  lo = h[0] & 0x0f;
    const uint8_t  hi = h[3] & 0x0f;

    if (((h[0] & 0xf0) == 0xf0) && ((h[1] & 0xf0) == 0xc0) && ((h[2] & 0xf0) == 0xf0) &&
        ((h[3] & 0xf0) == 0x90) && ((h[4] & 0xf0) == 0x80) &&
        ((h[1] & 0x0f) == lo) && ((h[2] & 0x0f) == lo) && ((h[4] & 0x0f) == hi)) {
        *out      = (uint8_t) ((hi << 4) | lo);
        cd->nhist = 0;
        return 1;
    }
    return 0;
}

/* The one query the 2000 generation's protection actually turns on.
 *
 * The API this reaches is `dongle(func, port, in, out)` -- send `in`, receive `out` --
 * and the guest's own wrapper (library offset 0x0A) fixes func = 1, reverses a 3-byte
 * input and widens the 2-byte reply into a long.  Its caller then does, verbatim:
 *
 *     cmp DWORD PTR [bp-6], 0x4693
 *     jne <fail>
 *
 * That constant, and the challenge that earns it, are the same in every one of the 29
 * game executables, in MENU.EXE, and across all four 2000 images to hand -- one fixed
 * pair, not a per-title or per-territory one.  Docs/15.
 *
 * So this is a recorded answer, not a derived one: the function inside the real device
 * that maps the challenge to the response is still unknown, and nothing here can compute
 * it for a challenge that has not been seen.  It is the honest shape of what has been
 * established.  Should another challenge ever turn up, it belongs in this table beside
 * this one, and the fallback below will make its absence obvious rather than silent. */
#define CD_CMD_KEY 0xA0 /* API function 1; the wire command byte is func + 0x9F */

static const struct {
    uint8_t cmd;
    uint8_t arg[3];
    uint8_t reply[2];
} cd_answers[] = {
    /* Established: the licence query, the same in all 29 games, MENU.EXE and all
       four 2000 images. */
    { 0xA0, { 0x86, 0x2E, 0xD0 }, { 0x93, 0x46 } }, /* -> the long 0x00004693 */

    /* PLACEHOLDERS, not established.  AMORE asks these three before it will load a
       picture; nothing checks their values, they are cached and used as the 16-bit
       constant in its picture-key derivation, so a wrong one yields a wrong key
       rather than a refusal.  They are here to get past the query and see what the
       game asks next -- the values are placeholders and the log says so. */
    { 0xA1, { 0xA6, 0x4B, 0xD0 }, { 0x8E, 0x0A } },
    { 0xA3, { 0x73, 0x07, 0x09 }, { 0x02, 0x00 } }, /* the case 2 tag; see cd_prepare_picture */
    { 0xA4, { 0x76, 0x02, 0x27 }, { 0x03, 0x00 } }, /* the case 3 tag; see cd_prepare_picture */
};
#define CD_NANSWERS ((int) (sizeof(cd_answers) / sizeof(cd_answers[0])))

/* ------------------------------------------------------------------------------------
 * The picture-key query: library functions 0x11 and 0x12, wire AA and AB.
 *
 * A photo game decrypts each PCX header with a Borland LCG seeded by a per-picture key
 * (Docs/ng-11), and asks the dongle for that key.  The request carries the uppercased
 * 8-character basename and a 16-bit constant:
 *
 *     08 00 | 8B 03 | "100     "
 *     count   const   name
 *
 * The count is 8, so the reply overwrites all eight name bytes, and the game folds those
 * into the four-byte seed -- XOR for two of its four cases, ADD for the other two.  The
 * case is chosen by a hash of the filename, and it is the case that picks the constant.
 *
 * WHAT IS AND IS NOT KNOWN HERE
 *
 * The device's real name-to-key function has not been recovered.  What has been recovered
 * is the answer it must give, for every picture in the shipped data: every key in AMORE's
 * COMIX archive was cracked straight out of the ciphertext, because a PCX header begins
 * with eight known bytes and that pins the LCG seed exactly.  The method was checked
 * against an archive packed with no dongle at all and recovers the documented vendor
 * default 0x00012345 on the nose.
 *
 * From those 332 keys, two of the four cases fall out as exact closed forms and are
 * implemented as such below -- each fits all 83 of its files.  The other two do not fit
 * any rotate/add/xor model tried, and are served from per-character tables instead.
 *
 * So cases 2 and 3 are FITTED, NOT EMULATED.  They are right for every character that
 * appears in the shipped archives (the digits and the space) and have nothing to say
 * about any other.  An unknown character is logged rather than guessed at, so the gap is
 * loud instead of silent.  Anyone deriving the real function should be able to reproduce
 * cd_c2_* and cd_c3_* from it exactly; that is the test it has to pass.
 *
 * The two constants for cases 2 and 3 come from the dongle itself, via the A3 and A4
 * queries, and their real values are unknown.  They do not need to be known: whatever
 * this device answers is what the game hands back in the request, so the constant is
 * simply a tag naming the case.  Two values are picked here that cannot collide with the
 * two the games hardcode.
 */

#define CD_CONST_CASE0 0x038B /* hardcoded in the game */
#define CD_CONST_CASE1 0x0A8E /* hardcoded in the game */
#define CD_CONST_CASE2 0x0002 /* our answer to A3 -- a tag, not a recovered value */
#define CD_CONST_CASE3 0x0003 /* our answer to A4 -- ditto */

typedef struct {
    uint8_t ch;
    uint8_t val;
} cd_map_t;

static const cd_map_t cd_c2_b0[] = { { 0x31, 0x4C }, { 0x32, 0x6C }, { 0x33, 0x8C }, { 0x34, 0xAC }, { 0x35, 0xCC }, { 0x36, 0xEC }, { 0x37, 0x0C }, { 0x38, 0x2B }, { 0x39, 0x4B }, { 0, 0 } };
static const cd_map_t cd_c2_b1[] = { { 0x20, 0xF1 }, { 0x30, 0x11 }, { 0x31, 0x13 }, { 0x32, 0x15 }, { 0x33, 0x17 }, { 0x34, 0x09 }, { 0x35, 0x0B }, { 0x36, 0x0D }, { 0x37, 0x0F }, { 0x38, 0x01 }, { 0x39, 0x03 }, { 0, 0 } };
static const cd_map_t cd_c2_b2[] = { { 0x20, 0xF6 }, { 0x30, 0xF8 }, { 0x31, 0xD8 }, { 0x32, 0xB8 }, { 0x33, 0x98 }, { 0x34, 0x78 }, { 0x35, 0x58 }, { 0x36, 0x38 }, { 0x37, 0x18 }, { 0x38, 0xF7 }, { 0x39, 0xD7 }, { 0, 0 } };
static const cd_map_t cd_c2_b3[] = { { 0x20, 0x0A }, { 0, 0 } };
static const cd_map_t cd_c3_b0[] = { { 0x31, 0x76 }, { 0x32, 0xF5 }, { 0x33, 0x75 }, { 0x34, 0xF4 }, { 0x35, 0x74 }, { 0x36, 0xF3 }, { 0x37, 0x73 }, { 0x38, 0xF2 }, { 0x39, 0x72 }, { 0, 0 } };
static const cd_map_t cd_c3_b1[] = { { 0x20, 0x12 }, { 0x30, 0x92 }, { 0x31, 0x8A }, { 0x32, 0x82 }, { 0x33, 0x7A }, { 0x34, 0xB2 }, { 0x35, 0xAA }, { 0x36, 0xA2 }, { 0x37, 0x9A }, { 0x38, 0xD2 }, { 0x39, 0xCA }, { 0, 0 } };
static const cd_map_t cd_c3_b2[] = { { 0x20, 0xFC }, { 0x30, 0x7C }, { 0x31, 0x74 }, { 0x32, 0x8C }, { 0x33, 0x84 }, { 0x34, 0x9C }, { 0x35, 0x94 }, { 0x36, 0xAC }, { 0x37, 0xA4 }, { 0x38, 0x3C }, { 0x39, 0x34 }, { 0, 0 } };
static const cd_map_t cd_c3_b3[] = { { 0x20, 0x98 }, { 0, 0 } };

static const cd_map_t *const cd_tables[2][4] = {
    { cd_c2_b0, cd_c2_b1, cd_c2_b2, cd_c2_b3 },
    { cd_c3_b0, cd_c3_b1, cd_c3_b2, cd_c3_b3 }
};

static uint8_t
cd_rol(uint8_t v, int r)
{
    return (uint8_t) ((v << r) | (v >> (8 - r)));
}

/* -1 when this character was never seen in the shipped archives. */
static int
cd_lookup(const cd_map_t *tab, uint8_t ch)
{
    for (int i = 0; tab[i].ch != 0; i++)
        if (tab[i].ch == ch)
            return tab[i].val;
    return -1;
}

/* The four-byte seed the game must end up with, little-endian in kb[]. */
static int
cd_picture_key(int which, const uint8_t *name, uint8_t *kb)
{
    switch (which) {
        case 0: /* exact; fits all 83 of its files */
            kb[0] = (uint8_t) (0x8F ^ cd_rol(name[0], 1));
            kb[1] = (uint8_t) (0xF8 ^ cd_rol(name[1], 5));
            kb[2] = (uint8_t) (0xF8 ^ cd_rol(name[2], 5));
            kb[3] = (uint8_t) (0x38 ^ name[3]);
            return 1;

        case 1: /* exact; the aliased fold really does drop name[0] and name[1] */
            kb[0] = (uint8_t) (0xD7 ^ name[3]);
            kb[1] = (uint8_t) (0xF7 ^ cd_rol(name[2], 3));
            kb[2] = (uint8_t) (0x2E ^ cd_rol(name[2], 3));
            kb[3] = (uint8_t) (0x8A ^ name[3]);
            return 1;

        default: { /* tabulated */
            const cd_map_t *const *t = cd_tables[which - 2];

            for (int i = 0; i < 4; i++) {
                const int v = cd_lookup(t[i], name[i]);

                if (v < 0) {
                    pp_log("PP: CDONGLE case %d has no tabulated value for '%c' at %d --"
                           " this name is outside what the shipped archives cover\n",
                           which, (char) name[i], i);
                    return 0;
                }
                kb[i] = (uint8_t) v;
            }
            return 1;
        }
    }
}

/* Build the eight bytes whose fold gives kb.  The fold takes eight inputs to four, so
   there is slack: zero the second half and let the first carry the answer.  Case 1 is
   the exception -- it reads back values it has just written (0x1F56E), so its inputs
   have to be placed to survive that. */
static void
cd_fold_inverse(int which, const uint8_t *kb, uint8_t *r)
{
    memset(r, 0, 8);

    if (which == 1) {
        r[3] = kb[0];
        r[2] = kb[1];
        r[4] = (uint8_t) (kb[1] ^ kb[2]);
        r[7] = (uint8_t) (kb[0] ^ kb[3]);
    } else {
        /* XOR fold for case 0, ADD fold for 2 and 3; either way x op 0 == x */
        r[0] = kb[0];
        r[1] = kb[1];
        r[2] = kb[2];
        r[3] = kb[3];
    }
}

/* Answer a picture-key request.  Returns 0 if this one cannot be served. */
static int
cd_prepare_picture(pp_t *dev)
{
    cd_t         *cd    = &dev->cd;
    const uint16_t konst = (uint16_t) (cd->arg[2] | (cd->arg[3] << 8));
    const uint8_t *name  = &cd->arg[4];
    uint8_t        kb[4];
    uint8_t        r[8];
    int            which;

    switch (konst) {
        case CD_CONST_CASE0: which = 0; break;
        case CD_CONST_CASE1: which = 1; break;
        case CD_CONST_CASE2: which = 2; break;
        case CD_CONST_CASE3: which = 3; break;
        default:
            pp_log("PP: CDONGLE picture key asked with unknown constant %04X\n", konst);
            return 0;
    }

    if (!cd_picture_key(which, name, kb))
        return 0;

    cd_fold_inverse(which, kb, r);

    /* 0x081D does not hand the caller what it received.  It ends with a backwards
       nibble-merge (0x08A9): walking down from the last byte,

           seen[i] = (recv[i + 1] & 0xF0) | (recv[i] & 0x0F)

       where recv[8] is one extra byte fetched after the loop by 0x0FCA.  So the bytes
       the game folds are not the bytes on the wire, and sending the wanted values
       directly delivers a nibble-shifted mess.  Invert it: each byte carries the low
       nibble of its own target and the high nibble of the one before. */
    uint8_t wire[9];

    wire[0] = (uint8_t) (r[0] & 0x0F);
    for (int i = 1; i < 8; i++)
        wire[i] = (uint8_t) ((r[i - 1] & 0xF0) | (r[i] & 0x0F));
    wire[8] = (uint8_t) (r[7] & 0xF0);
    cd->pic_ready = 1;

    cd->tx_len = 9;
    cd->tx_bit = 0;
    for (int i = 0; i < 9; i++)
        cd->tx[i] = (uint8_t) (wire[i] ^ cd->key);

    pp_log("PP: CDONGLE picture key for \"%.8s\" case %d -> %02X%02X%02X%02X\n",
           (const char *) name, which, kb[3], kb[2], kb[1], kb[0]);
    return 1;
}

/* Queue whatever this command is owed, scrambled with the key the host just told us. */
static void
cd_prepare(pp_t *dev)
{
    cd_t *cd = &dev->cd;

    /* 0x081D interleaves a send with every read: it pushes one byte of the buffer,
       reads one back, and repeats.  Those sends arrive here as further arguments, and
       rebuilding the reply for each of them rewinds the stream to its first byte -- the
       guest then reads byte 0 over and over.  Build it once per command. */
    if (cd->pic_ready)
        return;

    cd->tx_bit = 0;

    /* The picture-key query carries a 12-byte payload: count, constant, then the
       8-character name.  Wait for all of it before answering. */
    if (((cd->cmd == 0xAA) || (cd->cmd == 0xAB)) && (cd->nargs >= 12)) {
        if (cd_prepare_picture(dev))
            return;
    }

    for (int a = 0; a < CD_NANSWERS; a++) {
        if ((cd->cmd != cd_answers[a].cmd) || (cd->nargs < 3))
            continue;
        if (memcmp(cd->arg, cd_answers[a].arg, 3) != 0)
            continue;

        cd->tx_len = 2;
        cd->tx[0]  = (uint8_t) (cd_answers[a].reply[0] ^ cd->key);
        cd->tx[1]  = (uint8_t) (cd_answers[a].reply[1] ^ cd->key);
        pp_log("PP: CDONGLE answering the licence query with %02X%02X\n",
               cd_answers[a].reply[1], cd_answers[a].reply[0]);
        return;
    }

    /* Library functions 1 to 8 -- wire A0 to A7 -- always read exactly two bytes
       back: the dispatcher hands 0x07E8 a fixed bx = 0x0302, three bytes out and two
       in.  Queueing the 48-byte record for one of those leaves the device parked in
       the middle of a record the guest stopped reading after two bytes, and the next
       transaction is served the leftovers.  Serve the right length even when the
       value is unknown. */
    if ((cd->cmd >= 0xA0) && (cd->cmd <= 0xA7)) {
        cd->tx_len = 2;
        cd->tx[0]  = cd->key;
        cd->tx[1]  = cd->key; /* plaintext 00 00 */
        pp_log("PP: CDONGLE command %02X (%02X %02X %02X) has no recorded answer\n",
               cd->cmd, cd->arg[0], cd->arg[1], cd->arg[2]);
        return;
    }

    /* Everything else gets the record.  The only other query seen is the parallel-port
       autodetect (`0x0BAD`), which reads one byte and looks solely at whether the
       transport worked -- it never examines the value. */
    pp_log("PP: CDONGLE no recorded answer for command %02X with %d args"
           " %02X %02X %02X %02X %02X %02X %02X %02X -- serving the record\n",
           cd->cmd, cd->nargs, cd->arg[0], cd->arg[1], cd->arg[2], cd->arg[3],
           cd->arg[4], cd->arg[5], cd->arg[6], cd->arg[7]);
    cd->tx_len = CD_RECORD;
    for (int i = 0; i < CD_RECORD; i++)
        cd->tx[i] = (uint8_t) (dev->block[i] ^ cd->key);
}

static void
cd_write_data(pp_t *dev, uint8_t val)
{
    cd_t   *cd = &dev->cd;
    uint8_t b;

    int rearmed = 0;

    /* Position, not value, is what separates the host's "claim the reply" byte from a
       frame write -- the frame's own C? and 8? writes look identical in every bit that
       matters.  A claim can only sit after a byte's D? trailer with no frame started
       since, so track exactly that: a trailer opens the window, and the F? that leads
       every byte frame closes it again. */
    if ((val & 0xf0) == 0xf0)
        cd->claimable = 0;

    /* The library opens every transaction with a BF/7F/BF pulse train (0x0792).  It is
       the one unambiguous frame marker on this wire, so use it to drop any half-decoded
       state and to start expecting the nonce again.  A 1999 guest drives only E0..FF on
       these lines, so it can never produce this pattern. */
    if ((cd->prev2 == 0xBF) && (cd->prev1 == 0x7F) && (val == 0xBF)) {
        cd->active = 1;
        cd_reset(cd);
        pp_log("PP: CDONGLE transaction start\n");
        cd->prev2 = cd->prev1;
        cd->prev1 = val;
        cd->last  = val;
        return;
    }
    cd->prev2 = cd->prev1;
    cd->prev1 = val;
    cd->last  = val;

    switch (cd->state) {
        case CD_IDLE:
        case CD_READY:
            /* A decoded byte is only classified once its trailer arrives, because the
               trailer is what says which kind of byte it was.  0x11FA trails the command
               byte with exactly D0; 0x1187 trails every ordinary byte with DF.  Keying on
               that rather than on the reset pulse train is what matters: the record read
               follows the previous transaction with no pulse train at all, and a parser
               waiting for one sits through the whole thing in silence. */
            if (cd->await_trailer) {
                cd->await_trailer = 0;
                cd->claimable     = 1; /* this is the trailer; a claim may follow it */

                if (val == 0xD0) {
                    /* The command.  The ordinary byte before it was the nonce, sent
                       while the key was still zero, so it names the key outright. */
                    cd->key      = (uint8_t) (cd->nonce_raw ^ 0xD3);
                    cd->cmd      = (uint8_t) (cd->pending ^ cd->key);
                    cd->have_cmd = 1;
                    cd->pic_ready = 0;
                    cd->nargs    = 0;
                    pp_log("PP: CDONGLE command %02X (nonce %02X -> key %02X)\n",
                           cd->cmd, cd->nonce_raw, cd->key);
                    cd_prepare(dev);
                    cd->state = CD_READY;
                } else if (!cd->have_cmd) {
                    cd->nonce_raw = cd->pending; /* still in clear: a nonce candidate */
                } else {
                    if (cd->nargs < (int) sizeof(cd->arg))
                        cd->arg[cd->nargs++] = (uint8_t) (cd->pending ^ cd->key);
                    pp_log("PP: CDONGLE arg %d = %02X\n", cd->nargs,
                           (uint8_t) (cd->pending ^ cd->key));
                    cd_prepare(dev);
                    cd->state = CD_READY;
                }
            }

            if (cd_assemble(cd, val, &b)) {
                cd->pending       = b;
                cd->await_trailer = 1;
            }
            break;

        case CD_ARMED:
            if (val == cd_attention[0]) {
                cd->state = CD_HS;
                cd->attn  = 1;
            }
            break;

        case CD_HS:
            if (cd->attn >= CD_ATTN_LEN) {
                /* The handshake's last write is still owed an answer of its own -- ACK
                   clear, from bit 5 of 8F -- so hold here until the host clocks out the
                   first bit.  Streaming from the write itself puts a data bit under that
                   read and the library gives up with its error 0x17. */
                if (val == 0xCF) {
                    cd->state = CD_STREAM;
                    pp_log("PP: CDONGLE handshake done, streaming %d bytes\n", cd->tx_len);
                }
            } else if (val == cd_attention[cd->attn])
                cd->attn++;
            else if (val == cd_attention[0])
                cd->attn = 1; /* it restarted the sequence */
            break;

        case CD_STREAM:
            /* CF presents the next bit on ACK, FF clocks past it.  Anything else ends
               this read: 0x0F6F signs off with BF.  The record read calls it 48 times
               over for one byte each, without re-sending the command, so go ready again
               with the cursor where it stands rather than treating that as the end. */
            if ((val != 0xFF) && (val != 0xCF)) {
                if (cd->tx_bit < (cd->tx_len * 8)) {
                    cd->attn      = 0;
                    cd->state     = CD_READY;
                    cd->claimable = 1; /* no trailer precedes the next read's claim */
                    rearmed       = 1; /* and this write ends a read, it does not claim */
                }
            } else if (val == 0xFF) {
                cd->tx_bit++;
                if (cd->tx_bit >= (cd->tx_len * 8)) {
                    /* Reply delivered: the device goes idle, which means ACK follows
                       bit 5 of whatever the host last wrote.  Holding it high here
                       instead strands the host in the wind-down loop that writes 8F
                       and waits for ACK to fall. */
                    pp_log("PP: CDONGLE reply delivered, %d bytes\n", cd->tx_len);
                    /* The transaction is over.  Forget the command, so the bytes of the
                       next one -- which may arrive with no reset pulse train in between,
                       as the record read does -- are read as a fresh nonce and command
                       rather than as more payload for this one. */
                    cd->have_cmd = 0;
                    cd->pic_ready = 0;
                    cd->nargs    = 0;
                    cd->attn     = 0;
                    cd->state    = CD_IDLE;
                }
            }
            break;

        default:
            break;
    }

    /* The host claims the waiting reply and then waits for ACK to fall.  There are two
       read routines and they use different bytes to do it -- 0x0F6F writes CF, 0xFCA
       writes 8F -- so keying on one value strands the other in its retry loop forever.
       Keying on a bit does not work either: a frame's own C? write is indistinguishable
       from CF.  What identifies it is where it sits -- straight after a trailer, with no
       frame under way. */
    /* 0x0F6F claims with CF, 0xFCA with 8F.  Both are C? or 8?, which is what tells them
       apart from the D? trailer sitting immediately before. */
    if ((cd->state == CD_READY) && cd->claimable && !rearmed
        && (((val & 0xf0) == 0xc0) || ((val & 0xf0) == 0x80))) {
        cd->state     = CD_ARMED;
        cd->claimable = 0;
    }
}

/* What we drive on ACK right now. */
static int
cd_ack(const pp_t *dev)
{
    const cd_t *cd = &dev->cd;

    if (cd->state == CD_STREAM) {
        if (cd->tx_bit < (cd->tx_len * 8)) {
            const int byte = cd->tx_bit >> 3;
            const int bit  = 7 - (cd->tx_bit & 7);

            return (cd->tx[byte] >> bit) & 1;
        }
        return 0;
    }

    /* Held high while a reply is waiting and the host has not yet claimed it -- that is
       what lets the poll loop out.  Otherwise ACK echoes bit 5 of the byte the host last
       wrote, which is what makes the attention handshake's four pairs come out right and
       what lets the line go once a transaction is over. */
    if (cd->state == CD_READY)
        return 1;

    return (cd->last & 0x20) ? 1 : 0;
}

/* Full raw-wire trace.  The 1999 bit-bang is byte-framed, so the transaction logging
   above is enough to follow it; the 2000 generation's CDONGLE is a nibble-clocked
   protocol where the meaning is in the individual port writes and in the values the
   guest reads back, which the transaction layer never sees.  Set PEEPEEBOX_LPT_TRACE
   to record every access.  Off unless asked for: it is tens of thousands of lines. */
static int pp_raw_want = -1;

static void
pp_raw(pp_t *dev, const char *what, uint8_t val)
{
    if (pp_raw_want < 0)
        pp_raw_want = (getenv("PEEPEEBOX_LPT_TRACE") != NULL);
    if (pp_raw_want && dev->n_raw++ < 400000)
        pp_log("PPRAW %6d %-10s %02X\n", dev->n_raw, what, val);
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
    pp_raw(dev, "write_data", val);
    cd_write_data(dev, val);
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
    pp_raw(dev, "write_ctrl", val);

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

    /* Once the 2000 generation's library has announced itself, it owns this line: it
       reads nothing but bit 6, and the 1999 half would otherwise put nibble data on the
       very same bit. */
    if (dev->cd.active) {
        st = cd_ack(dev) ? 0x40 : 0x00;
        pp_raw(dev, "read_status", st);
        return st;
    }

    /* The ack line reaches us the same way the data nibbles do, so sample it from the
       port here rather than relying on write_data edges that 86Box filters out. */
    if (dev->lpt != NULL)
        pp_ack_edge(dev, ((lpt_t *) dev->lpt)->dat);


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

    pp_raw(dev, "read_status", st);
    return st;
}

static uint8_t
pp_read_ctrl(void *priv)
{
    pp_t *dev = (pp_t *) priv;

    pp_raw(dev, "read_ctrl", dev->last_ctrl);
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

    pp_raw(dev, "read_data", out);
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
    char       banner[31];   /* the record's field: 30 characters plus the NUL */
    char       full[96];     /* composed here first, so overlong is detectable */
    char       img_banner[64];
    char       img_terr[16];
    char       img_rel[64];

    /* The image knows what it is, and a cabinet's dongle always matched the disk
       it shipped with, so both fields default to whatever MAIN.SET says rather
       than to a fixed guess.  A 2000-generation image asked for by a dongle
       reporting "Version 99 (AT)" fails its own check, which is exactly the
       trap that made the games report PDONGLE FAILED.  Either field can still
       be pinned by hand for testing an image against the wrong dongle. */
    const int have_img = photoplay_image_ident(img_banner, sizeof(img_banner),
                                               img_terr, sizeof(img_terr));

    /* MAIN.SET carries the composed form, "Version 2000 (DE)".  Split the
       territory back off so a hand-picked one can be substituted. */
    snprintf(img_rel, sizeof(img_rel), "%s", img_banner);
    {
        char *paren = strchr(img_rel, '(');

        while ((paren != NULL) && (paren > img_rel) && (paren[-1] == ' '))
            paren--;
        if (paren != NULL)
            *paren = 0;
    }

    if (have_img && (bi < 0) && (ti < 0) && img_banner[0]) {
        /* Both fields on auto: hand back MAIN.SET's string exactly as it reads.
           Rebuilding it from our own release and territory lists would only ever be
           as good as those lists, and the guest does an exact compare -- so a
           release or a territory this build has never heard of still gets a correct
           dongle this way. */
        snprintf(full, sizeof(full), "%s", img_banner);
    } else {
        /* At least one field was pinned by hand, so compose: keep whichever half is
           still on auto and substitute the other. */
        const char *rel = (have_img && (bi < 0) && img_rel[0])
                        ? img_rel
                        : pp_banners[(bi >= 0 && bi < PP_NBANNERS) ? bi : 0];
        const char *ter = (have_img && (ti < 0) && img_terr[0])
                        ? img_terr
                        : pp_terrs[(ti >= 0 && ti < PP_NTERRS) ? ti : 0];

        snprintf(full, sizeof(full), "%s (%s)", rel, ter);
    }

    /* No release uses a banner longer than 30 characters, which is exactly what the
       record has room for -- a banner and eight dwords inside 62 bytes.  The copy is
       bounded regardless, so the compiler can see it is safe without being told. */
    strncpy(banner, full, sizeof(banner) - 1);
    banner[sizeof(banner) - 1] = 0;

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
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Auto (from the disk image)", .value = -1 },
            { .description = "Photo Play 1999",        .value = 0 },
            { .description = "Photo Play 2000",        .value = 1 },
            { .description = "Photo Play 2001 / IGO 1", .value = 2 },
            { .description = "IGO 2",                  .value = 3 },
            { .description = "IGO 3",                  .value = 4 },
            { .description = "IGO 4",                  .value = 5 },
            { .description = "IGO 5",                  .value = 6 },
            { .description = "IGO 6",                  .value = 7 },
            { .description = "IGO 7",                  .value = 8 },
            { .description = "IGO 8 / IGO Italy",      .value = 9 },
            { .description = ""                                   }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "territory",
        .description    = "Territory",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Auto (from the disk image)", .value = -1 },
            { .description = "AT - Austria",              .value =  0 },
            { .description = "BE - Belgium",              .value =  1 },
            { .description = "CY - Cyprus",               .value =  2 },
            { .description = "CZ - Czechia",              .value =  3 },
            { .description = "DE - Germany",              .value =  4 },
            { .description = "ES - Spain (2003 onwards)", .value =  5 },
            { .description = "FR - France",               .value =  6 },
            { .description = "GR - Greece",               .value =  7 },
            { .description = "IT - Italy",                .value =  8 },
            { .description = "NL - Netherlands",          .value =  9 },
            { .description = "PT - Portugal",             .value = 10 },
            { .description = "SE - Sweden",               .value = 11 },
            { .description = "SP - Spain (1999)",         .value = 12 },
            { .description = ""                                       }
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
