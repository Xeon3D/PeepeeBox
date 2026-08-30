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

/* The dwords sit at a FIXED offset of 16, whatever the banner is.
 *
 * The games do not walk a struct -- each one reads one absolute offset into the block
 * it was handed, hardcoded at compile time: FINDIT +0x1C, MOSAIC +0x20, FMEMO +0x24
 * (Docs/14).  Those are v[3], v[4] and v[5] of a banner[16] + uint32 v[8] record, which
 * is what the hardware serves (Docs/12).  KEYN.COM's 30-byte banner is the odd one out,
 * and copying it is what put every dword 14 bytes late.
 *
 * A 1999 banner ("Version 99 (XX)", 15 characters) fits the field exactly.  A 2000 one
 * ("Version 2000 (DE)", 17) does not, and must still not move the dwords: the banner is
 * simply written over the start of the block, clipping the first two bytes of v[0].
 * Nothing reads v[0] -- it is the per-unit word, uninitialised host memory on a real
 * dongle -- so the collision costs nothing, where moving the dwords costs FINDIT its
 * level database and leaves the game running with two black picture panels. */
#define PP_BANNER_1999 16

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
 * The answer the device must give was recovered from the shipped data rather than from
 * the dongle: a PCX header begins with eight known bytes, which pins the LCG seed
 * exactly, so every key can be cracked straight out of the ciphertext.  The method was
 * checked against an archive packed with no dongle at all and recovers the documented
 * vendor default 0x00012345 on the nose.  3096 keys over three archives came out that
 * way, and they are what everything below is fitted to and verified against.
 *
 * THE STRUCTURE, which is what makes this generalise past the names in those archives:
 *
 *   1. The reply is EIGHT bytes and the guest folds them itself.  Byte j is a function
 *      of name[j] alone -- call it S_j -- and the fold pairs j with j+4.  That is why
 *      per-position models fitted on AMORE collapsed on FINDIT: AMORE's names are so
 *      short that name[4..7] were always spaces, so S_{j+4} looked like a constant.
 *      It also makes the reply streamable, since 0x081D wants byte j back before it has
 *      sent name[j+1].
 *
 *   2. Each S_j is SEPARABLE IN THE NIBBLES: S(c) = a[hi] + b[lo] mod 256, exactly, in
 *      every position of every case, with no contradiction over all 3096 keys.
 *
 *   3. b is LINEAR IN THE BITS of the low nibble, so four weights give all sixteen
 *      entries.  This is the fingerprint the earlier notes kept seeing as
 *      "nibble-granular with carries", and it is why an affine model in the name BYTES
 *      was rank-deficient and died: the function is affine in the name's BITS.
 *
 * The table below is therefore measured where the archives show a character and filled
 * in from that law where they do not.  Entries the law cannot reach are -1 and are
 * logged rather than guessed, so the gap stays loud.  It reproduces all 3096 cracked
 * keys exactly -- 667, 810, 911 and 708 over the four cases.
 *
 * It is still a characterisation, not the dongle's own arithmetic: the closed form
 * behind a[] is not known, and neither is the constant-to-transform rule that would
 * yield the real A3 and A4.  Anyone who recovers those must reproduce this table.
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

/* Picture-key transform, one byte per character, per case, per position.
 *
 *   476 entries measured directly from cracked keys,
 *   1386 more filled in by the nibble law, the rest (-1) unknown.
 */
#define CD_PIC_LO 0x20
static const int16_t cd_pic_s[4][8][64] = {
    { /* case 0 */
        { /* S_0 */
             207,  205,  203,  201,  199,  197,  195,  193,  223,  221,  219,  217,  215,  213,  211,  209,
             239,  237,  235,  233,  231,  229,  227,  225,  255,  253,  251,  249,  247,  245,  243,  241,
              15,   13,   11,    9,    7,    5,    3,    1,   31,   29,   27,   25,   23,   21,   19,   17,
              47,   45,   43,   41,   39,   37,   35,   33,   63,   61,   59,   57,   55,   53,   51,   49,
        },
        { /* S_1 */
             252,  220,  188,  156,  124,   92,   60,   28,  253,  221,  189,  157,  125,   93,   61,   29,
             254,  222,  190,  158,  126,   94,   62,   30,  255,  223,  191,  159,  127,   95,   63,   31,
             240,  208,  176,  144,  112,   80,   48,   16,  241,  209,  177,  145,  113,   81,   49,   17,
             242,  210,  178,  146,  114,   82,   50,   18,  243,  211,  179,  147,  115,   83,   51,   19,
        },
        { /* S_2 */
             252,  220,  188,  156,  124,   92,   60,   28,  253,  221,  189,  157,  125,   93,   61,   29,
             254,  222,  190,  158,  126,   94,   62,   30,  255,  223,  191,  159,  127,   95,   63,   31,
             240,  208,  176,  144,  112,   80,   48,   16,  241,  209,  177,  145,  113,   81,   49,   17,
             242,  210,  178,  146,  114,   82,   50,   18,  243,  211,  179,  147,  115,   83,   51,   19,
        },
        { /* S_3 */
              24,   56,   88,  120,  152,  184,  216,  248,   25,   57,   89,  121,  153,  185,  217,  249,
              26,   58,   90,  122,  154,  186,  218,  250,   27,   59,   91,  123,  155,  187,  219,  251,
              20,   52,   84,  116,  148,  180,  212,  244,   21,   53,   85,  117,  149,  181,  213,  245,
              22,   54,   86,  118,  150,  182,  214,  246,   23,   55,   87,  119,  151,  183,  215,  247,
        },
        { /* S_4 */
               0,    2,    4,    6,    8,   10,   12,   14,   16,   18,   20,   22,   24,   26,   28,   30,
              32,   34,   36,   38,   40,   42,   44,   46,   48,   50,   52,   54,   56,   58,   60,   62,
             192,  194,  196,  198,  200,  202,  204,  206,  208,  210,  212,  214,  216,  218,  220,  222,
             224,  226,  228,  230,  232,  234,  236,  238,  240,  242,  244,  246,  248,  250,  252,  254,
        },
        { /* S_5 */
               0,   32,   64,   96,  128,  160,  192,  224,    1,   33,   65,   97,  129,  161,  193,  225,
               2,   34,   66,   98,  130,  162,  194,  226,    3,   35,   67,   99,  131,  163,  195,  227,
              12,   44,   76,  108,  140,  172,  204,  236,   13,   45,   77,  109,  141,  173,  205,  237,
              14,   46,   78,  110,  142,  174,  206,  238,   15,   47,   79,  111,  143,  175,  207,  239,
        },
        { /* S_6 */
               0,   32,   64,   96,  128,  160,  192,  224,    1,   33,   65,   97,  129,  161,  193,  225,
               2,   34,   66,   98,  130,  162,  194,  226,    3,   35,   67,   99,  131,  163,  195,  227,
              12,   44,   76,  108,  140,  172,  204,  236,   13,   45,   77,  109,  141,  173,  205,  237,
              14,   46,   78,  110,  142,  174,  206,  238,   15,   47,   79,  111,  143,  175,  207,  239,
        },
        { /* S_7 */
               0,   32,   64,   96,  128,  160,  192,  224,    1,   33,   65,   97,  129,  161,  193,  225,
               2,   34,   66,   98,  130,  162,  194,  226,    3,   35,   67,   99,  131,  163,  195,  227,
              12,   44,   76,  108,  140,  172,  204,  236,   13,   45,   77,  109,  141,  173,  205,  237,
              14,   46,   78,  110,  142,  174,  206,  238,   15,   47,   79,  111,  143,  175,  207,  239,
        },
    },
    { /* case 1 */
        { /* S_0 */
              -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
              -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
              -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
              -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        },
        { /* S_1 */
              -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
              -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
              -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
              -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        },
        { /* S_2 */
             246,  254,  230,  238,  214,  222,  198,  206,  182,  190,  166,  174,  150,  158,  134,  142,
             118,  126,  102,  110,   86,   94,   70,   78,   54,   62,   38,   46,   22,   30,    6,   14,
             245,  253,  229,  237,  213,  221,  197,  205,  181,  189,  165,  173,  149,  157,  133,  141,
             117,  125,  101,  109,   85,   93,   69,   77,   53,   61,   37,   45,   21,   29,    5,   13,
        },
        { /* S_3 */
             247,  255,  231,  239,  215,  223,  199,  207,  183,  191,  167,  175,  151,  159,  135,  143,
             119,  127,  103,  111,   87,   95,   71,   79,   55,   63,   39,   47,   23,   31,    7,   15,
             244,  252,  228,  236,  212,  220,  196,  204,  180,  188,  164,  172,  148,  156,  132,  140,
             116,  124,  100,  108,   84,   92,   68,   76,   52,   60,   36,   44,   20,   28,    4,   12,
        },
        { /* S_4 */
             217,  209,  201,  193,  249,  241,  233,  225,  153,  145,  137,  129,  185,  177,  169,  161,
              89,   81,   73,   65,  121,  113,  105,   97,   25,   17,    9,    1,   57,   49,   41,   33,
             218,  210,  202,  194,  250,  242,  234,  226,  154,  146,  138,  130,  186,  178,  170,  162,
              90,   82,   74,   66,  122,  114,  106,   98,   26,   18,   10,    2,   58,   50,   42,   34,
        },
        { /* S_5 */
               0,  128,    1,  129,    2,  130,    3,  131,    4,  132,    5,  133,    6,  134,    7,  135,
               8,  136,    9,  137,   10,  138,   11,  139,   12,  140,   13,  141,   14,  142,   15,  143,
              48,  176,   49,  177,   50,  178,   51,  179,   52,  180,   53,  181,   54,  182,   55,  183,
              56,  184,   57,  185,   58,  186,   59,  187,   60,  188,   61,  189,   62,  190,   63,  191,
        },
        { /* S_6 */
               0,    8,   16,   24,   32,   40,   48,   56,   64,   72,   80,   88,   96,  104,  112,  120,
             128,  136,  144,  152,  160,  168,  176,  184,  192,  200,  208,  216,  224,  232,  240,  248,
               3,   11,   19,   27,   35,   43,   51,   59,   67,   75,   83,   91,   99,  107,  115,  123,
             131,  139,  147,  155,  163,  171,  179,  187,  195,  203,  211,  219,  227,  235,  243,  251,
        },
        { /* S_7 */
              93,   85,   77,   69,  125,  117,  109,  101,   29,   21,   13,    5,   61,   53,   45,   37,
             221,  213,  205,  197,  253,  245,  237,  229,  157,  149,  141,  133,  189,  181,  173,  165,
              94,   86,   78,   70,  126,  118,  110,  102,   30,   22,   14,    6,   62,   54,   46,   38,
             222,  214,  206,  198,  254,  246,  238,  230,  158,  150,  142,  134,  190,  182,  174,  166,
        },
    },
    { /* case 2 */
        { /* S_0 */
              46,   78,  110,  142,  174,  206,  238,   14,   45,   77,  109,  141,  173,  205,  237,   13,
              44,   76,  108,  140,  172,  204,  236,   12,   43,   75,  107,  139,  171,  203,  235,   11,
              34,   66,   98,  130,  162,  194,  226,    2,   33,   65,   97,  129,  161,  193,  225,    1,
              32,   64,   96,  128,  160,  192,  224,    0,   31,   63,   95,  127,  159,  191,  223,  255,
        },
        { /* S_1 */
             241,  243,  245,  247,  233,  235,  237,  239,  225,  227,  229,  231,  217,  219,  221,  223,
              17,   19,   21,   23,    9,   11,   13,   15,    1,    3,    5,    7,  249,  251,  253,  255,
             177,  179,  181,  183,  169,  171,  173,  175,  161,  163,  165,  167,  153,  155,  157,  159,
             209,  211,  213,  215,  201,  203,  205,  207,  193,  195,  197,  199,  185,  187,  189,  191,
        },
        { /* S_2 */
             246,  214,  182,  150,  118,   86,   54,   22,  245,  213,  181,  149,  117,   85,   53,   21,
             248,  216,  184,  152,  120,   88,   56,   24,  247,  215,  183,  151,  119,   87,   55,   23,
             242,  210,  178,  146,  114,   82,   50,   18,  241,  209,  177,  145,  113,   81,   49,   17,
             244,  212,  180,  148,  116,   84,   52,   20,  243,  211,  179,  147,  115,   83,   51,   19,
        },
        { /* S_3 */
              10,   42,   74,  106,  138,  170,  202,  234,    9,   41,   73,  105,  137,  169,  201,  233,
               8,   40,   72,  104,  136,  168,  200,  232,    7,   39,   71,  103,  135,  167,  199,  231,
              22,   54,   86,  118,  150,  182,  214,  246,   21,   53,   85,  117,  149,  181,  213,  245,
              20,   52,   84,  116,  148,  180,  212,  244,   19,   51,   83,  115,  147,  179,  211,  243,
        },
        { /* S_4 */
               0,   32,   64,   96,  128,  160,  192,  224,  255,   31,   63,   95,  127,  159,  191,  223,
             254,   30,   62,   94,  126,  158,  190,  222,  253,   29,   61,   93,  125,  157,  189,  221,
             244,   20,   52,   84,  116,  148,  180,  212,  243,   19,   51,   83,  115,  147,  179,  211,
             242,   18,   50,   82,  114,  146,  178,  210,  241,   17,   49,   81,  113,  145,  177,  209,
        },
        { /* S_5 */
               0,    2,    4,    6,  248,  250,  252,  254,  240,  242,  244,  246,  232,  234,  236,  238,
              32,   34,   36,   38,   24,   26,   28,   30,   16,   18,   20,   22,    8,   10,   12,   14,
              64,   66,   68,   70,   56,   58,   60,   62,   48,   50,   52,   54,   40,   42,   44,   46,
              96,   98,  100,  102,   88,   90,   92,   94,   80,   82,   84,   86,   72,   74,   76,   78,
        },
        { /* S_6 */
               0,  224,  192,  160,  128,   96,   64,   32,  255,  223,  191,  159,  127,   95,   63,   31,
               2,  226,  194,  162,  130,   98,   66,   34,    1,  225,  193,  161,  129,   97,   65,   33,
             244,  212,  180,  148,  116,   84,   52,   20,  243,  211,  179,  147,  115,   83,   51,   19,
             246,  214,  182,  150,  118,   86,   54,   22,  245,  213,  181,  149,  117,   85,   53,   21,
        },
        { /* S_7 */
               0,   32,   64,   96,  128,  160,  192,  224,  255,   31,   63,   95,  127,  159,  191,  223,
             254,   30,   62,   94,  126,  158,  190,  222,  253,   29,   61,   93,  125,  157,  189,  221,
               4,   36,   68,  100,  132,  164,  196,  228,    3,   35,   67,   99,  131,  163,  195,  227,
               2,   34,   66,   98,  130,  162,  194,  226,    1,   33,   65,   97,  129,  161,  193,  225,
        },
    },
    { /* case 3 */
        { /* S_0 */
             254,  126,  253,  125,  252,  124,  251,  123,  250,  122,  249,  121,  248,  120,  247,  119,
             246,  118,  245,  117,  244,  116,  243,  115,  242,  114,  241,  113,  240,  112,  239,  111,
             238,  110,  237,  109,  236,  108,  235,  107,  234,  106,  233,  105,  232,  104,  231,  103,
             230,  102,  229,  101,  228,  100,  227,   99,  226,   98,  225,   97,  224,   96,  223,   95,
        },
        { /* S_1 */
              18,   10,    2,  250,   50,   42,   34,   26,   82,   74,   66,   58,  114,  106,   98,   90,
             146,  138,  130,  122,  178,  170,  162,  154,  210,  202,  194,  186,  242,  234,  226,  218,
              19,   11,    3,  251,   51,   43,   35,   27,   83,   75,   67,   59,  115,  107,   99,   91,
             147,  139,  131,  123,  179,  171,  163,  155,  211,  203,  195,  187,  243,  235,  227,  219,
        },
        { /* S_2 */
             252,  244,   12,    4,   28,   20,   44,   36,  188,  180,  204,  196,  220,  212,  236,  228,
             124,  116,  140,  132,  156,  148,  172,  164,   60,   52,   76,   68,   92,   84,  108,  100,
             251,  243,   11,    3,   27,   19,   43,   35,  187,  179,  203,  195,  219,  211,  235,  227,
             123,  115,  139,  131,  155,  147,  171,  163,   59,   51,   75,   67,   91,   83,  107,   99,
        },
        { /* S_3 */
             152,  144,  168,  160,  120,  112,  136,  128,  216,  208,  232,  224,  184,  176,  200,  192,
              24,   16,   40,   32,  248,  240,    8,    0,   88,   80,  104,   96,   56,   48,   72,   64,
             155,  147,  171,  163,  123,  115,  139,  131,  219,  211,  235,  227,  187,  179,  203,  195,
              27,   19,   43,   35,  251,  243,   11,    3,   91,   83,  107,   99,   59,   51,   75,   67,
        },
        { /* S_4 */
               0,  128,  255,  127,  254,  126,  253,  125,  252,  124,  251,  123,  250,  122,  249,  121,
             248,  120,  247,  119,  246,  118,  245,  117,  244,  116,  243,  115,  242,  114,  241,  113,
              48,  176,   47,  175,   46,  174,   45,  173,   44,  172,   43,  171,   42,  170,   41,  169,
              40,  168,   39,  167,   38,  166,   37,  165,   36,  164,   35,  163,   34,  162,   33,  161,
        },
        { /* S_5 */
               0,  248,  240,  232,  224,  216,  208,  200,  192,  184,  176,  168,  160,  152,  144,  136,
             128,  120,  112,  104,   96,   88,   80,   72,   64,   56,   48,   40,   32,   24,   16,    8,
               1,  249,  241,  233,  225,  217,  209,  201,  193,  185,  177,  169,  161,  153,  145,  137,
             129,  121,  113,  105,   97,   89,   81,   73,   65,   57,   49,   41,   33,   25,   17,    9,
        },
        { /* S_6 */
               0,  248,   16,    8,  224,  216,  240,  232,   64,   56,   80,   72,   32,   24,   48,   40,
             128,  120,  144,  136,   96,   88,  112,  104,  192,  184,  208,  200,  160,  152,  176,  168,
             255,  247,   15,    7,  223,  215,  239,  231,   63,   55,   79,   71,   31,   23,   47,   39,
             127,  119,  143,  135,   95,   87,  111,  103,  191,  183,  207,  199,  159,  151,  175,  167,
        },
        { /* S_7 */
               0,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
              -1,   -1,  144,   -1,   -1,   -1,   -1,  104,   64,   56,   -1,   -1,   -1,   -1,   -1,   -1,
              -1,   -1,   -1,   -1,   -1,  219,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
              -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        },
    },
};

/* The transform for one name position.  -1 when this character was never seen and the
   nibble law could not supply it either. */
static int
cd_pic_transform(int which, int pos, uint8_t ch)
{
    if ((ch < CD_PIC_LO) || (ch > (CD_PIC_LO + 63)))
        return -1;
    return cd_pic_s[which][pos][ch - CD_PIC_LO];
}

/* Which of the eight the game's fold actually consumes.  Case 1 aliases: it never
   reads the first two, which is why no amount of data ever determined them. */
static const uint8_t cd_pic_used[4] = { 0xFF, 0xFC, 0xFF, 0xFF };

/* The eight bytes the dongle answers with.  The game folds them itself -- that is the
   whole point of doing it this way round: byte j depends only on name[j], so it can go
   out while the rest of the name is still arriving. */
static int
cd_pic_reply(int which, const uint8_t *name, uint8_t *r)
{
    for (int j = 0; j < 8; j++) {
        const int v = cd_pic_transform(which, j, name[j]);

        if (v < 0) {
            if (!((cd_pic_used[which] >> j) & 1)) {
                r[j] = 0; /* folded away; anything will do */
                continue;
            }
            pp_log("PP: CDONGLE case %d has no transform for '%c' at position %d --"
                   " this name is outside what the cracked keys cover\n",
                   which, (char) name[j], j);
            return 0;
        }
        r[j] = (uint8_t) v;
    }
    return 1;
}

/* Answer a picture-key request.  Returns 0 if this one cannot be served. */
static int
cd_prepare_picture(pp_t *dev)
{
    cd_t         *cd    = &dev->cd;
    const uint16_t konst = (uint16_t) (cd->arg[2] | (cd->arg[3] << 8));
    uint8_t        name[8];

    /* The name is NOT part of the payload.  0x081D sends four header bytes and then
       repeats: push one name byte, read one reply byte.  So byte i of the reply has to
       be on the wire when only name[0..i] has been seen.  That is exactly why the
       device answers S_j(name[j]) and lets the guest fold: reply byte j depends on
       name[j] and nothing later.  Positions not yet sent stand in as spaces; they are
       corrected as they arrive, and nothing reads them before then. */
    for (int i = 0; i < 8; i++)
        name[i] = ((4 + i) < cd->nargs) ? cd->arg[4 + i] : (uint8_t) 0x20;
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

    if (!cd_pic_reply(which, name, r))
        return 0;

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
    /* Refresh the queued bytes without disturbing how far the guest has read: the
       stream is being consumed while the name is still arriving. */
    if (!cd->pic_ready) {
        cd->tx_bit    = 0;
        cd->pic_ready = 1;
    }

    cd->tx_len = 9;
    for (int i = 0; i < 9; i++)
        cd->tx[i] = (uint8_t) (wire[i] ^ cd->key);

    /* The reply is refreshed on every name byte, so only announce the finished one
       rather than each partial name on the way to it. */
    if (cd->nargs >= 12)
        pp_log("PP: CDONGLE picture key for \"%.8s\" case %d -> %02X %02X %02X %02X"
               " %02X %02X %02X %02X\n", (const char *) name, which,
               r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
    return 1;
}

/* Queue whatever this command is owed, scrambled with the key the host just told us. */
static void
cd_prepare(pp_t *dev)
{
    cd_t *cd = &dev->cd;

    /* The picture-key header is four bytes: the count, then the 16-bit constant.  The
       name follows one byte per read, so answer from the header on and keep the reply
       up to date as the rest lands. */
    if (((cd->cmd == 0xAA) || (cd->cmd == 0xAB)) && (cd->nargs >= 4)) {
        if (cd_prepare_picture(dev))
            return;
    }

    cd->tx_bit = 0;
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

    /* Lay the record out the way the hardware does: dwords first at their fixed offset,
       then the banner written over the start.  A banner longer than the field clips the
       first bytes of v[0], which nothing reads -- see the note by PP_BANNER_1999. */
    const size_t blen = strlen(banner);

    memset(dev->block, 0, sizeof(dev->block));
    for (size_t n = 0; n < 8; n++) {
        const size_t o = PP_BANNER_1999 + (n * 4);

        if ((o + 4) > sizeof(dev->block))
            break;
        dev->block[o]     = (uint8_t) (pp_dwords[n]);
        dev->block[o + 1] = (uint8_t) (pp_dwords[n] >> 8);
        dev->block[o + 2] = (uint8_t) (pp_dwords[n] >> 16);
        dev->block[o + 3] = (uint8_t) (pp_dwords[n] >> 24);
    }
    memcpy(dev->block, banner, blen);
    dev->block[blen] = 0;

    pp_log("PP: Photo Play dongle attached, banner \"%s\" (%u chars), dwords at +%02X;"
           " FINDIT reads +1C = %02X%02X%02X%02X\n",
           banner, (unsigned) blen, PP_BANNER_1999,
           dev->block[0x1F], dev->block[0x1E], dev->block[0x1D], dev->block[0x1C]);

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
