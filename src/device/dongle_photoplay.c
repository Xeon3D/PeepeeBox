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
#include "cpu.h"   /* CS and cpu_state.pc, so the trace can name the caller */
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/lpt.h>
#include <86box/photoplay.h>

/* The 2008 generation's token lives on COM2 rather than the parallel port, so it is a
   device of its own.  This one brings it up; see dongle_igo8.c. */
extern const device_t igo8_reader_device;

/* Protocol bring-up: log every transaction.  Remove once this is trusted. */
#define ENABLE_DONGLE_PHOTOPLAY_LOG 1

#define PP_OUT_MAX 512
#define PP_IN_MAX  256
#define PP_BLOCK   62 /* what KEYN.COM serves; type 3 returns the first 48 */

/* The 2001 generation's dongle -- a Microwire EEPROM.  Sizes and pin map here, the
   protocol itself over pp_read_status. */
/* Measured, not chosen.  In the passthrough capture a real part is addressed with
   twenty-five clocks per read -- a start bit, two opcode bits, **six** address bits and
   sixteen data bits -- so the device holds 64 words, not 256.  That also explains the
   record: the library adds HD_START to the caller's word and asks for 56, and 8 + 56 is
   exactly 64.  Advertising 256 made the guest clock eight address bits, which is why the
   measured identity answer of docs/research/32 first came out as a garbled banner.  See
   docs/research/32 section 6. */
#define HD_WORDS  64
#define HD_ABITS  6
#define HD_BANNER 30  /* the banner's column count; the numeric fields follow it */
#define HD_RECORD 112 /* 56 words, which is what service 0x32 asks for */
#define HD_START  8   /* the library adds 8 to the caller's start word */
                 /* the scramble key is the release's first password -- hd_key() */

#define HD_CS 0x02 /* DATA bit 1 */
#define HD_SK 0x20 /* DATA bit 5 */
#define HD_DI 0x40 /* DATA bit 6 */
#define HD_DO 0x20 /* STATUS bit 5 */

/* What a real part puts on DO during the identity ramp, one bit per address 0..63,
   measured off the 68BB/1329 dongle in docs/research/evidence/igo2-dongle-wire-*.log.gz.
   96 ramps in that capture, all identical.  See pp_read_status. */
#define HD_SIGNATURE 0xCEFF0AFFCECE0A0AULL

/* The session layer does three things and this device used to answer all of them with the
   ramp rule.  The other two, both measured off the same capture:

   The 64-step sweep.  The library writes this fixed sequence and reads one bit back each
   time; the part answers the same 64 bits every time, six times over in that run.
   `dongcap` calls these "read and discarded", which is true of dongcap and evidently not
   of the game.

   The 2-step liveness probe.  The library writes 1E then 1C, and the part answers 1 then
   0.  Answering 0 to both -- which the ramp rule does, because address 15 really is clear
   in the signature -- is precisely the stuck line that gate exists to reject, and it
   happens 59 times in one boot. */
static const uint8_t hs_sweep_w[64] = {
    0x78, 0x6A, 0x56, 0x26, 0x02, 0x18, 0x6E, 0x3C, 0x2E, 0x3A, 0x72, 0x52,
    0x0C, 0x64, 0x70, 0x74, 0x2E, 0x24, 0x78, 0x36, 0x22, 0x0C, 0x1C, 0x26,
    0x78, 0x28, 0x68, 0x54, 0x40, 0x0C, 0x70, 0x52, 0x0C, 0x46, 0x44, 0x2E,
    0x6A, 0x68, 0x70, 0x78, 0x6A, 0x7E, 0x08, 0x40, 0x1C, 0x1C, 0x1A, 0x16,
    0x12, 0x50, 0x36, 0x0C, 0x58, 0x2C, 0x6C, 0x30, 0x04, 0x3C, 0x4E, 0x12,
    0x20, 0x14, 0x6A, 0x44
};
#define HS_SWEEP_A 0xF57A37E78F8FBDDAULL

/* The sweep is not a recording to replay: it is a table.  Measured on real parts through
   the passthrough, 2026-09-24 (docs/research-v2/10): after the burst that opens a sweep,
   a write answers the bit at the address in its payload (bits 1..6), exactly as the
   identity ramp does -- only from a second table, and that table belongs to the password
   pair.  Repeated payloads always get the same answer, on every part tried.

   Address a is bit 63 - a.  The 68BB/1329 table reproduces HS_SWEEP_A, the capture this
   device was first built from, to the bit; 7477/7D57 was read off the 2001 PT dongle and
   6B91/24A3 off the 2003 PT and 2005 PT dongles, which agree.  The identity table,
   HD_SIGNATURE, is the same on all three pairs. */
static uint64_t
hs_sweep_table(uint16_t pass1)
{
    switch (pass1) {
        case 0x7477: return 0x423ED3BFE2BEF3BFULL; /* Photo Play 2001 */
        case 0x6B91: return 0x0EE697F74CE4D5F5ULL; /* I.G.O. 3 and 5 */
        default:     return 0x225E9EDE445CDCDCULL; /* 68BB: I.G.O. 2, 6, 7 and Italy */
    }
}

/* The burst that opens a sweep is the password, spelled out.  The library clocks 46 and
   then fifteen command bytes, and each of those is a fixed table lookup on one nibble of
   the password word (pass2 << 16 | pass1): bytes 1..8 take nibbles 0..7, bytes 9..13 take
   7 down to 3, byte 15 takes nibble 1, byte 14 is constant.  Read out of I.G.O. 3's
   library under unicorn over 170 passwords, every burst reproduced; I.G.O. 6's and
   Italy's libraries send the same bytes (docs/research-v2/10.11).  The same burst with
   byte 10 replaced by 50 opens the identity table instead, and the zero password's
   burst -- the one every library sends before the identity ramp -- is special-cased by
   the library to hs_ramp_burst.

   A real part answers only its own password's burst: given another pair's, it says
   nothing until it next sees one it knows.  Measured on the IGO 8 Italy dongle, a
   68BB/1329 part: the library's status call with 7477/7D57 fails and with 68BB/1329
   succeeds. */
static const uint8_t hs_burst_tab[15][16] = {
    { 0x1E, 0x2E, 0x48, 0x06, 0x18, 0x28, 0x0C, 0x00, 0x6A, 0x0C, 0x3A, 0x5A, 0x22, 0x42, 0x4E, 0x12 },
    { 0x58, 0x68, 0x70, 0x38, 0x58, 0x68, 0x70, 0x7C, 0x7C, 0x38, 0x38, 0x58, 0x38, 0x58, 0x68, 0x38 },
    { 0x4A, 0x7A, 0x1A, 0x52, 0x4A, 0x7A, 0x5E, 0x52, 0x1A, 0x5E, 0x7A, 0x1A, 0x7A, 0x1A, 0x1A, 0x4A },
    { 0x7A, 0x3E, 0x1A, 0x7A, 0x1A, 0x7A, 0x7A, 0x2A, 0x2A, 0x1A, 0x7A, 0x32, 0x2A, 0x1A, 0x3E, 0x32 },
    { 0x34, 0x04, 0x1C, 0x54, 0x34, 0x04, 0x1C, 0x10, 0x10, 0x54, 0x54, 0x34, 0x54, 0x34, 0x04, 0x54 },
    { 0x4C, 0x08, 0x08, 0x68, 0x08, 0x68, 0x58, 0x08, 0x68, 0x58, 0x40, 0x08, 0x68, 0x58, 0x40, 0x4C },
    { 0x68, 0x2C, 0x08, 0x68, 0x08, 0x68, 0x68, 0x38, 0x38, 0x08, 0x68, 0x20, 0x38, 0x08, 0x2C, 0x20 },
    { 0x04, 0x40, 0x40, 0x20, 0x40, 0x20, 0x10, 0x40, 0x20, 0x10, 0x08, 0x40, 0x20, 0x10, 0x08, 0x04 },
    { 0x52, 0x32, 0x02, 0x1A, 0x52, 0x32, 0x02, 0x1A, 0x16, 0x16, 0x52, 0x52, 0x32, 0x52, 0x32, 0x02 },
    { 0x70, 0x10, 0x58, 0x40, 0x70, 0x10, 0x58, 0x40, 0x54, 0x54, 0x10, 0x10, 0x10, 0x70, 0x40, 0x70 },
    { 0x70, 0x10, 0x20, 0x38, 0x70, 0x10, 0x20, 0x38, 0x34, 0x34, 0x70, 0x70, 0x10, 0x70, 0x10, 0x20 },
    { 0x08, 0x08, 0x4C, 0x4C, 0x2C, 0x4C, 0x2C, 0x1C, 0x4C, 0x2C, 0x1C, 0x04, 0x4C, 0x2C, 0x1C, 0x04 },
    { 0x3E, 0x5E, 0x16, 0x0E, 0x3E, 0x5E, 0x16, 0x0E, 0x1A, 0x1A, 0x5E, 0x5E, 0x5E, 0x3E, 0x0E, 0x3E },
    { 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C },
    { 0x38, 0x38, 0x7C, 0x7C, 0x1C, 0x7C, 0x1C, 0x2C, 0x7C, 0x1C, 0x2C, 0x34, 0x7C, 0x1C, 0x2C, 0x34 }
};
static const uint8_t hs_burst_nib[15] = { 0, 1, 2, 3, 4, 5, 6, 7, 7, 6, 5, 4, 3, 0, 1 };
static const uint8_t hs_ramp_burst[15] = {
    0x0A, 0x78, 0x5A, 0x3A, 0x14, 0x48, 0x28, 0x00, 0x12, 0x50, 0x30, 0x0C, 0x1E, 0x5C, 0x3C
};
#define HS_BURST_MODE 9 /* index of the byte the identity variant sets to 50 */

/* What a burst leaves the part doing; the last three are also its mode. */
enum { HB_OTHER = 0, HB_SWEEP, HB_IDENT, HB_FOREIGN };

/* What a part holding `pw` (pass2 << 16 | pass1) makes of a fifteen-byte burst: its own
   sweep or identity burst, the zero-password ramp burst (which every part answers), a
   sweep burst for some other password, or something else -- a keyed-round preamble or a
   service request -- which leaves it as it was.  "Some other password" means the bytes
   decode consistently through the tables; over a full real I.G.O. 2 boot no other burst
   does, nor any of 100,000 random ones. */
static int
hs_burst_class(const uint8_t *b, uint32_t pw)
{
    uint16_t cand[8];
    int      own   = 1;
    int      ident = 0;

    if (!memcmp(b, hs_ramp_burst, sizeof(hs_ramp_burst)))
        return HB_IDENT;
    for (int n = 0; n < 8; n++)
        cand[n] = 0xFFFF;
    for (int i = 0; i < 15; i++) {
        const int     n    = hs_burst_nib[i];
        const uint8_t want = hs_burst_tab[i][(pw >> (4 * n)) & 0x0F];
        uint16_t      fits = 0;

        if ((i == HS_BURST_MODE) && (b[i] == 0x50) && (want != 0x50)) {
            ident = 1;
            continue;
        }
        if (b[i] != want)
            own = 0;
        for (int v = 0; v < 16; v++)
            if (hs_burst_tab[i][v] == b[i])
                fits |= (uint16_t) (1u << v);
        cand[n] &= fits;
    }
    if (own)
        return ident ? HB_IDENT : HB_SWEEP;
    for (int n = 0; n < 8; n++)
        if (!cand[n])
            return HB_OTHER;
    return HB_FOREIGN;
}

/* The pair's second password, for the burst check. */
static uint16_t
hd_pass2(uint16_t pass1)
{
    switch (pass1) {
        case 0x7477: return 0x7D57;
        case 0x6B91: return 0x24A3;
        default:     return 0x1329; /* 68BB */
    }
}

enum {
    HD_IDLE = 0, /* deselected, or waiting for a start bit */
    HD_OP,
    HD_ADDR,
    HD_READ,
    HD_WRITE,
    HD_DONE
};

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
    int     refuse;      /* a query the part does not answer: ACK stays up, never a reply */
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
    int     dumped_cs;         /* the code dump above happens once */

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

    /* 2001 HDONGLE -- a Microwire serial EEPROM.  See the section above pp_read_status. */
    int      hd_probe;
    int      hd_sk;             /* the last clock level seen on the data lines */
    int      hd_ph;             /* which part of an instruction is being clocked in */
    int      hd_n;              /* bits done in this part */
    int      hd_op;             /* the two opcode bits */
    uint8_t  hd_addr;
    int      hd_abits;          /* address bits per instruction: HD_ABITS, or 8 when
                                   the synthesised identity has advertised 256 words */
    uint16_t hd_sr;             /* the word going out, or the one coming in */
    int      hd_do;             /* the level this chip is driving on DO */
    int      hd_wen;            /* EWEN seen, so a write will take */
    int      hd_ready;          /* a write finished; DO answers the busy poll */
    int      hd_reads;          /* read instructions decoded, for the log */
    uint16_t hd_mem[HD_WORDS];

    /* Session layer.  Advanced by the status reads, never by the writes: Microwire
       traffic is bit-7-clear too, so anything driven off DATA alone drifts. */
    int      hs_ramping;        /* the ascending identity ramp is in progress */
    uint8_t  hs_ramp_prev;      /* ...and the last step of it answered */
    int      hs_sweep;          /* how far into the 64-step sweep */
    uint8_t  hs_fold[8];        /* the sweep answers, folded the way 0x24FB does */
    int      hs_fold_next;      /* ...and which step is expected next, to spot lost sync */
    uint64_t hs_table;          /* the sweep table of this release's password pair */
    int      hb_gate;           /* check the password in each burst -- see hs_burst_class() */
    uint32_t hb_pw;             /* the part's own pair, pass2 << 16 | pass1 */
    uint8_t  hb_prev;           /* the last DATA write, for bit 0's rising edge */
    int      hb_n;              /* bytes collected since a 46, -1 when not collecting */
    uint8_t  hb_win[15];
    int      hb_mode;           /* HB_SWEEP, HB_IDENT, or HB_FOREIGN: silent until an own burst */
    int      hb_foreign;        /* how many times it went silent, for the log */

    /* The keyed round the picture cipher asks for -- see the section above t_query(). */
    uint32_t t_key;             /* this release's 32 key bits, 0 if it has none */
    int      t_sess_hi;         /* this release's session layer carries bit 7 */
    int      t_synth_ident;     /* answer the session layer with the synthesised
                                   0x1C rule rather than the measured 68BB part */
    uint8_t  t_hold_val;        /* the DATA value a held answer belongs to */
    int      t_burst;           /* consultations since the last preamble */
    int      t_bursts;          /* how many bursts have been logged */
    uint16_t t_init;            /* the register the preamble leaves behind */
    uint32_t t_cur;             /* and where it has got to inside the round */
    uint8_t  t_last;            /* the previous DATA byte, for the clock edges */
    uint8_t  t_ans;             /* the bit waiting to go out on STATUS */
    int      t_pending;         /* ...and whether one is */
    long     t_queries;         /* answered so far, for the log */
    uint8_t  t_recent[3];       /* distinct DATA values since the last STATUS read, newest
                                   last -- how a bit-7 release's query is recognised */
    int      t_nrecent;
    int      t_modeclk;         /* CA->DA clocks since the last 84/A4 run */
    int      t_mode;            /* ...latched by the command byte that opens a round: the
                                   EncodeData mode, 0 for the picture cipher's own */
    int      t_qn;              /* consultations since that command byte */
    long     t_mode_rounds[5];  /* rounds answered in each EncodeData mode, for the log */

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
   right -- and note 2005 is "Version 2005B" on most IGO 5 images.  The IT CZ033 image
   says plain "Version 2005 (IT)" and is an older library that wants a different
   record (see hd_keys); Auto reads that out of MAIN.SET, so it is not offered here.

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
    "Version 2006A", /* IGO 6  - the dumped dongle says 2006A, as 2005 says B  */
    "Version 2007",  /* IGO 7                                                  */
    "Version 2008"   /* IGO 8, and IGO Italy with territory IT                 */
};
#define PP_NBANNERS ((int) (sizeof(pp_banners) / sizeof(pp_banners[0])))

/* Territories, sorted by code.  ES and SP are both here on purpose: the Spanish
   dongles dumped for this project carry "Version 99 (SP)" in their EEPROM, while
   the 2003 Spanish image's MAIN.SET says "Version 2003 (ES)".  funworld changed
   the code between generations, and the banner has to match the image exactly,
   so both have to be offered.  SE is included because an IGO 3 image reads
   "Version 2003 (SE)".

   ZA is appended rather than sorted in: these are indices, and a config that pinned
   a territory before it existed still has to mean what it meant.  The dialog lists
   them alphabetically regardless.  It is ZA and not SA: the one South African image
   known, an IGO 3, says "Version 2003 (ZA)" and Country=ZA in its MAIN.SET.  This
   slot offered "SA" until 1.10.1, which no image uses. */
static const char *pp_terrs[] = {
    "AT", "BE", "CY", "CZ", "DE", "ES", "FR",
    "GR", "IT", "NL", "PT", "SE", "SP",
    "ZA"
};
#define PP_NTERRS ((int) (sizeof(pp_terrs) / sizeof(pp_terrs[0])))

/* What the settings dialog says this dongle currently is.  Both fields default to Auto
   and are read out of the disk image, so without this the dialog shows two boxes reading
   "Auto" and nothing at all about what that resolved to -- and the release decides the
   record layout, the scramble key and whether the part answers on the port. Filled by
   pp_init(), which runs at machine start, well before the dialog can be opened. */
static char pp_ident_text[256] = "Attach the dongle and start the machine to identify it.";

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
    cd->refuse     = 0;
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
 * There is no function behind it to recover.  Asked on real parts through the
 * passthrough (the 2000 PT and 2004 ES dongles, 2026-09-24, docs/research-v2/10), a part
 * answers this challenge and stays silent for every other one: change any byte of the
 * three and the host's claim is never acknowledged.  So these are the part's whole
 * repertoire, as the games use it, and a query outside it is refused the way the part
 * refuses it (cd->refuse) rather than answered with a guess. */
#define CD_CMD_KEY 0xA0 /* API function 1; the wire command byte is func + 0x9F */

static const struct {
    uint8_t cmd;
    uint8_t arg[3];
    uint8_t reply[2];
} cd_answers[] = {
    /* Established: the licence query, the same in all 29 games, MENU.EXE and all
       four 2000 images. */
    { 0xA0, { 0x86, 0x2E, 0xD0 }, { 0x93, 0x46 } }, /* -> the long 0x00004693 */

    /* Measured on the 2000 PT dongle, in a real FIND IT / AMORE / CONCENTRATION session
       through the passthrough.  A1 is the licence query the games send before every AB
       picture request, as A0 goes before every AA.  A3 and A4 are the constants for
       picture cases 2 and 3; they used to be tags of this device's own choosing. */
    { 0xA1, { 0xA6, 0x4B, 0xD0 }, { 0x4E, 0x05 } },
    { 0xA3, { 0x73, 0x07, 0x09 }, { 0x06, 0x12 } }, /* case 2's constant, 0x1206 */
    { 0xA4, { 0x76, 0x02, 0x27 }, { 0x43, 0x73 } }, /* case 3's constant, 0x7343 */
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
 * THE TABLE IS NOW THE PART'S OWN (2026-09-24, docs/research-v2/10).  With a 2000 PT
 * dongle on the passthrough, the query was put to it with every byte value in all eight
 * name positions under each case's command and constant -- 1,024 queries -- and what it
 * answered is what is below.  The fitted table it replaces agreed with it in everything
 * the game computes: each position differed from the part by one constant, and each
 * pair's constants cancel in the fold -- XOR for case 0, ADD for cases 2 and 3, and for
 * case 1 the pairs are 2/5, 3/6 and 4/7, which is why it never reads the first two.
 * The archives could only ever fix the folded value; the part fixes every byte.
 *
 * The constant is an INPUT to the part, not a tag: other constants, and the same constant
 * under the other command, get other answers.  So a request is served only for the four
 * command-and-constant pairs the games use, with the real A3 and A4 values; anything
 * else is refused rather than answered from a table that does not describe it.
 */

#define CD_CONST_CASE0 0x038B /* hardcoded in the game, asked with AA */
#define CD_CONST_CASE1 0x0A8E /* hardcoded in the game, asked with AB */
#define CD_CONST_CASE2 0x1206 /* the part's answer to A3, asked with AA */
#define CD_CONST_CASE3 0x7343 /* the part's answer to A4, asked with AB */

/* Picture-key transform, one byte per character, per case, per position, exactly as the
   2000 PT dongle answered it.  Every entry measured; none filled in or fitted. */
static const uint8_t cd_pic_s[4][8][256] = {
    { /* case 0: AA, constant 038B */
        { /* S_0 */
            0xF8, 0xFA, 0xFC, 0xFE, 0xF0, 0xF2, 0xF4, 0xF6, 0xE8, 0xEA, 0xEC, 0xEE, 0xE0, 0xE2, 0xE4, 0xE6,
            0xD8, 0xDA, 0xDC, 0xDE, 0xD0, 0xD2, 0xD4, 0xD6, 0xC8, 0xCA, 0xCC, 0xCE, 0xC0, 0xC2, 0xC4, 0xC6,
            0xB8, 0xBA, 0xBC, 0xBE, 0xB0, 0xB2, 0xB4, 0xB6, 0xA8, 0xAA, 0xAC, 0xAE, 0xA0, 0xA2, 0xA4, 0xA6,
            0x98, 0x9A, 0x9C, 0x9E, 0x90, 0x92, 0x94, 0x96, 0x88, 0x8A, 0x8C, 0x8E, 0x80, 0x82, 0x84, 0x86,
            0x78, 0x7A, 0x7C, 0x7E, 0x70, 0x72, 0x74, 0x76, 0x68, 0x6A, 0x6C, 0x6E, 0x60, 0x62, 0x64, 0x66,
            0x58, 0x5A, 0x5C, 0x5E, 0x50, 0x52, 0x54, 0x56, 0x48, 0x4A, 0x4C, 0x4E, 0x40, 0x42, 0x44, 0x46,
            0x38, 0x3A, 0x3C, 0x3E, 0x30, 0x32, 0x34, 0x36, 0x28, 0x2A, 0x2C, 0x2E, 0x20, 0x22, 0x24, 0x26,
            0x18, 0x1A, 0x1C, 0x1E, 0x10, 0x12, 0x14, 0x16, 0x08, 0x0A, 0x0C, 0x0E, 0x00, 0x02, 0x04, 0x06,
            0xF9, 0xFB, 0xFD, 0xFF, 0xF1, 0xF3, 0xF5, 0xF7, 0xE9, 0xEB, 0xED, 0xEF, 0xE1, 0xE3, 0xE5, 0xE7,
            0xD9, 0xDB, 0xDD, 0xDF, 0xD1, 0xD3, 0xD5, 0xD7, 0xC9, 0xCB, 0xCD, 0xCF, 0xC1, 0xC3, 0xC5, 0xC7,
            0xB9, 0xBB, 0xBD, 0xBF, 0xB1, 0xB3, 0xB5, 0xB7, 0xA9, 0xAB, 0xAD, 0xAF, 0xA1, 0xA3, 0xA5, 0xA7,
            0x99, 0x9B, 0x9D, 0x9F, 0x91, 0x93, 0x95, 0x97, 0x89, 0x8B, 0x8D, 0x8F, 0x81, 0x83, 0x85, 0x87,
            0x79, 0x7B, 0x7D, 0x7F, 0x71, 0x73, 0x75, 0x77, 0x69, 0x6B, 0x6D, 0x6F, 0x61, 0x63, 0x65, 0x67,
            0x59, 0x5B, 0x5D, 0x5F, 0x51, 0x53, 0x55, 0x57, 0x49, 0x4B, 0x4D, 0x4F, 0x41, 0x43, 0x45, 0x47,
            0x39, 0x3B, 0x3D, 0x3F, 0x31, 0x33, 0x35, 0x37, 0x29, 0x2B, 0x2D, 0x2F, 0x21, 0x23, 0x25, 0x27,
            0x19, 0x1B, 0x1D, 0x1F, 0x11, 0x13, 0x15, 0x17, 0x09, 0x0B, 0x0D, 0x0F, 0x01, 0x03, 0x05, 0x07,
        },
        { /* S_1 */
            0x73, 0x53, 0x33, 0x13, 0xF3, 0xD3, 0xB3, 0x93, 0x72, 0x52, 0x32, 0x12, 0xF2, 0xD2, 0xB2, 0x92,
            0x71, 0x51, 0x31, 0x11, 0xF1, 0xD1, 0xB1, 0x91, 0x70, 0x50, 0x30, 0x10, 0xF0, 0xD0, 0xB0, 0x90,
            0x77, 0x57, 0x37, 0x17, 0xF7, 0xD7, 0xB7, 0x97, 0x76, 0x56, 0x36, 0x16, 0xF6, 0xD6, 0xB6, 0x96,
            0x75, 0x55, 0x35, 0x15, 0xF5, 0xD5, 0xB5, 0x95, 0x74, 0x54, 0x34, 0x14, 0xF4, 0xD4, 0xB4, 0x94,
            0x7B, 0x5B, 0x3B, 0x1B, 0xFB, 0xDB, 0xBB, 0x9B, 0x7A, 0x5A, 0x3A, 0x1A, 0xFA, 0xDA, 0xBA, 0x9A,
            0x79, 0x59, 0x39, 0x19, 0xF9, 0xD9, 0xB9, 0x99, 0x78, 0x58, 0x38, 0x18, 0xF8, 0xD8, 0xB8, 0x98,
            0x7F, 0x5F, 0x3F, 0x1F, 0xFF, 0xDF, 0xBF, 0x9F, 0x7E, 0x5E, 0x3E, 0x1E, 0xFE, 0xDE, 0xBE, 0x9E,
            0x7D, 0x5D, 0x3D, 0x1D, 0xFD, 0xDD, 0xBD, 0x9D, 0x7C, 0x5C, 0x3C, 0x1C, 0xFC, 0xDC, 0xBC, 0x9C,
            0x63, 0x43, 0x23, 0x03, 0xE3, 0xC3, 0xA3, 0x83, 0x62, 0x42, 0x22, 0x02, 0xE2, 0xC2, 0xA2, 0x82,
            0x61, 0x41, 0x21, 0x01, 0xE1, 0xC1, 0xA1, 0x81, 0x60, 0x40, 0x20, 0x00, 0xE0, 0xC0, 0xA0, 0x80,
            0x67, 0x47, 0x27, 0x07, 0xE7, 0xC7, 0xA7, 0x87, 0x66, 0x46, 0x26, 0x06, 0xE6, 0xC6, 0xA6, 0x86,
            0x65, 0x45, 0x25, 0x05, 0xE5, 0xC5, 0xA5, 0x85, 0x64, 0x44, 0x24, 0x04, 0xE4, 0xC4, 0xA4, 0x84,
            0x6B, 0x4B, 0x2B, 0x0B, 0xEB, 0xCB, 0xAB, 0x8B, 0x6A, 0x4A, 0x2A, 0x0A, 0xEA, 0xCA, 0xAA, 0x8A,
            0x69, 0x49, 0x29, 0x09, 0xE9, 0xC9, 0xA9, 0x89, 0x68, 0x48, 0x28, 0x08, 0xE8, 0xC8, 0xA8, 0x88,
            0x6F, 0x4F, 0x2F, 0x0F, 0xEF, 0xCF, 0xAF, 0x8F, 0x6E, 0x4E, 0x2E, 0x0E, 0xEE, 0xCE, 0xAE, 0x8E,
            0x6D, 0x4D, 0x2D, 0x0D, 0xED, 0xCD, 0xAD, 0x8D, 0x6C, 0x4C, 0x2C, 0x0C, 0xEC, 0xCC, 0xAC, 0x8C,
        },
        { /* S_2 */
            0x89, 0xA9, 0xC9, 0xE9, 0x09, 0x29, 0x49, 0x69, 0x88, 0xA8, 0xC8, 0xE8, 0x08, 0x28, 0x48, 0x68,
            0x8B, 0xAB, 0xCB, 0xEB, 0x0B, 0x2B, 0x4B, 0x6B, 0x8A, 0xAA, 0xCA, 0xEA, 0x0A, 0x2A, 0x4A, 0x6A,
            0x8D, 0xAD, 0xCD, 0xED, 0x0D, 0x2D, 0x4D, 0x6D, 0x8C, 0xAC, 0xCC, 0xEC, 0x0C, 0x2C, 0x4C, 0x6C,
            0x8F, 0xAF, 0xCF, 0xEF, 0x0F, 0x2F, 0x4F, 0x6F, 0x8E, 0xAE, 0xCE, 0xEE, 0x0E, 0x2E, 0x4E, 0x6E,
            0x81, 0xA1, 0xC1, 0xE1, 0x01, 0x21, 0x41, 0x61, 0x80, 0xA0, 0xC0, 0xE0, 0x00, 0x20, 0x40, 0x60,
            0x83, 0xA3, 0xC3, 0xE3, 0x03, 0x23, 0x43, 0x63, 0x82, 0xA2, 0xC2, 0xE2, 0x02, 0x22, 0x42, 0x62,
            0x85, 0xA5, 0xC5, 0xE5, 0x05, 0x25, 0x45, 0x65, 0x84, 0xA4, 0xC4, 0xE4, 0x04, 0x24, 0x44, 0x64,
            0x87, 0xA7, 0xC7, 0xE7, 0x07, 0x27, 0x47, 0x67, 0x86, 0xA6, 0xC6, 0xE6, 0x06, 0x26, 0x46, 0x66,
            0x99, 0xB9, 0xD9, 0xF9, 0x19, 0x39, 0x59, 0x79, 0x98, 0xB8, 0xD8, 0xF8, 0x18, 0x38, 0x58, 0x78,
            0x9B, 0xBB, 0xDB, 0xFB, 0x1B, 0x3B, 0x5B, 0x7B, 0x9A, 0xBA, 0xDA, 0xFA, 0x1A, 0x3A, 0x5A, 0x7A,
            0x9D, 0xBD, 0xDD, 0xFD, 0x1D, 0x3D, 0x5D, 0x7D, 0x9C, 0xBC, 0xDC, 0xFC, 0x1C, 0x3C, 0x5C, 0x7C,
            0x9F, 0xBF, 0xDF, 0xFF, 0x1F, 0x3F, 0x5F, 0x7F, 0x9E, 0xBE, 0xDE, 0xFE, 0x1E, 0x3E, 0x5E, 0x7E,
            0x91, 0xB1, 0xD1, 0xF1, 0x11, 0x31, 0x51, 0x71, 0x90, 0xB0, 0xD0, 0xF0, 0x10, 0x30, 0x50, 0x70,
            0x93, 0xB3, 0xD3, 0xF3, 0x13, 0x33, 0x53, 0x73, 0x92, 0xB2, 0xD2, 0xF2, 0x12, 0x32, 0x52, 0x72,
            0x95, 0xB5, 0xD5, 0xF5, 0x15, 0x35, 0x55, 0x75, 0x94, 0xB4, 0xD4, 0xF4, 0x14, 0x34, 0x54, 0x74,
            0x97, 0xB7, 0xD7, 0xF7, 0x17, 0x37, 0x57, 0x77, 0x96, 0xB6, 0xD6, 0xF6, 0x16, 0x36, 0x56, 0x76,
        },
        { /* S_3 */
            0xFD, 0xDD, 0xBD, 0x9D, 0x7D, 0x5D, 0x3D, 0x1D, 0xFC, 0xDC, 0xBC, 0x9C, 0x7C, 0x5C, 0x3C, 0x1C,
            0xFF, 0xDF, 0xBF, 0x9F, 0x7F, 0x5F, 0x3F, 0x1F, 0xFE, 0xDE, 0xBE, 0x9E, 0x7E, 0x5E, 0x3E, 0x1E,
            0xF9, 0xD9, 0xB9, 0x99, 0x79, 0x59, 0x39, 0x19, 0xF8, 0xD8, 0xB8, 0x98, 0x78, 0x58, 0x38, 0x18,
            0xFB, 0xDB, 0xBB, 0x9B, 0x7B, 0x5B, 0x3B, 0x1B, 0xFA, 0xDA, 0xBA, 0x9A, 0x7A, 0x5A, 0x3A, 0x1A,
            0xF5, 0xD5, 0xB5, 0x95, 0x75, 0x55, 0x35, 0x15, 0xF4, 0xD4, 0xB4, 0x94, 0x74, 0x54, 0x34, 0x14,
            0xF7, 0xD7, 0xB7, 0x97, 0x77, 0x57, 0x37, 0x17, 0xF6, 0xD6, 0xB6, 0x96, 0x76, 0x56, 0x36, 0x16,
            0xF1, 0xD1, 0xB1, 0x91, 0x71, 0x51, 0x31, 0x11, 0xF0, 0xD0, 0xB0, 0x90, 0x70, 0x50, 0x30, 0x10,
            0xF3, 0xD3, 0xB3, 0x93, 0x73, 0x53, 0x33, 0x13, 0xF2, 0xD2, 0xB2, 0x92, 0x72, 0x52, 0x32, 0x12,
            0xED, 0xCD, 0xAD, 0x8D, 0x6D, 0x4D, 0x2D, 0x0D, 0xEC, 0xCC, 0xAC, 0x8C, 0x6C, 0x4C, 0x2C, 0x0C,
            0xEF, 0xCF, 0xAF, 0x8F, 0x6F, 0x4F, 0x2F, 0x0F, 0xEE, 0xCE, 0xAE, 0x8E, 0x6E, 0x4E, 0x2E, 0x0E,
            0xE9, 0xC9, 0xA9, 0x89, 0x69, 0x49, 0x29, 0x09, 0xE8, 0xC8, 0xA8, 0x88, 0x68, 0x48, 0x28, 0x08,
            0xEB, 0xCB, 0xAB, 0x8B, 0x6B, 0x4B, 0x2B, 0x0B, 0xEA, 0xCA, 0xAA, 0x8A, 0x6A, 0x4A, 0x2A, 0x0A,
            0xE5, 0xC5, 0xA5, 0x85, 0x65, 0x45, 0x25, 0x05, 0xE4, 0xC4, 0xA4, 0x84, 0x64, 0x44, 0x24, 0x04,
            0xE7, 0xC7, 0xA7, 0x87, 0x67, 0x47, 0x27, 0x07, 0xE6, 0xC6, 0xA6, 0x86, 0x66, 0x46, 0x26, 0x06,
            0xE1, 0xC1, 0xA1, 0x81, 0x61, 0x41, 0x21, 0x01, 0xE0, 0xC0, 0xA0, 0x80, 0x60, 0x40, 0x20, 0x00,
            0xE3, 0xC3, 0xA3, 0x83, 0x63, 0x43, 0x23, 0x03, 0xE2, 0xC2, 0xA2, 0x82, 0x62, 0x42, 0x22, 0x02,
        },
        { /* S_4 */
            0x37, 0x35, 0x33, 0x31, 0x3F, 0x3D, 0x3B, 0x39, 0x27, 0x25, 0x23, 0x21, 0x2F, 0x2D, 0x2B, 0x29,
            0x17, 0x15, 0x13, 0x11, 0x1F, 0x1D, 0x1B, 0x19, 0x07, 0x05, 0x03, 0x01, 0x0F, 0x0D, 0x0B, 0x09,
            0x77, 0x75, 0x73, 0x71, 0x7F, 0x7D, 0x7B, 0x79, 0x67, 0x65, 0x63, 0x61, 0x6F, 0x6D, 0x6B, 0x69,
            0x57, 0x55, 0x53, 0x51, 0x5F, 0x5D, 0x5B, 0x59, 0x47, 0x45, 0x43, 0x41, 0x4F, 0x4D, 0x4B, 0x49,
            0xB7, 0xB5, 0xB3, 0xB1, 0xBF, 0xBD, 0xBB, 0xB9, 0xA7, 0xA5, 0xA3, 0xA1, 0xAF, 0xAD, 0xAB, 0xA9,
            0x97, 0x95, 0x93, 0x91, 0x9F, 0x9D, 0x9B, 0x99, 0x87, 0x85, 0x83, 0x81, 0x8F, 0x8D, 0x8B, 0x89,
            0xF7, 0xF5, 0xF3, 0xF1, 0xFF, 0xFD, 0xFB, 0xF9, 0xE7, 0xE5, 0xE3, 0xE1, 0xEF, 0xED, 0xEB, 0xE9,
            0xD7, 0xD5, 0xD3, 0xD1, 0xDF, 0xDD, 0xDB, 0xD9, 0xC7, 0xC5, 0xC3, 0xC1, 0xCF, 0xCD, 0xCB, 0xC9,
            0x36, 0x34, 0x32, 0x30, 0x3E, 0x3C, 0x3A, 0x38, 0x26, 0x24, 0x22, 0x20, 0x2E, 0x2C, 0x2A, 0x28,
            0x16, 0x14, 0x12, 0x10, 0x1E, 0x1C, 0x1A, 0x18, 0x06, 0x04, 0x02, 0x00, 0x0E, 0x0C, 0x0A, 0x08,
            0x76, 0x74, 0x72, 0x70, 0x7E, 0x7C, 0x7A, 0x78, 0x66, 0x64, 0x62, 0x60, 0x6E, 0x6C, 0x6A, 0x68,
            0x56, 0x54, 0x52, 0x50, 0x5E, 0x5C, 0x5A, 0x58, 0x46, 0x44, 0x42, 0x40, 0x4E, 0x4C, 0x4A, 0x48,
            0xB6, 0xB4, 0xB2, 0xB0, 0xBE, 0xBC, 0xBA, 0xB8, 0xA6, 0xA4, 0xA2, 0xA0, 0xAE, 0xAC, 0xAA, 0xA8,
            0x96, 0x94, 0x92, 0x90, 0x9E, 0x9C, 0x9A, 0x98, 0x86, 0x84, 0x82, 0x80, 0x8E, 0x8C, 0x8A, 0x88,
            0xF6, 0xF4, 0xF2, 0xF0, 0xFE, 0xFC, 0xFA, 0xF8, 0xE6, 0xE4, 0xE2, 0xE0, 0xEE, 0xEC, 0xEA, 0xE8,
            0xD6, 0xD4, 0xD2, 0xD0, 0xDE, 0xDC, 0xDA, 0xD8, 0xC6, 0xC4, 0xC2, 0xC0, 0xCE, 0xCC, 0xCA, 0xC8,
        },
        { /* S_5 */
            0x8F, 0xAF, 0xCF, 0xEF, 0x0F, 0x2F, 0x4F, 0x6F, 0x8E, 0xAE, 0xCE, 0xEE, 0x0E, 0x2E, 0x4E, 0x6E,
            0x8D, 0xAD, 0xCD, 0xED, 0x0D, 0x2D, 0x4D, 0x6D, 0x8C, 0xAC, 0xCC, 0xEC, 0x0C, 0x2C, 0x4C, 0x6C,
            0x8B, 0xAB, 0xCB, 0xEB, 0x0B, 0x2B, 0x4B, 0x6B, 0x8A, 0xAA, 0xCA, 0xEA, 0x0A, 0x2A, 0x4A, 0x6A,
            0x89, 0xA9, 0xC9, 0xE9, 0x09, 0x29, 0x49, 0x69, 0x88, 0xA8, 0xC8, 0xE8, 0x08, 0x28, 0x48, 0x68,
            0x87, 0xA7, 0xC7, 0xE7, 0x07, 0x27, 0x47, 0x67, 0x86, 0xA6, 0xC6, 0xE6, 0x06, 0x26, 0x46, 0x66,
            0x85, 0xA5, 0xC5, 0xE5, 0x05, 0x25, 0x45, 0x65, 0x84, 0xA4, 0xC4, 0xE4, 0x04, 0x24, 0x44, 0x64,
            0x83, 0xA3, 0xC3, 0xE3, 0x03, 0x23, 0x43, 0x63, 0x82, 0xA2, 0xC2, 0xE2, 0x02, 0x22, 0x42, 0x62,
            0x81, 0xA1, 0xC1, 0xE1, 0x01, 0x21, 0x41, 0x61, 0x80, 0xA0, 0xC0, 0xE0, 0x00, 0x20, 0x40, 0x60,
            0x9F, 0xBF, 0xDF, 0xFF, 0x1F, 0x3F, 0x5F, 0x7F, 0x9E, 0xBE, 0xDE, 0xFE, 0x1E, 0x3E, 0x5E, 0x7E,
            0x9D, 0xBD, 0xDD, 0xFD, 0x1D, 0x3D, 0x5D, 0x7D, 0x9C, 0xBC, 0xDC, 0xFC, 0x1C, 0x3C, 0x5C, 0x7C,
            0x9B, 0xBB, 0xDB, 0xFB, 0x1B, 0x3B, 0x5B, 0x7B, 0x9A, 0xBA, 0xDA, 0xFA, 0x1A, 0x3A, 0x5A, 0x7A,
            0x99, 0xB9, 0xD9, 0xF9, 0x19, 0x39, 0x59, 0x79, 0x98, 0xB8, 0xD8, 0xF8, 0x18, 0x38, 0x58, 0x78,
            0x97, 0xB7, 0xD7, 0xF7, 0x17, 0x37, 0x57, 0x77, 0x96, 0xB6, 0xD6, 0xF6, 0x16, 0x36, 0x56, 0x76,
            0x95, 0xB5, 0xD5, 0xF5, 0x15, 0x35, 0x55, 0x75, 0x94, 0xB4, 0xD4, 0xF4, 0x14, 0x34, 0x54, 0x74,
            0x93, 0xB3, 0xD3, 0xF3, 0x13, 0x33, 0x53, 0x73, 0x92, 0xB2, 0xD2, 0xF2, 0x12, 0x32, 0x52, 0x72,
            0x91, 0xB1, 0xD1, 0xF1, 0x11, 0x31, 0x51, 0x71, 0x90, 0xB0, 0xD0, 0xF0, 0x10, 0x30, 0x50, 0x70,
        },
        { /* S_6 */
            0x75, 0x55, 0x35, 0x15, 0xF5, 0xD5, 0xB5, 0x95, 0x74, 0x54, 0x34, 0x14, 0xF4, 0xD4, 0xB4, 0x94,
            0x77, 0x57, 0x37, 0x17, 0xF7, 0xD7, 0xB7, 0x97, 0x76, 0x56, 0x36, 0x16, 0xF6, 0xD6, 0xB6, 0x96,
            0x71, 0x51, 0x31, 0x11, 0xF1, 0xD1, 0xB1, 0x91, 0x70, 0x50, 0x30, 0x10, 0xF0, 0xD0, 0xB0, 0x90,
            0x73, 0x53, 0x33, 0x13, 0xF3, 0xD3, 0xB3, 0x93, 0x72, 0x52, 0x32, 0x12, 0xF2, 0xD2, 0xB2, 0x92,
            0x7D, 0x5D, 0x3D, 0x1D, 0xFD, 0xDD, 0xBD, 0x9D, 0x7C, 0x5C, 0x3C, 0x1C, 0xFC, 0xDC, 0xBC, 0x9C,
            0x7F, 0x5F, 0x3F, 0x1F, 0xFF, 0xDF, 0xBF, 0x9F, 0x7E, 0x5E, 0x3E, 0x1E, 0xFE, 0xDE, 0xBE, 0x9E,
            0x79, 0x59, 0x39, 0x19, 0xF9, 0xD9, 0xB9, 0x99, 0x78, 0x58, 0x38, 0x18, 0xF8, 0xD8, 0xB8, 0x98,
            0x7B, 0x5B, 0x3B, 0x1B, 0xFB, 0xDB, 0xBB, 0x9B, 0x7A, 0x5A, 0x3A, 0x1A, 0xFA, 0xDA, 0xBA, 0x9A,
            0x65, 0x45, 0x25, 0x05, 0xE5, 0xC5, 0xA5, 0x85, 0x64, 0x44, 0x24, 0x04, 0xE4, 0xC4, 0xA4, 0x84,
            0x67, 0x47, 0x27, 0x07, 0xE7, 0xC7, 0xA7, 0x87, 0x66, 0x46, 0x26, 0x06, 0xE6, 0xC6, 0xA6, 0x86,
            0x61, 0x41, 0x21, 0x01, 0xE1, 0xC1, 0xA1, 0x81, 0x60, 0x40, 0x20, 0x00, 0xE0, 0xC0, 0xA0, 0x80,
            0x63, 0x43, 0x23, 0x03, 0xE3, 0xC3, 0xA3, 0x83, 0x62, 0x42, 0x22, 0x02, 0xE2, 0xC2, 0xA2, 0x82,
            0x6D, 0x4D, 0x2D, 0x0D, 0xED, 0xCD, 0xAD, 0x8D, 0x6C, 0x4C, 0x2C, 0x0C, 0xEC, 0xCC, 0xAC, 0x8C,
            0x6F, 0x4F, 0x2F, 0x0F, 0xEF, 0xCF, 0xAF, 0x8F, 0x6E, 0x4E, 0x2E, 0x0E, 0xEE, 0xCE, 0xAE, 0x8E,
            0x69, 0x49, 0x29, 0x09, 0xE9, 0xC9, 0xA9, 0x89, 0x68, 0x48, 0x28, 0x08, 0xE8, 0xC8, 0xA8, 0x88,
            0x6B, 0x4B, 0x2B, 0x0B, 0xEB, 0xCB, 0xAB, 0x8B, 0x6A, 0x4A, 0x2A, 0x0A, 0xEA, 0xCA, 0xAA, 0x8A,
        },
        { /* S_7 */
            0xE5, 0xC5, 0xA5, 0x85, 0x65, 0x45, 0x25, 0x05, 0xE4, 0xC4, 0xA4, 0x84, 0x64, 0x44, 0x24, 0x04,
            0xE7, 0xC7, 0xA7, 0x87, 0x67, 0x47, 0x27, 0x07, 0xE6, 0xC6, 0xA6, 0x86, 0x66, 0x46, 0x26, 0x06,
            0xE1, 0xC1, 0xA1, 0x81, 0x61, 0x41, 0x21, 0x01, 0xE0, 0xC0, 0xA0, 0x80, 0x60, 0x40, 0x20, 0x00,
            0xE3, 0xC3, 0xA3, 0x83, 0x63, 0x43, 0x23, 0x03, 0xE2, 0xC2, 0xA2, 0x82, 0x62, 0x42, 0x22, 0x02,
            0xED, 0xCD, 0xAD, 0x8D, 0x6D, 0x4D, 0x2D, 0x0D, 0xEC, 0xCC, 0xAC, 0x8C, 0x6C, 0x4C, 0x2C, 0x0C,
            0xEF, 0xCF, 0xAF, 0x8F, 0x6F, 0x4F, 0x2F, 0x0F, 0xEE, 0xCE, 0xAE, 0x8E, 0x6E, 0x4E, 0x2E, 0x0E,
            0xE9, 0xC9, 0xA9, 0x89, 0x69, 0x49, 0x29, 0x09, 0xE8, 0xC8, 0xA8, 0x88, 0x68, 0x48, 0x28, 0x08,
            0xEB, 0xCB, 0xAB, 0x8B, 0x6B, 0x4B, 0x2B, 0x0B, 0xEA, 0xCA, 0xAA, 0x8A, 0x6A, 0x4A, 0x2A, 0x0A,
            0xF5, 0xD5, 0xB5, 0x95, 0x75, 0x55, 0x35, 0x15, 0xF4, 0xD4, 0xB4, 0x94, 0x74, 0x54, 0x34, 0x14,
            0xF7, 0xD7, 0xB7, 0x97, 0x77, 0x57, 0x37, 0x17, 0xF6, 0xD6, 0xB6, 0x96, 0x76, 0x56, 0x36, 0x16,
            0xF1, 0xD1, 0xB1, 0x91, 0x71, 0x51, 0x31, 0x11, 0xF0, 0xD0, 0xB0, 0x90, 0x70, 0x50, 0x30, 0x10,
            0xF3, 0xD3, 0xB3, 0x93, 0x73, 0x53, 0x33, 0x13, 0xF2, 0xD2, 0xB2, 0x92, 0x72, 0x52, 0x32, 0x12,
            0xFD, 0xDD, 0xBD, 0x9D, 0x7D, 0x5D, 0x3D, 0x1D, 0xFC, 0xDC, 0xBC, 0x9C, 0x7C, 0x5C, 0x3C, 0x1C,
            0xFF, 0xDF, 0xBF, 0x9F, 0x7F, 0x5F, 0x3F, 0x1F, 0xFE, 0xDE, 0xBE, 0x9E, 0x7E, 0x5E, 0x3E, 0x1E,
            0xF9, 0xD9, 0xB9, 0x99, 0x79, 0x59, 0x39, 0x19, 0xF8, 0xD8, 0xB8, 0x98, 0x78, 0x58, 0x38, 0x18,
            0xFB, 0xDB, 0xBB, 0x9B, 0x7B, 0x5B, 0x3B, 0x1B, 0xFA, 0xDA, 0xBA, 0x9A, 0x7A, 0x5A, 0x3A, 0x1A,
        },
    },
    { /* case 1: AB, constant 0A8E */
        { /* S_0 */
            0x58, 0x50, 0x48, 0x40, 0x78, 0x70, 0x68, 0x60, 0x18, 0x10, 0x08, 0x00, 0x38, 0x30, 0x28, 0x20,
            0xD8, 0xD0, 0xC8, 0xC0, 0xF8, 0xF0, 0xE8, 0xE0, 0x98, 0x90, 0x88, 0x80, 0xB8, 0xB0, 0xA8, 0xA0,
            0x59, 0x51, 0x49, 0x41, 0x79, 0x71, 0x69, 0x61, 0x19, 0x11, 0x09, 0x01, 0x39, 0x31, 0x29, 0x21,
            0xD9, 0xD1, 0xC9, 0xC1, 0xF9, 0xF1, 0xE9, 0xE1, 0x99, 0x91, 0x89, 0x81, 0xB9, 0xB1, 0xA9, 0xA1,
            0x5A, 0x52, 0x4A, 0x42, 0x7A, 0x72, 0x6A, 0x62, 0x1A, 0x12, 0x0A, 0x02, 0x3A, 0x32, 0x2A, 0x22,
            0xDA, 0xD2, 0xCA, 0xC2, 0xFA, 0xF2, 0xEA, 0xE2, 0x9A, 0x92, 0x8A, 0x82, 0xBA, 0xB2, 0xAA, 0xA2,
            0x5B, 0x53, 0x4B, 0x43, 0x7B, 0x73, 0x6B, 0x63, 0x1B, 0x13, 0x0B, 0x03, 0x3B, 0x33, 0x2B, 0x23,
            0xDB, 0xD3, 0xCB, 0xC3, 0xFB, 0xF3, 0xEB, 0xE3, 0x9B, 0x93, 0x8B, 0x83, 0xBB, 0xB3, 0xAB, 0xA3,
            0x5C, 0x54, 0x4C, 0x44, 0x7C, 0x74, 0x6C, 0x64, 0x1C, 0x14, 0x0C, 0x04, 0x3C, 0x34, 0x2C, 0x24,
            0xDC, 0xD4, 0xCC, 0xC4, 0xFC, 0xF4, 0xEC, 0xE4, 0x9C, 0x94, 0x8C, 0x84, 0xBC, 0xB4, 0xAC, 0xA4,
            0x5D, 0x55, 0x4D, 0x45, 0x7D, 0x75, 0x6D, 0x65, 0x1D, 0x15, 0x0D, 0x05, 0x3D, 0x35, 0x2D, 0x25,
            0xDD, 0xD5, 0xCD, 0xC5, 0xFD, 0xF5, 0xED, 0xE5, 0x9D, 0x95, 0x8D, 0x85, 0xBD, 0xB5, 0xAD, 0xA5,
            0x5E, 0x56, 0x4E, 0x46, 0x7E, 0x76, 0x6E, 0x66, 0x1E, 0x16, 0x0E, 0x06, 0x3E, 0x36, 0x2E, 0x26,
            0xDE, 0xD6, 0xCE, 0xC6, 0xFE, 0xF6, 0xEE, 0xE6, 0x9E, 0x96, 0x8E, 0x86, 0xBE, 0xB6, 0xAE, 0xA6,
            0x5F, 0x57, 0x4F, 0x47, 0x7F, 0x77, 0x6F, 0x67, 0x1F, 0x17, 0x0F, 0x07, 0x3F, 0x37, 0x2F, 0x27,
            0xDF, 0xD7, 0xCF, 0xC7, 0xFF, 0xF7, 0xEF, 0xE7, 0x9F, 0x97, 0x8F, 0x87, 0xBF, 0xB7, 0xAF, 0xA7,
        },
        { /* S_1 */
            0x2C, 0xAC, 0x2D, 0xAD, 0x2E, 0xAE, 0x2F, 0xAF, 0x28, 0xA8, 0x29, 0xA9, 0x2A, 0xAA, 0x2B, 0xAB,
            0x24, 0xA4, 0x25, 0xA5, 0x26, 0xA6, 0x27, 0xA7, 0x20, 0xA0, 0x21, 0xA1, 0x22, 0xA2, 0x23, 0xA3,
            0x3C, 0xBC, 0x3D, 0xBD, 0x3E, 0xBE, 0x3F, 0xBF, 0x38, 0xB8, 0x39, 0xB9, 0x3A, 0xBA, 0x3B, 0xBB,
            0x34, 0xB4, 0x35, 0xB5, 0x36, 0xB6, 0x37, 0xB7, 0x30, 0xB0, 0x31, 0xB1, 0x32, 0xB2, 0x33, 0xB3,
            0x0C, 0x8C, 0x0D, 0x8D, 0x0E, 0x8E, 0x0F, 0x8F, 0x08, 0x88, 0x09, 0x89, 0x0A, 0x8A, 0x0B, 0x8B,
            0x04, 0x84, 0x05, 0x85, 0x06, 0x86, 0x07, 0x87, 0x00, 0x80, 0x01, 0x81, 0x02, 0x82, 0x03, 0x83,
            0x1C, 0x9C, 0x1D, 0x9D, 0x1E, 0x9E, 0x1F, 0x9F, 0x18, 0x98, 0x19, 0x99, 0x1A, 0x9A, 0x1B, 0x9B,
            0x14, 0x94, 0x15, 0x95, 0x16, 0x96, 0x17, 0x97, 0x10, 0x90, 0x11, 0x91, 0x12, 0x92, 0x13, 0x93,
            0x6C, 0xEC, 0x6D, 0xED, 0x6E, 0xEE, 0x6F, 0xEF, 0x68, 0xE8, 0x69, 0xE9, 0x6A, 0xEA, 0x6B, 0xEB,
            0x64, 0xE4, 0x65, 0xE5, 0x66, 0xE6, 0x67, 0xE7, 0x60, 0xE0, 0x61, 0xE1, 0x62, 0xE2, 0x63, 0xE3,
            0x7C, 0xFC, 0x7D, 0xFD, 0x7E, 0xFE, 0x7F, 0xFF, 0x78, 0xF8, 0x79, 0xF9, 0x7A, 0xFA, 0x7B, 0xFB,
            0x74, 0xF4, 0x75, 0xF5, 0x76, 0xF6, 0x77, 0xF7, 0x70, 0xF0, 0x71, 0xF1, 0x72, 0xF2, 0x73, 0xF3,
            0x4C, 0xCC, 0x4D, 0xCD, 0x4E, 0xCE, 0x4F, 0xCF, 0x48, 0xC8, 0x49, 0xC9, 0x4A, 0xCA, 0x4B, 0xCB,
            0x44, 0xC4, 0x45, 0xC5, 0x46, 0xC6, 0x47, 0xC7, 0x40, 0xC0, 0x41, 0xC1, 0x42, 0xC2, 0x43, 0xC3,
            0x5C, 0xDC, 0x5D, 0xDD, 0x5E, 0xDE, 0x5F, 0xDF, 0x58, 0xD8, 0x59, 0xD9, 0x5A, 0xDA, 0x5B, 0xDB,
            0x54, 0xD4, 0x55, 0xD5, 0x56, 0xD6, 0x57, 0xD7, 0x50, 0xD0, 0x51, 0xD1, 0x52, 0xD2, 0x53, 0xD3,
        },
        { /* S_2 */
            0xAB, 0xA3, 0xBB, 0xB3, 0x8B, 0x83, 0x9B, 0x93, 0xEB, 0xE3, 0xFB, 0xF3, 0xCB, 0xC3, 0xDB, 0xD3,
            0x2B, 0x23, 0x3B, 0x33, 0x0B, 0x03, 0x1B, 0x13, 0x6B, 0x63, 0x7B, 0x73, 0x4B, 0x43, 0x5B, 0x53,
            0xAA, 0xA2, 0xBA, 0xB2, 0x8A, 0x82, 0x9A, 0x92, 0xEA, 0xE2, 0xFA, 0xF2, 0xCA, 0xC2, 0xDA, 0xD2,
            0x2A, 0x22, 0x3A, 0x32, 0x0A, 0x02, 0x1A, 0x12, 0x6A, 0x62, 0x7A, 0x72, 0x4A, 0x42, 0x5A, 0x52,
            0xA9, 0xA1, 0xB9, 0xB1, 0x89, 0x81, 0x99, 0x91, 0xE9, 0xE1, 0xF9, 0xF1, 0xC9, 0xC1, 0xD9, 0xD1,
            0x29, 0x21, 0x39, 0x31, 0x09, 0x01, 0x19, 0x11, 0x69, 0x61, 0x79, 0x71, 0x49, 0x41, 0x59, 0x51,
            0xA8, 0xA0, 0xB8, 0xB0, 0x88, 0x80, 0x98, 0x90, 0xE8, 0xE0, 0xF8, 0xF0, 0xC8, 0xC0, 0xD8, 0xD0,
            0x28, 0x20, 0x38, 0x30, 0x08, 0x00, 0x18, 0x10, 0x68, 0x60, 0x78, 0x70, 0x48, 0x40, 0x58, 0x50,
            0xAF, 0xA7, 0xBF, 0xB7, 0x8F, 0x87, 0x9F, 0x97, 0xEF, 0xE7, 0xFF, 0xF7, 0xCF, 0xC7, 0xDF, 0xD7,
            0x2F, 0x27, 0x3F, 0x37, 0x0F, 0x07, 0x1F, 0x17, 0x6F, 0x67, 0x7F, 0x77, 0x4F, 0x47, 0x5F, 0x57,
            0xAE, 0xA6, 0xBE, 0xB6, 0x8E, 0x86, 0x9E, 0x96, 0xEE, 0xE6, 0xFE, 0xF6, 0xCE, 0xC6, 0xDE, 0xD6,
            0x2E, 0x26, 0x3E, 0x36, 0x0E, 0x06, 0x1E, 0x16, 0x6E, 0x66, 0x7E, 0x76, 0x4E, 0x46, 0x5E, 0x56,
            0xAD, 0xA5, 0xBD, 0xB5, 0x8D, 0x85, 0x9D, 0x95, 0xED, 0xE5, 0xFD, 0xF5, 0xCD, 0xC5, 0xDD, 0xD5,
            0x2D, 0x25, 0x3D, 0x35, 0x0D, 0x05, 0x1D, 0x15, 0x6D, 0x65, 0x7D, 0x75, 0x4D, 0x45, 0x5D, 0x55,
            0xAC, 0xA4, 0xBC, 0xB4, 0x8C, 0x84, 0x9C, 0x94, 0xEC, 0xE4, 0xFC, 0xF4, 0xCC, 0xC4, 0xDC, 0xD4,
            0x2C, 0x24, 0x3C, 0x34, 0x0C, 0x04, 0x1C, 0x14, 0x6C, 0x64, 0x7C, 0x74, 0x4C, 0x44, 0x5C, 0x54,
        },
        { /* S_3 */
            0x7C, 0x74, 0x6C, 0x64, 0x5C, 0x54, 0x4C, 0x44, 0x3C, 0x34, 0x2C, 0x24, 0x1C, 0x14, 0x0C, 0x04,
            0xFC, 0xF4, 0xEC, 0xE4, 0xDC, 0xD4, 0xCC, 0xC4, 0xBC, 0xB4, 0xAC, 0xA4, 0x9C, 0x94, 0x8C, 0x84,
            0x7D, 0x75, 0x6D, 0x65, 0x5D, 0x55, 0x4D, 0x45, 0x3D, 0x35, 0x2D, 0x25, 0x1D, 0x15, 0x0D, 0x05,
            0xFD, 0xF5, 0xED, 0xE5, 0xDD, 0xD5, 0xCD, 0xC5, 0xBD, 0xB5, 0xAD, 0xA5, 0x9D, 0x95, 0x8D, 0x85,
            0x7E, 0x76, 0x6E, 0x66, 0x5E, 0x56, 0x4E, 0x46, 0x3E, 0x36, 0x2E, 0x26, 0x1E, 0x16, 0x0E, 0x06,
            0xFE, 0xF6, 0xEE, 0xE6, 0xDE, 0xD6, 0xCE, 0xC6, 0xBE, 0xB6, 0xAE, 0xA6, 0x9E, 0x96, 0x8E, 0x86,
            0x7F, 0x77, 0x6F, 0x67, 0x5F, 0x57, 0x4F, 0x47, 0x3F, 0x37, 0x2F, 0x27, 0x1F, 0x17, 0x0F, 0x07,
            0xFF, 0xF7, 0xEF, 0xE7, 0xDF, 0xD7, 0xCF, 0xC7, 0xBF, 0xB7, 0xAF, 0xA7, 0x9F, 0x97, 0x8F, 0x87,
            0x78, 0x70, 0x68, 0x60, 0x58, 0x50, 0x48, 0x40, 0x38, 0x30, 0x28, 0x20, 0x18, 0x10, 0x08, 0x00,
            0xF8, 0xF0, 0xE8, 0xE0, 0xD8, 0xD0, 0xC8, 0xC0, 0xB8, 0xB0, 0xA8, 0xA0, 0x98, 0x90, 0x88, 0x80,
            0x79, 0x71, 0x69, 0x61, 0x59, 0x51, 0x49, 0x41, 0x39, 0x31, 0x29, 0x21, 0x19, 0x11, 0x09, 0x01,
            0xF9, 0xF1, 0xE9, 0xE1, 0xD9, 0xD1, 0xC9, 0xC1, 0xB9, 0xB1, 0xA9, 0xA1, 0x99, 0x91, 0x89, 0x81,
            0x7A, 0x72, 0x6A, 0x62, 0x5A, 0x52, 0x4A, 0x42, 0x3A, 0x32, 0x2A, 0x22, 0x1A, 0x12, 0x0A, 0x02,
            0xFA, 0xF2, 0xEA, 0xE2, 0xDA, 0xD2, 0xCA, 0xC2, 0xBA, 0xB2, 0xAA, 0xA2, 0x9A, 0x92, 0x8A, 0x82,
            0x7B, 0x73, 0x6B, 0x63, 0x5B, 0x53, 0x4B, 0x43, 0x3B, 0x33, 0x2B, 0x23, 0x1B, 0x13, 0x0B, 0x03,
            0xFB, 0xF3, 0xEB, 0xE3, 0xDB, 0xD3, 0xCB, 0xC3, 0xBB, 0xB3, 0xAB, 0xA3, 0x9B, 0x93, 0x8B, 0x83,
        },
        { /* S_4 */
            0xD8, 0xD0, 0xC8, 0xC0, 0xF8, 0xF0, 0xE8, 0xE0, 0x98, 0x90, 0x88, 0x80, 0xB8, 0xB0, 0xA8, 0xA0,
            0x58, 0x50, 0x48, 0x40, 0x78, 0x70, 0x68, 0x60, 0x18, 0x10, 0x08, 0x00, 0x38, 0x30, 0x28, 0x20,
            0xD9, 0xD1, 0xC9, 0xC1, 0xF9, 0xF1, 0xE9, 0xE1, 0x99, 0x91, 0x89, 0x81, 0xB9, 0xB1, 0xA9, 0xA1,
            0x59, 0x51, 0x49, 0x41, 0x79, 0x71, 0x69, 0x61, 0x19, 0x11, 0x09, 0x01, 0x39, 0x31, 0x29, 0x21,
            0xDA, 0xD2, 0xCA, 0xC2, 0xFA, 0xF2, 0xEA, 0xE2, 0x9A, 0x92, 0x8A, 0x82, 0xBA, 0xB2, 0xAA, 0xA2,
            0x5A, 0x52, 0x4A, 0x42, 0x7A, 0x72, 0x6A, 0x62, 0x1A, 0x12, 0x0A, 0x02, 0x3A, 0x32, 0x2A, 0x22,
            0xDB, 0xD3, 0xCB, 0xC3, 0xFB, 0xF3, 0xEB, 0xE3, 0x9B, 0x93, 0x8B, 0x83, 0xBB, 0xB3, 0xAB, 0xA3,
            0x5B, 0x53, 0x4B, 0x43, 0x7B, 0x73, 0x6B, 0x63, 0x1B, 0x13, 0x0B, 0x03, 0x3B, 0x33, 0x2B, 0x23,
            0xDC, 0xD4, 0xCC, 0xC4, 0xFC, 0xF4, 0xEC, 0xE4, 0x9C, 0x94, 0x8C, 0x84, 0xBC, 0xB4, 0xAC, 0xA4,
            0x5C, 0x54, 0x4C, 0x44, 0x7C, 0x74, 0x6C, 0x64, 0x1C, 0x14, 0x0C, 0x04, 0x3C, 0x34, 0x2C, 0x24,
            0xDD, 0xD5, 0xCD, 0xC5, 0xFD, 0xF5, 0xED, 0xE5, 0x9D, 0x95, 0x8D, 0x85, 0xBD, 0xB5, 0xAD, 0xA5,
            0x5D, 0x55, 0x4D, 0x45, 0x7D, 0x75, 0x6D, 0x65, 0x1D, 0x15, 0x0D, 0x05, 0x3D, 0x35, 0x2D, 0x25,
            0xDE, 0xD6, 0xCE, 0xC6, 0xFE, 0xF6, 0xEE, 0xE6, 0x9E, 0x96, 0x8E, 0x86, 0xBE, 0xB6, 0xAE, 0xA6,
            0x5E, 0x56, 0x4E, 0x46, 0x7E, 0x76, 0x6E, 0x66, 0x1E, 0x16, 0x0E, 0x06, 0x3E, 0x36, 0x2E, 0x26,
            0xDF, 0xD7, 0xCF, 0xC7, 0xFF, 0xF7, 0xEF, 0xE7, 0x9F, 0x97, 0x8F, 0x87, 0xBF, 0xB7, 0xAF, 0xA7,
            0x5F, 0x57, 0x4F, 0x47, 0x7F, 0x77, 0x6F, 0x67, 0x1F, 0x17, 0x0F, 0x07, 0x3F, 0x37, 0x2F, 0x27,
        },
        { /* S_5 */
            0x4C, 0xCC, 0x4D, 0xCD, 0x4E, 0xCE, 0x4F, 0xCF, 0x48, 0xC8, 0x49, 0xC9, 0x4A, 0xCA, 0x4B, 0xCB,
            0x44, 0xC4, 0x45, 0xC5, 0x46, 0xC6, 0x47, 0xC7, 0x40, 0xC0, 0x41, 0xC1, 0x42, 0xC2, 0x43, 0xC3,
            0x5C, 0xDC, 0x5D, 0xDD, 0x5E, 0xDE, 0x5F, 0xDF, 0x58, 0xD8, 0x59, 0xD9, 0x5A, 0xDA, 0x5B, 0xDB,
            0x54, 0xD4, 0x55, 0xD5, 0x56, 0xD6, 0x57, 0xD7, 0x50, 0xD0, 0x51, 0xD1, 0x52, 0xD2, 0x53, 0xD3,
            0x6C, 0xEC, 0x6D, 0xED, 0x6E, 0xEE, 0x6F, 0xEF, 0x68, 0xE8, 0x69, 0xE9, 0x6A, 0xEA, 0x6B, 0xEB,
            0x64, 0xE4, 0x65, 0xE5, 0x66, 0xE6, 0x67, 0xE7, 0x60, 0xE0, 0x61, 0xE1, 0x62, 0xE2, 0x63, 0xE3,
            0x7C, 0xFC, 0x7D, 0xFD, 0x7E, 0xFE, 0x7F, 0xFF, 0x78, 0xF8, 0x79, 0xF9, 0x7A, 0xFA, 0x7B, 0xFB,
            0x74, 0xF4, 0x75, 0xF5, 0x76, 0xF6, 0x77, 0xF7, 0x70, 0xF0, 0x71, 0xF1, 0x72, 0xF2, 0x73, 0xF3,
            0x0C, 0x8C, 0x0D, 0x8D, 0x0E, 0x8E, 0x0F, 0x8F, 0x08, 0x88, 0x09, 0x89, 0x0A, 0x8A, 0x0B, 0x8B,
            0x04, 0x84, 0x05, 0x85, 0x06, 0x86, 0x07, 0x87, 0x00, 0x80, 0x01, 0x81, 0x02, 0x82, 0x03, 0x83,
            0x1C, 0x9C, 0x1D, 0x9D, 0x1E, 0x9E, 0x1F, 0x9F, 0x18, 0x98, 0x19, 0x99, 0x1A, 0x9A, 0x1B, 0x9B,
            0x14, 0x94, 0x15, 0x95, 0x16, 0x96, 0x17, 0x97, 0x10, 0x90, 0x11, 0x91, 0x12, 0x92, 0x13, 0x93,
            0x2C, 0xAC, 0x2D, 0xAD, 0x2E, 0xAE, 0x2F, 0xAF, 0x28, 0xA8, 0x29, 0xA9, 0x2A, 0xAA, 0x2B, 0xAB,
            0x24, 0xA4, 0x25, 0xA5, 0x26, 0xA6, 0x27, 0xA7, 0x20, 0xA0, 0x21, 0xA1, 0x22, 0xA2, 0x23, 0xA3,
            0x3C, 0xBC, 0x3D, 0xBD, 0x3E, 0xBE, 0x3F, 0xBF, 0x38, 0xB8, 0x39, 0xB9, 0x3A, 0xBA, 0x3B, 0xBB,
            0x34, 0xB4, 0x35, 0xB5, 0x36, 0xB6, 0x37, 0xB7, 0x30, 0xB0, 0x31, 0xB1, 0x32, 0xB2, 0x33, 0xB3,
        },
        { /* S_6 */
            0x8B, 0x83, 0x9B, 0x93, 0xAB, 0xA3, 0xBB, 0xB3, 0xCB, 0xC3, 0xDB, 0xD3, 0xEB, 0xE3, 0xFB, 0xF3,
            0x0B, 0x03, 0x1B, 0x13, 0x2B, 0x23, 0x3B, 0x33, 0x4B, 0x43, 0x5B, 0x53, 0x6B, 0x63, 0x7B, 0x73,
            0x8A, 0x82, 0x9A, 0x92, 0xAA, 0xA2, 0xBA, 0xB2, 0xCA, 0xC2, 0xDA, 0xD2, 0xEA, 0xE2, 0xFA, 0xF2,
            0x0A, 0x02, 0x1A, 0x12, 0x2A, 0x22, 0x3A, 0x32, 0x4A, 0x42, 0x5A, 0x52, 0x6A, 0x62, 0x7A, 0x72,
            0x89, 0x81, 0x99, 0x91, 0xA9, 0xA1, 0xB9, 0xB1, 0xC9, 0xC1, 0xD9, 0xD1, 0xE9, 0xE1, 0xF9, 0xF1,
            0x09, 0x01, 0x19, 0x11, 0x29, 0x21, 0x39, 0x31, 0x49, 0x41, 0x59, 0x51, 0x69, 0x61, 0x79, 0x71,
            0x88, 0x80, 0x98, 0x90, 0xA8, 0xA0, 0xB8, 0xB0, 0xC8, 0xC0, 0xD8, 0xD0, 0xE8, 0xE0, 0xF8, 0xF0,
            0x08, 0x00, 0x18, 0x10, 0x28, 0x20, 0x38, 0x30, 0x48, 0x40, 0x58, 0x50, 0x68, 0x60, 0x78, 0x70,
            0x8F, 0x87, 0x9F, 0x97, 0xAF, 0xA7, 0xBF, 0xB7, 0xCF, 0xC7, 0xDF, 0xD7, 0xEF, 0xE7, 0xFF, 0xF7,
            0x0F, 0x07, 0x1F, 0x17, 0x2F, 0x27, 0x3F, 0x37, 0x4F, 0x47, 0x5F, 0x57, 0x6F, 0x67, 0x7F, 0x77,
            0x8E, 0x86, 0x9E, 0x96, 0xAE, 0xA6, 0xBE, 0xB6, 0xCE, 0xC6, 0xDE, 0xD6, 0xEE, 0xE6, 0xFE, 0xF6,
            0x0E, 0x06, 0x1E, 0x16, 0x2E, 0x26, 0x3E, 0x36, 0x4E, 0x46, 0x5E, 0x56, 0x6E, 0x66, 0x7E, 0x76,
            0x8D, 0x85, 0x9D, 0x95, 0xAD, 0xA5, 0xBD, 0xB5, 0xCD, 0xC5, 0xDD, 0xD5, 0xED, 0xE5, 0xFD, 0xF5,
            0x0D, 0x05, 0x1D, 0x15, 0x2D, 0x25, 0x3D, 0x35, 0x4D, 0x45, 0x5D, 0x55, 0x6D, 0x65, 0x7D, 0x75,
            0x8C, 0x84, 0x9C, 0x94, 0xAC, 0xA4, 0xBC, 0xB4, 0xCC, 0xC4, 0xDC, 0xD4, 0xEC, 0xE4, 0xFC, 0xF4,
            0x0C, 0x04, 0x1C, 0x14, 0x2C, 0x24, 0x3C, 0x34, 0x4C, 0x44, 0x5C, 0x54, 0x6C, 0x64, 0x7C, 0x74,
        },
        { /* S_7 */
            0x5C, 0x54, 0x4C, 0x44, 0x7C, 0x74, 0x6C, 0x64, 0x1C, 0x14, 0x0C, 0x04, 0x3C, 0x34, 0x2C, 0x24,
            0xDC, 0xD4, 0xCC, 0xC4, 0xFC, 0xF4, 0xEC, 0xE4, 0x9C, 0x94, 0x8C, 0x84, 0xBC, 0xB4, 0xAC, 0xA4,
            0x5D, 0x55, 0x4D, 0x45, 0x7D, 0x75, 0x6D, 0x65, 0x1D, 0x15, 0x0D, 0x05, 0x3D, 0x35, 0x2D, 0x25,
            0xDD, 0xD5, 0xCD, 0xC5, 0xFD, 0xF5, 0xED, 0xE5, 0x9D, 0x95, 0x8D, 0x85, 0xBD, 0xB5, 0xAD, 0xA5,
            0x5E, 0x56, 0x4E, 0x46, 0x7E, 0x76, 0x6E, 0x66, 0x1E, 0x16, 0x0E, 0x06, 0x3E, 0x36, 0x2E, 0x26,
            0xDE, 0xD6, 0xCE, 0xC6, 0xFE, 0xF6, 0xEE, 0xE6, 0x9E, 0x96, 0x8E, 0x86, 0xBE, 0xB6, 0xAE, 0xA6,
            0x5F, 0x57, 0x4F, 0x47, 0x7F, 0x77, 0x6F, 0x67, 0x1F, 0x17, 0x0F, 0x07, 0x3F, 0x37, 0x2F, 0x27,
            0xDF, 0xD7, 0xCF, 0xC7, 0xFF, 0xF7, 0xEF, 0xE7, 0x9F, 0x97, 0x8F, 0x87, 0xBF, 0xB7, 0xAF, 0xA7,
            0x58, 0x50, 0x48, 0x40, 0x78, 0x70, 0x68, 0x60, 0x18, 0x10, 0x08, 0x00, 0x38, 0x30, 0x28, 0x20,
            0xD8, 0xD0, 0xC8, 0xC0, 0xF8, 0xF0, 0xE8, 0xE0, 0x98, 0x90, 0x88, 0x80, 0xB8, 0xB0, 0xA8, 0xA0,
            0x59, 0x51, 0x49, 0x41, 0x79, 0x71, 0x69, 0x61, 0x19, 0x11, 0x09, 0x01, 0x39, 0x31, 0x29, 0x21,
            0xD9, 0xD1, 0xC9, 0xC1, 0xF9, 0xF1, 0xE9, 0xE1, 0x99, 0x91, 0x89, 0x81, 0xB9, 0xB1, 0xA9, 0xA1,
            0x5A, 0x52, 0x4A, 0x42, 0x7A, 0x72, 0x6A, 0x62, 0x1A, 0x12, 0x0A, 0x02, 0x3A, 0x32, 0x2A, 0x22,
            0xDA, 0xD2, 0xCA, 0xC2, 0xFA, 0xF2, 0xEA, 0xE2, 0x9A, 0x92, 0x8A, 0x82, 0xBA, 0xB2, 0xAA, 0xA2,
            0x5B, 0x53, 0x4B, 0x43, 0x7B, 0x73, 0x6B, 0x63, 0x1B, 0x13, 0x0B, 0x03, 0x3B, 0x33, 0x2B, 0x23,
            0xDB, 0xD3, 0xCB, 0xC3, 0xFB, 0xF3, 0xEB, 0xE3, 0x9B, 0x93, 0x8B, 0x83, 0xBB, 0xB3, 0xAB, 0xA3,
        },
    },
    { /* case 2: AA, constant 1206 (A3) */
        { /* S_0 */
            0x9B, 0xBB, 0xDB, 0xFB, 0x1B, 0x3B, 0x5B, 0x7B, 0x9A, 0xBA, 0xDA, 0xFA, 0x1A, 0x3A, 0x5A, 0x7A,
            0x99, 0xB9, 0xD9, 0xF9, 0x19, 0x39, 0x59, 0x79, 0x98, 0xB8, 0xD8, 0xF8, 0x18, 0x38, 0x58, 0x78,
            0x9F, 0xBF, 0xDF, 0xFF, 0x1F, 0x3F, 0x5F, 0x7F, 0x9E, 0xBE, 0xDE, 0xFE, 0x1E, 0x3E, 0x5E, 0x7E,
            0x9D, 0xBD, 0xDD, 0xFD, 0x1D, 0x3D, 0x5D, 0x7D, 0x9C, 0xBC, 0xDC, 0xFC, 0x1C, 0x3C, 0x5C, 0x7C,
            0x93, 0xB3, 0xD3, 0xF3, 0x13, 0x33, 0x53, 0x73, 0x92, 0xB2, 0xD2, 0xF2, 0x12, 0x32, 0x52, 0x72,
            0x91, 0xB1, 0xD1, 0xF1, 0x11, 0x31, 0x51, 0x71, 0x90, 0xB0, 0xD0, 0xF0, 0x10, 0x30, 0x50, 0x70,
            0x97, 0xB7, 0xD7, 0xF7, 0x17, 0x37, 0x57, 0x77, 0x96, 0xB6, 0xD6, 0xF6, 0x16, 0x36, 0x56, 0x76,
            0x95, 0xB5, 0xD5, 0xF5, 0x15, 0x35, 0x55, 0x75, 0x94, 0xB4, 0xD4, 0xF4, 0x14, 0x34, 0x54, 0x74,
            0x8B, 0xAB, 0xCB, 0xEB, 0x0B, 0x2B, 0x4B, 0x6B, 0x8A, 0xAA, 0xCA, 0xEA, 0x0A, 0x2A, 0x4A, 0x6A,
            0x89, 0xA9, 0xC9, 0xE9, 0x09, 0x29, 0x49, 0x69, 0x88, 0xA8, 0xC8, 0xE8, 0x08, 0x28, 0x48, 0x68,
            0x8F, 0xAF, 0xCF, 0xEF, 0x0F, 0x2F, 0x4F, 0x6F, 0x8E, 0xAE, 0xCE, 0xEE, 0x0E, 0x2E, 0x4E, 0x6E,
            0x8D, 0xAD, 0xCD, 0xED, 0x0D, 0x2D, 0x4D, 0x6D, 0x8C, 0xAC, 0xCC, 0xEC, 0x0C, 0x2C, 0x4C, 0x6C,
            0x83, 0xA3, 0xC3, 0xE3, 0x03, 0x23, 0x43, 0x63, 0x82, 0xA2, 0xC2, 0xE2, 0x02, 0x22, 0x42, 0x62,
            0x81, 0xA1, 0xC1, 0xE1, 0x01, 0x21, 0x41, 0x61, 0x80, 0xA0, 0xC0, 0xE0, 0x00, 0x20, 0x40, 0x60,
            0x87, 0xA7, 0xC7, 0xE7, 0x07, 0x27, 0x47, 0x67, 0x86, 0xA6, 0xC6, 0xE6, 0x06, 0x26, 0x46, 0x66,
            0x85, 0xA5, 0xC5, 0xE5, 0x05, 0x25, 0x45, 0x65, 0x84, 0xA4, 0xC4, 0xE4, 0x04, 0x24, 0x44, 0x64,
        },
        { /* S_1 */
            0xD9, 0xDB, 0xDD, 0xDF, 0xD1, 0xD3, 0xD5, 0xD7, 0xC9, 0xCB, 0xCD, 0xCF, 0xC1, 0xC3, 0xC5, 0xC7,
            0xF9, 0xFB, 0xFD, 0xFF, 0xF1, 0xF3, 0xF5, 0xF7, 0xE9, 0xEB, 0xED, 0xEF, 0xE1, 0xE3, 0xE5, 0xE7,
            0x99, 0x9B, 0x9D, 0x9F, 0x91, 0x93, 0x95, 0x97, 0x89, 0x8B, 0x8D, 0x8F, 0x81, 0x83, 0x85, 0x87,
            0xB9, 0xBB, 0xBD, 0xBF, 0xB1, 0xB3, 0xB5, 0xB7, 0xA9, 0xAB, 0xAD, 0xAF, 0xA1, 0xA3, 0xA5, 0xA7,
            0x59, 0x5B, 0x5D, 0x5F, 0x51, 0x53, 0x55, 0x57, 0x49, 0x4B, 0x4D, 0x4F, 0x41, 0x43, 0x45, 0x47,
            0x79, 0x7B, 0x7D, 0x7F, 0x71, 0x73, 0x75, 0x77, 0x69, 0x6B, 0x6D, 0x6F, 0x61, 0x63, 0x65, 0x67,
            0x19, 0x1B, 0x1D, 0x1F, 0x11, 0x13, 0x15, 0x17, 0x09, 0x0B, 0x0D, 0x0F, 0x01, 0x03, 0x05, 0x07,
            0x39, 0x3B, 0x3D, 0x3F, 0x31, 0x33, 0x35, 0x37, 0x29, 0x2B, 0x2D, 0x2F, 0x21, 0x23, 0x25, 0x27,
            0xD8, 0xDA, 0xDC, 0xDE, 0xD0, 0xD2, 0xD4, 0xD6, 0xC8, 0xCA, 0xCC, 0xCE, 0xC0, 0xC2, 0xC4, 0xC6,
            0xF8, 0xFA, 0xFC, 0xFE, 0xF0, 0xF2, 0xF4, 0xF6, 0xE8, 0xEA, 0xEC, 0xEE, 0xE0, 0xE2, 0xE4, 0xE6,
            0x98, 0x9A, 0x9C, 0x9E, 0x90, 0x92, 0x94, 0x96, 0x88, 0x8A, 0x8C, 0x8E, 0x80, 0x82, 0x84, 0x86,
            0xB8, 0xBA, 0xBC, 0xBE, 0xB0, 0xB2, 0xB4, 0xB6, 0xA8, 0xAA, 0xAC, 0xAE, 0xA0, 0xA2, 0xA4, 0xA6,
            0x58, 0x5A, 0x5C, 0x5E, 0x50, 0x52, 0x54, 0x56, 0x48, 0x4A, 0x4C, 0x4E, 0x40, 0x42, 0x44, 0x46,
            0x78, 0x7A, 0x7C, 0x7E, 0x70, 0x72, 0x74, 0x76, 0x68, 0x6A, 0x6C, 0x6E, 0x60, 0x62, 0x64, 0x66,
            0x18, 0x1A, 0x1C, 0x1E, 0x10, 0x12, 0x14, 0x16, 0x08, 0x0A, 0x0C, 0x0E, 0x00, 0x02, 0x04, 0x06,
            0x38, 0x3A, 0x3C, 0x3E, 0x30, 0x32, 0x34, 0x36, 0x28, 0x2A, 0x2C, 0x2E, 0x20, 0x22, 0x24, 0x26,
        },
        { /* S_2 */
            0x7D, 0x5D, 0x3D, 0x1D, 0xFD, 0xDD, 0xBD, 0x9D, 0x7C, 0x5C, 0x3C, 0x1C, 0xFC, 0xDC, 0xBC, 0x9C,
            0x7F, 0x5F, 0x3F, 0x1F, 0xFF, 0xDF, 0xBF, 0x9F, 0x7E, 0x5E, 0x3E, 0x1E, 0xFE, 0xDE, 0xBE, 0x9E,
            0x79, 0x59, 0x39, 0x19, 0xF9, 0xD9, 0xB9, 0x99, 0x78, 0x58, 0x38, 0x18, 0xF8, 0xD8, 0xB8, 0x98,
            0x7B, 0x5B, 0x3B, 0x1B, 0xFB, 0xDB, 0xBB, 0x9B, 0x7A, 0x5A, 0x3A, 0x1A, 0xFA, 0xDA, 0xBA, 0x9A,
            0x75, 0x55, 0x35, 0x15, 0xF5, 0xD5, 0xB5, 0x95, 0x74, 0x54, 0x34, 0x14, 0xF4, 0xD4, 0xB4, 0x94,
            0x77, 0x57, 0x37, 0x17, 0xF7, 0xD7, 0xB7, 0x97, 0x76, 0x56, 0x36, 0x16, 0xF6, 0xD6, 0xB6, 0x96,
            0x71, 0x51, 0x31, 0x11, 0xF1, 0xD1, 0xB1, 0x91, 0x70, 0x50, 0x30, 0x10, 0xF0, 0xD0, 0xB0, 0x90,
            0x73, 0x53, 0x33, 0x13, 0xF3, 0xD3, 0xB3, 0x93, 0x72, 0x52, 0x32, 0x12, 0xF2, 0xD2, 0xB2, 0x92,
            0x6D, 0x4D, 0x2D, 0x0D, 0xED, 0xCD, 0xAD, 0x8D, 0x6C, 0x4C, 0x2C, 0x0C, 0xEC, 0xCC, 0xAC, 0x8C,
            0x6F, 0x4F, 0x2F, 0x0F, 0xEF, 0xCF, 0xAF, 0x8F, 0x6E, 0x4E, 0x2E, 0x0E, 0xEE, 0xCE, 0xAE, 0x8E,
            0x69, 0x49, 0x29, 0x09, 0xE9, 0xC9, 0xA9, 0x89, 0x68, 0x48, 0x28, 0x08, 0xE8, 0xC8, 0xA8, 0x88,
            0x6B, 0x4B, 0x2B, 0x0B, 0xEB, 0xCB, 0xAB, 0x8B, 0x6A, 0x4A, 0x2A, 0x0A, 0xEA, 0xCA, 0xAA, 0x8A,
            0x65, 0x45, 0x25, 0x05, 0xE5, 0xC5, 0xA5, 0x85, 0x64, 0x44, 0x24, 0x04, 0xE4, 0xC4, 0xA4, 0x84,
            0x67, 0x47, 0x27, 0x07, 0xE7, 0xC7, 0xA7, 0x87, 0x66, 0x46, 0x26, 0x06, 0xE6, 0xC6, 0xA6, 0x86,
            0x61, 0x41, 0x21, 0x01, 0xE1, 0xC1, 0xA1, 0x81, 0x60, 0x40, 0x20, 0x00, 0xE0, 0xC0, 0xA0, 0x80,
            0x63, 0x43, 0x23, 0x03, 0xE3, 0xC3, 0xA3, 0x83, 0x62, 0x42, 0x22, 0x02, 0xE2, 0xC2, 0xA2, 0x82,
        },
        { /* S_3 */
            0x87, 0xA7, 0xC7, 0xE7, 0x07, 0x27, 0x47, 0x67, 0x86, 0xA6, 0xC6, 0xE6, 0x06, 0x26, 0x46, 0x66,
            0x85, 0xA5, 0xC5, 0xE5, 0x05, 0x25, 0x45, 0x65, 0x84, 0xA4, 0xC4, 0xE4, 0x04, 0x24, 0x44, 0x64,
            0x83, 0xA3, 0xC3, 0xE3, 0x03, 0x23, 0x43, 0x63, 0x82, 0xA2, 0xC2, 0xE2, 0x02, 0x22, 0x42, 0x62,
            0x81, 0xA1, 0xC1, 0xE1, 0x01, 0x21, 0x41, 0x61, 0x80, 0xA0, 0xC0, 0xE0, 0x00, 0x20, 0x40, 0x60,
            0x8F, 0xAF, 0xCF, 0xEF, 0x0F, 0x2F, 0x4F, 0x6F, 0x8E, 0xAE, 0xCE, 0xEE, 0x0E, 0x2E, 0x4E, 0x6E,
            0x8D, 0xAD, 0xCD, 0xED, 0x0D, 0x2D, 0x4D, 0x6D, 0x8C, 0xAC, 0xCC, 0xEC, 0x0C, 0x2C, 0x4C, 0x6C,
            0x8B, 0xAB, 0xCB, 0xEB, 0x0B, 0x2B, 0x4B, 0x6B, 0x8A, 0xAA, 0xCA, 0xEA, 0x0A, 0x2A, 0x4A, 0x6A,
            0x89, 0xA9, 0xC9, 0xE9, 0x09, 0x29, 0x49, 0x69, 0x88, 0xA8, 0xC8, 0xE8, 0x08, 0x28, 0x48, 0x68,
            0x97, 0xB7, 0xD7, 0xF7, 0x17, 0x37, 0x57, 0x77, 0x96, 0xB6, 0xD6, 0xF6, 0x16, 0x36, 0x56, 0x76,
            0x95, 0xB5, 0xD5, 0xF5, 0x15, 0x35, 0x55, 0x75, 0x94, 0xB4, 0xD4, 0xF4, 0x14, 0x34, 0x54, 0x74,
            0x93, 0xB3, 0xD3, 0xF3, 0x13, 0x33, 0x53, 0x73, 0x92, 0xB2, 0xD2, 0xF2, 0x12, 0x32, 0x52, 0x72,
            0x91, 0xB1, 0xD1, 0xF1, 0x11, 0x31, 0x51, 0x71, 0x90, 0xB0, 0xD0, 0xF0, 0x10, 0x30, 0x50, 0x70,
            0x9F, 0xBF, 0xDF, 0xFF, 0x1F, 0x3F, 0x5F, 0x7F, 0x9E, 0xBE, 0xDE, 0xFE, 0x1E, 0x3E, 0x5E, 0x7E,
            0x9D, 0xBD, 0xDD, 0xFD, 0x1D, 0x3D, 0x5D, 0x7D, 0x9C, 0xBC, 0xDC, 0xFC, 0x1C, 0x3C, 0x5C, 0x7C,
            0x9B, 0xBB, 0xDB, 0xFB, 0x1B, 0x3B, 0x5B, 0x7B, 0x9A, 0xBA, 0xDA, 0xFA, 0x1A, 0x3A, 0x5A, 0x7A,
            0x99, 0xB9, 0xD9, 0xF9, 0x19, 0x39, 0x59, 0x79, 0x98, 0xB8, 0xD8, 0xF8, 0x18, 0x38, 0x58, 0x78,
        },
        { /* S_4 */
            0x8B, 0xAB, 0xCB, 0xEB, 0x0B, 0x2B, 0x4B, 0x6B, 0x8A, 0xAA, 0xCA, 0xEA, 0x0A, 0x2A, 0x4A, 0x6A,
            0x89, 0xA9, 0xC9, 0xE9, 0x09, 0x29, 0x49, 0x69, 0x88, 0xA8, 0xC8, 0xE8, 0x08, 0x28, 0x48, 0x68,
            0x8F, 0xAF, 0xCF, 0xEF, 0x0F, 0x2F, 0x4F, 0x6F, 0x8E, 0xAE, 0xCE, 0xEE, 0x0E, 0x2E, 0x4E, 0x6E,
            0x8D, 0xAD, 0xCD, 0xED, 0x0D, 0x2D, 0x4D, 0x6D, 0x8C, 0xAC, 0xCC, 0xEC, 0x0C, 0x2C, 0x4C, 0x6C,
            0x83, 0xA3, 0xC3, 0xE3, 0x03, 0x23, 0x43, 0x63, 0x82, 0xA2, 0xC2, 0xE2, 0x02, 0x22, 0x42, 0x62,
            0x81, 0xA1, 0xC1, 0xE1, 0x01, 0x21, 0x41, 0x61, 0x80, 0xA0, 0xC0, 0xE0, 0x00, 0x20, 0x40, 0x60,
            0x87, 0xA7, 0xC7, 0xE7, 0x07, 0x27, 0x47, 0x67, 0x86, 0xA6, 0xC6, 0xE6, 0x06, 0x26, 0x46, 0x66,
            0x85, 0xA5, 0xC5, 0xE5, 0x05, 0x25, 0x45, 0x65, 0x84, 0xA4, 0xC4, 0xE4, 0x04, 0x24, 0x44, 0x64,
            0x9B, 0xBB, 0xDB, 0xFB, 0x1B, 0x3B, 0x5B, 0x7B, 0x9A, 0xBA, 0xDA, 0xFA, 0x1A, 0x3A, 0x5A, 0x7A,
            0x99, 0xB9, 0xD9, 0xF9, 0x19, 0x39, 0x59, 0x79, 0x98, 0xB8, 0xD8, 0xF8, 0x18, 0x38, 0x58, 0x78,
            0x9F, 0xBF, 0xDF, 0xFF, 0x1F, 0x3F, 0x5F, 0x7F, 0x9E, 0xBE, 0xDE, 0xFE, 0x1E, 0x3E, 0x5E, 0x7E,
            0x9D, 0xBD, 0xDD, 0xFD, 0x1D, 0x3D, 0x5D, 0x7D, 0x9C, 0xBC, 0xDC, 0xFC, 0x1C, 0x3C, 0x5C, 0x7C,
            0x93, 0xB3, 0xD3, 0xF3, 0x13, 0x33, 0x53, 0x73, 0x92, 0xB2, 0xD2, 0xF2, 0x12, 0x32, 0x52, 0x72,
            0x91, 0xB1, 0xD1, 0xF1, 0x11, 0x31, 0x51, 0x71, 0x90, 0xB0, 0xD0, 0xF0, 0x10, 0x30, 0x50, 0x70,
            0x97, 0xB7, 0xD7, 0xF7, 0x17, 0x37, 0x57, 0x77, 0x96, 0xB6, 0xD6, 0xF6, 0x16, 0x36, 0x56, 0x76,
            0x95, 0xB5, 0xD5, 0xF5, 0x15, 0x35, 0x55, 0x75, 0x94, 0xB4, 0xD4, 0xF4, 0x14, 0x34, 0x54, 0x74,
        },
        { /* S_5 */
            0x18, 0x1A, 0x1C, 0x1E, 0x10, 0x12, 0x14, 0x16, 0x08, 0x0A, 0x0C, 0x0E, 0x00, 0x02, 0x04, 0x06,
            0x38, 0x3A, 0x3C, 0x3E, 0x30, 0x32, 0x34, 0x36, 0x28, 0x2A, 0x2C, 0x2E, 0x20, 0x22, 0x24, 0x26,
            0x58, 0x5A, 0x5C, 0x5E, 0x50, 0x52, 0x54, 0x56, 0x48, 0x4A, 0x4C, 0x4E, 0x40, 0x42, 0x44, 0x46,
            0x78, 0x7A, 0x7C, 0x7E, 0x70, 0x72, 0x74, 0x76, 0x68, 0x6A, 0x6C, 0x6E, 0x60, 0x62, 0x64, 0x66,
            0x98, 0x9A, 0x9C, 0x9E, 0x90, 0x92, 0x94, 0x96, 0x88, 0x8A, 0x8C, 0x8E, 0x80, 0x82, 0x84, 0x86,
            0xB8, 0xBA, 0xBC, 0xBE, 0xB0, 0xB2, 0xB4, 0xB6, 0xA8, 0xAA, 0xAC, 0xAE, 0xA0, 0xA2, 0xA4, 0xA6,
            0xD8, 0xDA, 0xDC, 0xDE, 0xD0, 0xD2, 0xD4, 0xD6, 0xC8, 0xCA, 0xCC, 0xCE, 0xC0, 0xC2, 0xC4, 0xC6,
            0xF8, 0xFA, 0xFC, 0xFE, 0xF0, 0xF2, 0xF4, 0xF6, 0xE8, 0xEA, 0xEC, 0xEE, 0xE0, 0xE2, 0xE4, 0xE6,
            0x19, 0x1B, 0x1D, 0x1F, 0x11, 0x13, 0x15, 0x17, 0x09, 0x0B, 0x0D, 0x0F, 0x01, 0x03, 0x05, 0x07,
            0x39, 0x3B, 0x3D, 0x3F, 0x31, 0x33, 0x35, 0x37, 0x29, 0x2B, 0x2D, 0x2F, 0x21, 0x23, 0x25, 0x27,
            0x59, 0x5B, 0x5D, 0x5F, 0x51, 0x53, 0x55, 0x57, 0x49, 0x4B, 0x4D, 0x4F, 0x41, 0x43, 0x45, 0x47,
            0x79, 0x7B, 0x7D, 0x7F, 0x71, 0x73, 0x75, 0x77, 0x69, 0x6B, 0x6D, 0x6F, 0x61, 0x63, 0x65, 0x67,
            0x99, 0x9B, 0x9D, 0x9F, 0x91, 0x93, 0x95, 0x97, 0x89, 0x8B, 0x8D, 0x8F, 0x81, 0x83, 0x85, 0x87,
            0xB9, 0xBB, 0xBD, 0xBF, 0xB1, 0xB3, 0xB5, 0xB7, 0xA9, 0xAB, 0xAD, 0xAF, 0xA1, 0xA3, 0xA5, 0xA7,
            0xD9, 0xDB, 0xDD, 0xDF, 0xD1, 0xD3, 0xD5, 0xD7, 0xC9, 0xCB, 0xCD, 0xCF, 0xC1, 0xC3, 0xC5, 0xC7,
            0xF9, 0xFB, 0xFD, 0xFF, 0xF1, 0xF3, 0xF5, 0xF7, 0xE9, 0xEB, 0xED, 0xEF, 0xE1, 0xE3, 0xE5, 0xE7,
        },
        { /* S_6 */
            0x79, 0x59, 0x39, 0x19, 0xF9, 0xD9, 0xB9, 0x99, 0x78, 0x58, 0x38, 0x18, 0xF8, 0xD8, 0xB8, 0x98,
            0x7B, 0x5B, 0x3B, 0x1B, 0xFB, 0xDB, 0xBB, 0x9B, 0x7A, 0x5A, 0x3A, 0x1A, 0xFA, 0xDA, 0xBA, 0x9A,
            0x7D, 0x5D, 0x3D, 0x1D, 0xFD, 0xDD, 0xBD, 0x9D, 0x7C, 0x5C, 0x3C, 0x1C, 0xFC, 0xDC, 0xBC, 0x9C,
            0x7F, 0x5F, 0x3F, 0x1F, 0xFF, 0xDF, 0xBF, 0x9F, 0x7E, 0x5E, 0x3E, 0x1E, 0xFE, 0xDE, 0xBE, 0x9E,
            0x71, 0x51, 0x31, 0x11, 0xF1, 0xD1, 0xB1, 0x91, 0x70, 0x50, 0x30, 0x10, 0xF0, 0xD0, 0xB0, 0x90,
            0x73, 0x53, 0x33, 0x13, 0xF3, 0xD3, 0xB3, 0x93, 0x72, 0x52, 0x32, 0x12, 0xF2, 0xD2, 0xB2, 0x92,
            0x75, 0x55, 0x35, 0x15, 0xF5, 0xD5, 0xB5, 0x95, 0x74, 0x54, 0x34, 0x14, 0xF4, 0xD4, 0xB4, 0x94,
            0x77, 0x57, 0x37, 0x17, 0xF7, 0xD7, 0xB7, 0x97, 0x76, 0x56, 0x36, 0x16, 0xF6, 0xD6, 0xB6, 0x96,
            0x69, 0x49, 0x29, 0x09, 0xE9, 0xC9, 0xA9, 0x89, 0x68, 0x48, 0x28, 0x08, 0xE8, 0xC8, 0xA8, 0x88,
            0x6B, 0x4B, 0x2B, 0x0B, 0xEB, 0xCB, 0xAB, 0x8B, 0x6A, 0x4A, 0x2A, 0x0A, 0xEA, 0xCA, 0xAA, 0x8A,
            0x6D, 0x4D, 0x2D, 0x0D, 0xED, 0xCD, 0xAD, 0x8D, 0x6C, 0x4C, 0x2C, 0x0C, 0xEC, 0xCC, 0xAC, 0x8C,
            0x6F, 0x4F, 0x2F, 0x0F, 0xEF, 0xCF, 0xAF, 0x8F, 0x6E, 0x4E, 0x2E, 0x0E, 0xEE, 0xCE, 0xAE, 0x8E,
            0x61, 0x41, 0x21, 0x01, 0xE1, 0xC1, 0xA1, 0x81, 0x60, 0x40, 0x20, 0x00, 0xE0, 0xC0, 0xA0, 0x80,
            0x63, 0x43, 0x23, 0x03, 0xE3, 0xC3, 0xA3, 0x83, 0x62, 0x42, 0x22, 0x02, 0xE2, 0xC2, 0xA2, 0x82,
            0x65, 0x45, 0x25, 0x05, 0xE5, 0xC5, 0xA5, 0x85, 0x64, 0x44, 0x24, 0x04, 0xE4, 0xC4, 0xA4, 0x84,
            0x67, 0x47, 0x27, 0x07, 0xE7, 0xC7, 0xA7, 0x87, 0x66, 0x46, 0x26, 0x06, 0xE6, 0xC6, 0xA6, 0x86,
        },
        { /* S_7 */
            0x83, 0xA3, 0xC3, 0xE3, 0x03, 0x23, 0x43, 0x63, 0x82, 0xA2, 0xC2, 0xE2, 0x02, 0x22, 0x42, 0x62,
            0x81, 0xA1, 0xC1, 0xE1, 0x01, 0x21, 0x41, 0x61, 0x80, 0xA0, 0xC0, 0xE0, 0x00, 0x20, 0x40, 0x60,
            0x87, 0xA7, 0xC7, 0xE7, 0x07, 0x27, 0x47, 0x67, 0x86, 0xA6, 0xC6, 0xE6, 0x06, 0x26, 0x46, 0x66,
            0x85, 0xA5, 0xC5, 0xE5, 0x05, 0x25, 0x45, 0x65, 0x84, 0xA4, 0xC4, 0xE4, 0x04, 0x24, 0x44, 0x64,
            0x8B, 0xAB, 0xCB, 0xEB, 0x0B, 0x2B, 0x4B, 0x6B, 0x8A, 0xAA, 0xCA, 0xEA, 0x0A, 0x2A, 0x4A, 0x6A,
            0x89, 0xA9, 0xC9, 0xE9, 0x09, 0x29, 0x49, 0x69, 0x88, 0xA8, 0xC8, 0xE8, 0x08, 0x28, 0x48, 0x68,
            0x8F, 0xAF, 0xCF, 0xEF, 0x0F, 0x2F, 0x4F, 0x6F, 0x8E, 0xAE, 0xCE, 0xEE, 0x0E, 0x2E, 0x4E, 0x6E,
            0x8D, 0xAD, 0xCD, 0xED, 0x0D, 0x2D, 0x4D, 0x6D, 0x8C, 0xAC, 0xCC, 0xEC, 0x0C, 0x2C, 0x4C, 0x6C,
            0x93, 0xB3, 0xD3, 0xF3, 0x13, 0x33, 0x53, 0x73, 0x92, 0xB2, 0xD2, 0xF2, 0x12, 0x32, 0x52, 0x72,
            0x91, 0xB1, 0xD1, 0xF1, 0x11, 0x31, 0x51, 0x71, 0x90, 0xB0, 0xD0, 0xF0, 0x10, 0x30, 0x50, 0x70,
            0x97, 0xB7, 0xD7, 0xF7, 0x17, 0x37, 0x57, 0x77, 0x96, 0xB6, 0xD6, 0xF6, 0x16, 0x36, 0x56, 0x76,
            0x95, 0xB5, 0xD5, 0xF5, 0x15, 0x35, 0x55, 0x75, 0x94, 0xB4, 0xD4, 0xF4, 0x14, 0x34, 0x54, 0x74,
            0x9B, 0xBB, 0xDB, 0xFB, 0x1B, 0x3B, 0x5B, 0x7B, 0x9A, 0xBA, 0xDA, 0xFA, 0x1A, 0x3A, 0x5A, 0x7A,
            0x99, 0xB9, 0xD9, 0xF9, 0x19, 0x39, 0x59, 0x79, 0x98, 0xB8, 0xD8, 0xF8, 0x18, 0x38, 0x58, 0x78,
            0x9F, 0xBF, 0xDF, 0xFF, 0x1F, 0x3F, 0x5F, 0x7F, 0x9E, 0xBE, 0xDE, 0xFE, 0x1E, 0x3E, 0x5E, 0x7E,
            0x9D, 0xBD, 0xDD, 0xFD, 0x1D, 0x3D, 0x5D, 0x7D, 0x9C, 0xBC, 0xDC, 0xFC, 0x1C, 0x3C, 0x5C, 0x7C,
        },
    },
    { /* case 3: AB, constant 7343 (A4) */
        { /* S_0 */
            0xFF, 0x7F, 0xFE, 0x7E, 0xFD, 0x7D, 0xFC, 0x7C, 0xFB, 0x7B, 0xFA, 0x7A, 0xF9, 0x79, 0xF8, 0x78,
            0xF7, 0x77, 0xF6, 0x76, 0xF5, 0x75, 0xF4, 0x74, 0xF3, 0x73, 0xF2, 0x72, 0xF1, 0x71, 0xF0, 0x70,
            0xEF, 0x6F, 0xEE, 0x6E, 0xED, 0x6D, 0xEC, 0x6C, 0xEB, 0x6B, 0xEA, 0x6A, 0xE9, 0x69, 0xE8, 0x68,
            0xE7, 0x67, 0xE6, 0x66, 0xE5, 0x65, 0xE4, 0x64, 0xE3, 0x63, 0xE2, 0x62, 0xE1, 0x61, 0xE0, 0x60,
            0xDF, 0x5F, 0xDE, 0x5E, 0xDD, 0x5D, 0xDC, 0x5C, 0xDB, 0x5B, 0xDA, 0x5A, 0xD9, 0x59, 0xD8, 0x58,
            0xD7, 0x57, 0xD6, 0x56, 0xD5, 0x55, 0xD4, 0x54, 0xD3, 0x53, 0xD2, 0x52, 0xD1, 0x51, 0xD0, 0x50,
            0xCF, 0x4F, 0xCE, 0x4E, 0xCD, 0x4D, 0xCC, 0x4C, 0xCB, 0x4B, 0xCA, 0x4A, 0xC9, 0x49, 0xC8, 0x48,
            0xC7, 0x47, 0xC6, 0x46, 0xC5, 0x45, 0xC4, 0x44, 0xC3, 0x43, 0xC2, 0x42, 0xC1, 0x41, 0xC0, 0x40,
            0xBF, 0x3F, 0xBE, 0x3E, 0xBD, 0x3D, 0xBC, 0x3C, 0xBB, 0x3B, 0xBA, 0x3A, 0xB9, 0x39, 0xB8, 0x38,
            0xB7, 0x37, 0xB6, 0x36, 0xB5, 0x35, 0xB4, 0x34, 0xB3, 0x33, 0xB2, 0x32, 0xB1, 0x31, 0xB0, 0x30,
            0xAF, 0x2F, 0xAE, 0x2E, 0xAD, 0x2D, 0xAC, 0x2C, 0xAB, 0x2B, 0xAA, 0x2A, 0xA9, 0x29, 0xA8, 0x28,
            0xA7, 0x27, 0xA6, 0x26, 0xA5, 0x25, 0xA4, 0x24, 0xA3, 0x23, 0xA2, 0x22, 0xA1, 0x21, 0xA0, 0x20,
            0x9F, 0x1F, 0x9E, 0x1E, 0x9D, 0x1D, 0x9C, 0x1C, 0x9B, 0x1B, 0x9A, 0x1A, 0x99, 0x19, 0x98, 0x18,
            0x97, 0x17, 0x96, 0x16, 0x95, 0x15, 0x94, 0x14, 0x93, 0x13, 0x92, 0x12, 0x91, 0x11, 0x90, 0x10,
            0x8F, 0x0F, 0x8E, 0x0E, 0x8D, 0x0D, 0x8C, 0x0C, 0x8B, 0x0B, 0x8A, 0x0A, 0x89, 0x09, 0x88, 0x08,
            0x87, 0x07, 0x86, 0x06, 0x85, 0x05, 0x84, 0x04, 0x83, 0x03, 0x82, 0x02, 0x81, 0x01, 0x80, 0x00,
        },
        { /* S_1 */
            0x18, 0x10, 0x08, 0x00, 0x38, 0x30, 0x28, 0x20, 0x58, 0x50, 0x48, 0x40, 0x78, 0x70, 0x68, 0x60,
            0x98, 0x90, 0x88, 0x80, 0xB8, 0xB0, 0xA8, 0xA0, 0xD8, 0xD0, 0xC8, 0xC0, 0xF8, 0xF0, 0xE8, 0xE0,
            0x19, 0x11, 0x09, 0x01, 0x39, 0x31, 0x29, 0x21, 0x59, 0x51, 0x49, 0x41, 0x79, 0x71, 0x69, 0x61,
            0x99, 0x91, 0x89, 0x81, 0xB9, 0xB1, 0xA9, 0xA1, 0xD9, 0xD1, 0xC9, 0xC1, 0xF9, 0xF1, 0xE9, 0xE1,
            0x1A, 0x12, 0x0A, 0x02, 0x3A, 0x32, 0x2A, 0x22, 0x5A, 0x52, 0x4A, 0x42, 0x7A, 0x72, 0x6A, 0x62,
            0x9A, 0x92, 0x8A, 0x82, 0xBA, 0xB2, 0xAA, 0xA2, 0xDA, 0xD2, 0xCA, 0xC2, 0xFA, 0xF2, 0xEA, 0xE2,
            0x1B, 0x13, 0x0B, 0x03, 0x3B, 0x33, 0x2B, 0x23, 0x5B, 0x53, 0x4B, 0x43, 0x7B, 0x73, 0x6B, 0x63,
            0x9B, 0x93, 0x8B, 0x83, 0xBB, 0xB3, 0xAB, 0xA3, 0xDB, 0xD3, 0xCB, 0xC3, 0xFB, 0xF3, 0xEB, 0xE3,
            0x1C, 0x14, 0x0C, 0x04, 0x3C, 0x34, 0x2C, 0x24, 0x5C, 0x54, 0x4C, 0x44, 0x7C, 0x74, 0x6C, 0x64,
            0x9C, 0x94, 0x8C, 0x84, 0xBC, 0xB4, 0xAC, 0xA4, 0xDC, 0xD4, 0xCC, 0xC4, 0xFC, 0xF4, 0xEC, 0xE4,
            0x1D, 0x15, 0x0D, 0x05, 0x3D, 0x35, 0x2D, 0x25, 0x5D, 0x55, 0x4D, 0x45, 0x7D, 0x75, 0x6D, 0x65,
            0x9D, 0x95, 0x8D, 0x85, 0xBD, 0xB5, 0xAD, 0xA5, 0xDD, 0xD5, 0xCD, 0xC5, 0xFD, 0xF5, 0xED, 0xE5,
            0x1E, 0x16, 0x0E, 0x06, 0x3E, 0x36, 0x2E, 0x26, 0x5E, 0x56, 0x4E, 0x46, 0x7E, 0x76, 0x6E, 0x66,
            0x9E, 0x96, 0x8E, 0x86, 0xBE, 0xB6, 0xAE, 0xA6, 0xDE, 0xD6, 0xCE, 0xC6, 0xFE, 0xF6, 0xEE, 0xE6,
            0x1F, 0x17, 0x0F, 0x07, 0x3F, 0x37, 0x2F, 0x27, 0x5F, 0x57, 0x4F, 0x47, 0x7F, 0x77, 0x6F, 0x67,
            0x9F, 0x97, 0x8F, 0x87, 0xBF, 0xB7, 0xAF, 0xA7, 0xDF, 0xD7, 0xCF, 0xC7, 0xFF, 0xF7, 0xEF, 0xE7,
        },
        { /* S_2 */
            0xCF, 0xC7, 0xDF, 0xD7, 0xEF, 0xE7, 0xFF, 0xF7, 0x8F, 0x87, 0x9F, 0x97, 0xAF, 0xA7, 0xBF, 0xB7,
            0x4F, 0x47, 0x5F, 0x57, 0x6F, 0x67, 0x7F, 0x77, 0x0F, 0x07, 0x1F, 0x17, 0x2F, 0x27, 0x3F, 0x37,
            0xCE, 0xC6, 0xDE, 0xD6, 0xEE, 0xE6, 0xFE, 0xF6, 0x8E, 0x86, 0x9E, 0x96, 0xAE, 0xA6, 0xBE, 0xB6,
            0x4E, 0x46, 0x5E, 0x56, 0x6E, 0x66, 0x7E, 0x76, 0x0E, 0x06, 0x1E, 0x16, 0x2E, 0x26, 0x3E, 0x36,
            0xCD, 0xC5, 0xDD, 0xD5, 0xED, 0xE5, 0xFD, 0xF5, 0x8D, 0x85, 0x9D, 0x95, 0xAD, 0xA5, 0xBD, 0xB5,
            0x4D, 0x45, 0x5D, 0x55, 0x6D, 0x65, 0x7D, 0x75, 0x0D, 0x05, 0x1D, 0x15, 0x2D, 0x25, 0x3D, 0x35,
            0xCC, 0xC4, 0xDC, 0xD4, 0xEC, 0xE4, 0xFC, 0xF4, 0x8C, 0x84, 0x9C, 0x94, 0xAC, 0xA4, 0xBC, 0xB4,
            0x4C, 0x44, 0x5C, 0x54, 0x6C, 0x64, 0x7C, 0x74, 0x0C, 0x04, 0x1C, 0x14, 0x2C, 0x24, 0x3C, 0x34,
            0xCB, 0xC3, 0xDB, 0xD3, 0xEB, 0xE3, 0xFB, 0xF3, 0x8B, 0x83, 0x9B, 0x93, 0xAB, 0xA3, 0xBB, 0xB3,
            0x4B, 0x43, 0x5B, 0x53, 0x6B, 0x63, 0x7B, 0x73, 0x0B, 0x03, 0x1B, 0x13, 0x2B, 0x23, 0x3B, 0x33,
            0xCA, 0xC2, 0xDA, 0xD2, 0xEA, 0xE2, 0xFA, 0xF2, 0x8A, 0x82, 0x9A, 0x92, 0xAA, 0xA2, 0xBA, 0xB2,
            0x4A, 0x42, 0x5A, 0x52, 0x6A, 0x62, 0x7A, 0x72, 0x0A, 0x02, 0x1A, 0x12, 0x2A, 0x22, 0x3A, 0x32,
            0xC9, 0xC1, 0xD9, 0xD1, 0xE9, 0xE1, 0xF9, 0xF1, 0x89, 0x81, 0x99, 0x91, 0xA9, 0xA1, 0xB9, 0xB1,
            0x49, 0x41, 0x59, 0x51, 0x69, 0x61, 0x79, 0x71, 0x09, 0x01, 0x19, 0x11, 0x29, 0x21, 0x39, 0x31,
            0xC8, 0xC0, 0xD8, 0xD0, 0xE8, 0xE0, 0xF8, 0xF0, 0x88, 0x80, 0x98, 0x90, 0xA8, 0xA0, 0xB8, 0xB0,
            0x48, 0x40, 0x58, 0x50, 0x68, 0x60, 0x78, 0x70, 0x08, 0x00, 0x18, 0x10, 0x28, 0x20, 0x38, 0x30,
        },
        { /* S_3 */
            0x2D, 0x25, 0x3D, 0x35, 0x0D, 0x05, 0x1D, 0x15, 0x6D, 0x65, 0x7D, 0x75, 0x4D, 0x45, 0x5D, 0x55,
            0xAD, 0xA5, 0xBD, 0xB5, 0x8D, 0x85, 0x9D, 0x95, 0xED, 0xE5, 0xFD, 0xF5, 0xCD, 0xC5, 0xDD, 0xD5,
            0x2C, 0x24, 0x3C, 0x34, 0x0C, 0x04, 0x1C, 0x14, 0x6C, 0x64, 0x7C, 0x74, 0x4C, 0x44, 0x5C, 0x54,
            0xAC, 0xA4, 0xBC, 0xB4, 0x8C, 0x84, 0x9C, 0x94, 0xEC, 0xE4, 0xFC, 0xF4, 0xCC, 0xC4, 0xDC, 0xD4,
            0x2F, 0x27, 0x3F, 0x37, 0x0F, 0x07, 0x1F, 0x17, 0x6F, 0x67, 0x7F, 0x77, 0x4F, 0x47, 0x5F, 0x57,
            0xAF, 0xA7, 0xBF, 0xB7, 0x8F, 0x87, 0x9F, 0x97, 0xEF, 0xE7, 0xFF, 0xF7, 0xCF, 0xC7, 0xDF, 0xD7,
            0x2E, 0x26, 0x3E, 0x36, 0x0E, 0x06, 0x1E, 0x16, 0x6E, 0x66, 0x7E, 0x76, 0x4E, 0x46, 0x5E, 0x56,
            0xAE, 0xA6, 0xBE, 0xB6, 0x8E, 0x86, 0x9E, 0x96, 0xEE, 0xE6, 0xFE, 0xF6, 0xCE, 0xC6, 0xDE, 0xD6,
            0x29, 0x21, 0x39, 0x31, 0x09, 0x01, 0x19, 0x11, 0x69, 0x61, 0x79, 0x71, 0x49, 0x41, 0x59, 0x51,
            0xA9, 0xA1, 0xB9, 0xB1, 0x89, 0x81, 0x99, 0x91, 0xE9, 0xE1, 0xF9, 0xF1, 0xC9, 0xC1, 0xD9, 0xD1,
            0x28, 0x20, 0x38, 0x30, 0x08, 0x00, 0x18, 0x10, 0x68, 0x60, 0x78, 0x70, 0x48, 0x40, 0x58, 0x50,
            0xA8, 0xA0, 0xB8, 0xB0, 0x88, 0x80, 0x98, 0x90, 0xE8, 0xE0, 0xF8, 0xF0, 0xC8, 0xC0, 0xD8, 0xD0,
            0x2B, 0x23, 0x3B, 0x33, 0x0B, 0x03, 0x1B, 0x13, 0x6B, 0x63, 0x7B, 0x73, 0x4B, 0x43, 0x5B, 0x53,
            0xAB, 0xA3, 0xBB, 0xB3, 0x8B, 0x83, 0x9B, 0x93, 0xEB, 0xE3, 0xFB, 0xF3, 0xCB, 0xC3, 0xDB, 0xD3,
            0x2A, 0x22, 0x3A, 0x32, 0x0A, 0x02, 0x1A, 0x12, 0x6A, 0x62, 0x7A, 0x72, 0x4A, 0x42, 0x5A, 0x52,
            0xAA, 0xA2, 0xBA, 0xB2, 0x8A, 0x82, 0x9A, 0x92, 0xEA, 0xE2, 0xFA, 0xF2, 0xCA, 0xC2, 0xDA, 0xD2,
        },
        { /* S_4 */
            0x1F, 0x9F, 0x1E, 0x9E, 0x1D, 0x9D, 0x1C, 0x9C, 0x1B, 0x9B, 0x1A, 0x9A, 0x19, 0x99, 0x18, 0x98,
            0x17, 0x97, 0x16, 0x96, 0x15, 0x95, 0x14, 0x94, 0x13, 0x93, 0x12, 0x92, 0x11, 0x91, 0x10, 0x90,
            0x0F, 0x8F, 0x0E, 0x8E, 0x0D, 0x8D, 0x0C, 0x8C, 0x0B, 0x8B, 0x0A, 0x8A, 0x09, 0x89, 0x08, 0x88,
            0x07, 0x87, 0x06, 0x86, 0x05, 0x85, 0x04, 0x84, 0x03, 0x83, 0x02, 0x82, 0x01, 0x81, 0x00, 0x80,
            0x3F, 0xBF, 0x3E, 0xBE, 0x3D, 0xBD, 0x3C, 0xBC, 0x3B, 0xBB, 0x3A, 0xBA, 0x39, 0xB9, 0x38, 0xB8,
            0x37, 0xB7, 0x36, 0xB6, 0x35, 0xB5, 0x34, 0xB4, 0x33, 0xB3, 0x32, 0xB2, 0x31, 0xB1, 0x30, 0xB0,
            0x2F, 0xAF, 0x2E, 0xAE, 0x2D, 0xAD, 0x2C, 0xAC, 0x2B, 0xAB, 0x2A, 0xAA, 0x29, 0xA9, 0x28, 0xA8,
            0x27, 0xA7, 0x26, 0xA6, 0x25, 0xA5, 0x24, 0xA4, 0x23, 0xA3, 0x22, 0xA2, 0x21, 0xA1, 0x20, 0xA0,
            0x5F, 0xDF, 0x5E, 0xDE, 0x5D, 0xDD, 0x5C, 0xDC, 0x5B, 0xDB, 0x5A, 0xDA, 0x59, 0xD9, 0x58, 0xD8,
            0x57, 0xD7, 0x56, 0xD6, 0x55, 0xD5, 0x54, 0xD4, 0x53, 0xD3, 0x52, 0xD2, 0x51, 0xD1, 0x50, 0xD0,
            0x4F, 0xCF, 0x4E, 0xCE, 0x4D, 0xCD, 0x4C, 0xCC, 0x4B, 0xCB, 0x4A, 0xCA, 0x49, 0xC9, 0x48, 0xC8,
            0x47, 0xC7, 0x46, 0xC6, 0x45, 0xC5, 0x44, 0xC4, 0x43, 0xC3, 0x42, 0xC2, 0x41, 0xC1, 0x40, 0xC0,
            0x7F, 0xFF, 0x7E, 0xFE, 0x7D, 0xFD, 0x7C, 0xFC, 0x7B, 0xFB, 0x7A, 0xFA, 0x79, 0xF9, 0x78, 0xF8,
            0x77, 0xF7, 0x76, 0xF6, 0x75, 0xF5, 0x74, 0xF4, 0x73, 0xF3, 0x72, 0xF2, 0x71, 0xF1, 0x70, 0xF0,
            0x6F, 0xEF, 0x6E, 0xEE, 0x6D, 0xED, 0x6C, 0xEC, 0x6B, 0xEB, 0x6A, 0xEA, 0x69, 0xE9, 0x68, 0xE8,
            0x67, 0xE7, 0x66, 0xE6, 0x65, 0xE5, 0x64, 0xE4, 0x63, 0xE3, 0x62, 0xE2, 0x61, 0xE1, 0x60, 0xE0,
        },
        { /* S_5 */
            0xF8, 0xF0, 0xE8, 0xE0, 0xD8, 0xD0, 0xC8, 0xC0, 0xB8, 0xB0, 0xA8, 0xA0, 0x98, 0x90, 0x88, 0x80,
            0x78, 0x70, 0x68, 0x60, 0x58, 0x50, 0x48, 0x40, 0x38, 0x30, 0x28, 0x20, 0x18, 0x10, 0x08, 0x00,
            0xF9, 0xF1, 0xE9, 0xE1, 0xD9, 0xD1, 0xC9, 0xC1, 0xB9, 0xB1, 0xA9, 0xA1, 0x99, 0x91, 0x89, 0x81,
            0x79, 0x71, 0x69, 0x61, 0x59, 0x51, 0x49, 0x41, 0x39, 0x31, 0x29, 0x21, 0x19, 0x11, 0x09, 0x01,
            0xFA, 0xF2, 0xEA, 0xE2, 0xDA, 0xD2, 0xCA, 0xC2, 0xBA, 0xB2, 0xAA, 0xA2, 0x9A, 0x92, 0x8A, 0x82,
            0x7A, 0x72, 0x6A, 0x62, 0x5A, 0x52, 0x4A, 0x42, 0x3A, 0x32, 0x2A, 0x22, 0x1A, 0x12, 0x0A, 0x02,
            0xFB, 0xF3, 0xEB, 0xE3, 0xDB, 0xD3, 0xCB, 0xC3, 0xBB, 0xB3, 0xAB, 0xA3, 0x9B, 0x93, 0x8B, 0x83,
            0x7B, 0x73, 0x6B, 0x63, 0x5B, 0x53, 0x4B, 0x43, 0x3B, 0x33, 0x2B, 0x23, 0x1B, 0x13, 0x0B, 0x03,
            0xFC, 0xF4, 0xEC, 0xE4, 0xDC, 0xD4, 0xCC, 0xC4, 0xBC, 0xB4, 0xAC, 0xA4, 0x9C, 0x94, 0x8C, 0x84,
            0x7C, 0x74, 0x6C, 0x64, 0x5C, 0x54, 0x4C, 0x44, 0x3C, 0x34, 0x2C, 0x24, 0x1C, 0x14, 0x0C, 0x04,
            0xFD, 0xF5, 0xED, 0xE5, 0xDD, 0xD5, 0xCD, 0xC5, 0xBD, 0xB5, 0xAD, 0xA5, 0x9D, 0x95, 0x8D, 0x85,
            0x7D, 0x75, 0x6D, 0x65, 0x5D, 0x55, 0x4D, 0x45, 0x3D, 0x35, 0x2D, 0x25, 0x1D, 0x15, 0x0D, 0x05,
            0xFE, 0xF6, 0xEE, 0xE6, 0xDE, 0xD6, 0xCE, 0xC6, 0xBE, 0xB6, 0xAE, 0xA6, 0x9E, 0x96, 0x8E, 0x86,
            0x7E, 0x76, 0x6E, 0x66, 0x5E, 0x56, 0x4E, 0x46, 0x3E, 0x36, 0x2E, 0x26, 0x1E, 0x16, 0x0E, 0x06,
            0xFF, 0xF7, 0xEF, 0xE7, 0xDF, 0xD7, 0xCF, 0xC7, 0xBF, 0xB7, 0xAF, 0xA7, 0x9F, 0x97, 0x8F, 0x87,
            0x7F, 0x77, 0x6F, 0x67, 0x5F, 0x57, 0x4F, 0x47, 0x3F, 0x37, 0x2F, 0x27, 0x1F, 0x17, 0x0F, 0x07,
        },
        { /* S_6 */
            0x2F, 0x27, 0x3F, 0x37, 0x0F, 0x07, 0x1F, 0x17, 0x6F, 0x67, 0x7F, 0x77, 0x4F, 0x47, 0x5F, 0x57,
            0xAF, 0xA7, 0xBF, 0xB7, 0x8F, 0x87, 0x9F, 0x97, 0xEF, 0xE7, 0xFF, 0xF7, 0xCF, 0xC7, 0xDF, 0xD7,
            0x2E, 0x26, 0x3E, 0x36, 0x0E, 0x06, 0x1E, 0x16, 0x6E, 0x66, 0x7E, 0x76, 0x4E, 0x46, 0x5E, 0x56,
            0xAE, 0xA6, 0xBE, 0xB6, 0x8E, 0x86, 0x9E, 0x96, 0xEE, 0xE6, 0xFE, 0xF6, 0xCE, 0xC6, 0xDE, 0xD6,
            0x2D, 0x25, 0x3D, 0x35, 0x0D, 0x05, 0x1D, 0x15, 0x6D, 0x65, 0x7D, 0x75, 0x4D, 0x45, 0x5D, 0x55,
            0xAD, 0xA5, 0xBD, 0xB5, 0x8D, 0x85, 0x9D, 0x95, 0xED, 0xE5, 0xFD, 0xF5, 0xCD, 0xC5, 0xDD, 0xD5,
            0x2C, 0x24, 0x3C, 0x34, 0x0C, 0x04, 0x1C, 0x14, 0x6C, 0x64, 0x7C, 0x74, 0x4C, 0x44, 0x5C, 0x54,
            0xAC, 0xA4, 0xBC, 0xB4, 0x8C, 0x84, 0x9C, 0x94, 0xEC, 0xE4, 0xFC, 0xF4, 0xCC, 0xC4, 0xDC, 0xD4,
            0x2B, 0x23, 0x3B, 0x33, 0x0B, 0x03, 0x1B, 0x13, 0x6B, 0x63, 0x7B, 0x73, 0x4B, 0x43, 0x5B, 0x53,
            0xAB, 0xA3, 0xBB, 0xB3, 0x8B, 0x83, 0x9B, 0x93, 0xEB, 0xE3, 0xFB, 0xF3, 0xCB, 0xC3, 0xDB, 0xD3,
            0x2A, 0x22, 0x3A, 0x32, 0x0A, 0x02, 0x1A, 0x12, 0x6A, 0x62, 0x7A, 0x72, 0x4A, 0x42, 0x5A, 0x52,
            0xAA, 0xA2, 0xBA, 0xB2, 0x8A, 0x82, 0x9A, 0x92, 0xEA, 0xE2, 0xFA, 0xF2, 0xCA, 0xC2, 0xDA, 0xD2,
            0x29, 0x21, 0x39, 0x31, 0x09, 0x01, 0x19, 0x11, 0x69, 0x61, 0x79, 0x71, 0x49, 0x41, 0x59, 0x51,
            0xA9, 0xA1, 0xB9, 0xB1, 0x89, 0x81, 0x99, 0x91, 0xE9, 0xE1, 0xF9, 0xF1, 0xC9, 0xC1, 0xD9, 0xD1,
            0x28, 0x20, 0x38, 0x30, 0x08, 0x00, 0x18, 0x10, 0x68, 0x60, 0x78, 0x70, 0x48, 0x40, 0x58, 0x50,
            0xA8, 0xA0, 0xB8, 0xB0, 0x88, 0x80, 0x98, 0x90, 0xE8, 0xE0, 0xF8, 0xF0, 0xC8, 0xC0, 0xD8, 0xD0,
        },
        { /* S_7 */
            0x6D, 0x65, 0x7D, 0x75, 0x4D, 0x45, 0x5D, 0x55, 0x2D, 0x25, 0x3D, 0x35, 0x0D, 0x05, 0x1D, 0x15,
            0xED, 0xE5, 0xFD, 0xF5, 0xCD, 0xC5, 0xDD, 0xD5, 0xAD, 0xA5, 0xBD, 0xB5, 0x8D, 0x85, 0x9D, 0x95,
            0x6C, 0x64, 0x7C, 0x74, 0x4C, 0x44, 0x5C, 0x54, 0x2C, 0x24, 0x3C, 0x34, 0x0C, 0x04, 0x1C, 0x14,
            0xEC, 0xE4, 0xFC, 0xF4, 0xCC, 0xC4, 0xDC, 0xD4, 0xAC, 0xA4, 0xBC, 0xB4, 0x8C, 0x84, 0x9C, 0x94,
            0x6F, 0x67, 0x7F, 0x77, 0x4F, 0x47, 0x5F, 0x57, 0x2F, 0x27, 0x3F, 0x37, 0x0F, 0x07, 0x1F, 0x17,
            0xEF, 0xE7, 0xFF, 0xF7, 0xCF, 0xC7, 0xDF, 0xD7, 0xAF, 0xA7, 0xBF, 0xB7, 0x8F, 0x87, 0x9F, 0x97,
            0x6E, 0x66, 0x7E, 0x76, 0x4E, 0x46, 0x5E, 0x56, 0x2E, 0x26, 0x3E, 0x36, 0x0E, 0x06, 0x1E, 0x16,
            0xEE, 0xE6, 0xFE, 0xF6, 0xCE, 0xC6, 0xDE, 0xD6, 0xAE, 0xA6, 0xBE, 0xB6, 0x8E, 0x86, 0x9E, 0x96,
            0x69, 0x61, 0x79, 0x71, 0x49, 0x41, 0x59, 0x51, 0x29, 0x21, 0x39, 0x31, 0x09, 0x01, 0x19, 0x11,
            0xE9, 0xE1, 0xF9, 0xF1, 0xC9, 0xC1, 0xD9, 0xD1, 0xA9, 0xA1, 0xB9, 0xB1, 0x89, 0x81, 0x99, 0x91,
            0x68, 0x60, 0x78, 0x70, 0x48, 0x40, 0x58, 0x50, 0x28, 0x20, 0x38, 0x30, 0x08, 0x00, 0x18, 0x10,
            0xE8, 0xE0, 0xF8, 0xF0, 0xC8, 0xC0, 0xD8, 0xD0, 0xA8, 0xA0, 0xB8, 0xB0, 0x88, 0x80, 0x98, 0x90,
            0x6B, 0x63, 0x7B, 0x73, 0x4B, 0x43, 0x5B, 0x53, 0x2B, 0x23, 0x3B, 0x33, 0x0B, 0x03, 0x1B, 0x13,
            0xEB, 0xE3, 0xFB, 0xF3, 0xCB, 0xC3, 0xDB, 0xD3, 0xAB, 0xA3, 0xBB, 0xB3, 0x8B, 0x83, 0x9B, 0x93,
            0x6A, 0x62, 0x7A, 0x72, 0x4A, 0x42, 0x5A, 0x52, 0x2A, 0x22, 0x3A, 0x32, 0x0A, 0x02, 0x1A, 0x12,
            0xEA, 0xE2, 0xFA, 0xF2, 0xCA, 0xC2, 0xDA, 0xD2, 0xAA, 0xA2, 0xBA, 0xB2, 0x8A, 0x82, 0x9A, 0x92,
        },
    },
};

static int
cd_pic_transform(int which, int pos, uint8_t ch)
{
    return cd_pic_s[which][pos][ch];
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

    /* The constant and the command together pick the table: the part answers every
       other combination too, but differently, and only these four were read off it. */
    if ((cd->cmd == 0xAA) && (konst == CD_CONST_CASE0))
        which = 0;
    else if ((cd->cmd == 0xAB) && (konst == CD_CONST_CASE1))
        which = 1;
    else if ((cd->cmd == 0xAA) && (konst == CD_CONST_CASE2))
        which = 2;
    else if ((cd->cmd == 0xAB) && (konst == CD_CONST_CASE3))
        which = 3;
    else {
        pp_log("PP: CDONGLE picture key asked with command %02X and constant %04X -- not a"
               " pair the games use, and not one read off a part; refused\n",
               cd->cmd, konst);
        cd->refuse = 1;
        cd->tx_len = 0;
        return 1;
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

    cd->refuse = 0; /* decided afresh as each argument lands */

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
       transaction is served the leftovers.

       Once all three arguments are in and none of the table's entries matched, the
       real part says nothing at all -- it refuses, for every function and every other
       challenge tried.  Until then the two bytes stand in, since the arguments are
       still arriving. */
    if ((cd->cmd >= 0xA0) && (cd->cmd <= 0xA7) && (cd->nargs >= 3)) {
        pp_log("PP: CDONGLE command %02X (%02X %02X %02X) is not one a part answers --"
               " refused, as a real part does\n", cd->cmd, cd->arg[0], cd->arg[1], cd->arg[2]);
        cd->refuse = 1;
        cd->tx_len = 0;
        return;
    }
    if ((cd->cmd >= 0xA0) && (cd->cmd <= 0xA7)) {
        cd->tx_len = 2;
        cd->tx[0]  = cd->key;
        cd->tx[1]  = cd->key; /* plaintext 00 00 */
        pp_log("PP: CDONGLE command %02X (%02X %02X %02X) has no recorded answer\n",
               cd->cmd, cd->arg[0], cd->arg[1], cd->arg[2]);
        return;
    }

    /* The parallel-port autodetect (`0x0BAD`): AC with the one argument CB, one byte
       back.  The guest never examines it, but a real part answers C5 -- both the 2000 PT
       and the 2004 ES dongle -- so that is what goes out. */
    if ((cd->cmd == 0xAC) && (cd->nargs >= 1) && (cd->arg[0] == 0xCB)) {
        cd->tx_len = 1;
        cd->tx[0]  = (uint8_t) (0xC5 ^ cd->key);
        return;
    }

    /* The record read: AD A6 8C 0D, then the start offset and the length, then three
       more bytes the part ignores.  Measured on real parts: offset 0 with any length up
       to 48 is served, that many bytes; a non-zero offset or a longer length is refused.
       So the part exposes the 48-byte record and nothing else of its memory. */
    if ((cd->cmd == 0xAD) && (cd->nargs >= 5)) {
        const int off = cd->arg[3];
        const int len = cd->arg[4];

        if ((off != 0) || (len > CD_RECORD) || (len == 0)) {
            pp_log("PP: CDONGLE record read at offset %d, length %d -- a real part refuses"
                   " anything but offset 0 and up to %d bytes\n", off, len, CD_RECORD);
            cd->refuse = 1;
            cd->tx_len = 0;
            return;
        }
        cd->tx_len = len;
        for (int i = 0; i < len; i++)
            cd->tx[i] = (uint8_t) (dev->block[i] ^ cd->key);
        return;
    }

    /* Everything else gets the record, as it did before the part was measured: the
       record read's arguments arrive one at a time, and until they are all in there is
       nothing better to queue. */
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

    /* A refused query: the real part leaves ACK up after the host's claim, so the host's
       wait for it to fall runs out -- measured with tools/dongcap/cdong.c, which reports
       every such query as "timeout at claim".  It lets go at the next transaction. */
    if (cd->refuse)
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
    /* Log the guest's CS:IP alongside the access.  Static disassembly of the 2001
       library has repeatedly mapped the wrong routine -- the device-type table is
       error reporting, 0x39FF4 is port resolution -- because nothing said which code
       actually drives the wire.  This does: one boot names every routine that touches
       the port, in the order it does so. */
    /* The first time a non-BIOS segment drives the port, dump the code there.  Which
       routine this is cannot be settled by pattern-searching the binary -- EC and EE
       occur constantly as ModRM bytes -- but the bytes themselves identify it exactly. */
    if (pp_raw_want && (CS < 0xC000) && !dev->dumped_cs) {
        dev->dumped_cs = 1;
        pp_log("PPCODE segment %04X, 64 bytes at %05X:\n", CS, CS << 4);
        for (int r = 0; r < 4; r++) {
            char line[80];
            int  n = 0;
            for (int b = 0; b < 16; b++)
                n += sprintf(line + n, "%02X", mem_readb_phys((CS << 4) + r * 16 + b));
            pp_log("PPCODE +%02X %s\n", r * 16, line);
        }
    }

    if (pp_raw_want && dev->n_raw++ < 400000) {
    /* All 2001 protection I/O goes through Borland's inportb/outportb -- three far-called
       stubs in their own segment -- which is why no port instruction could be found in
       the protection segment itself.  CS:IP therefore always names the stub and never
       the caller.  Those stubs open with push bp / mov bp,sp, so the far return address
       sits at [bp+2] (IP) and [bp+4] (CS): report that instead, and the log names the
       routine that actually wanted the port. */
    /* Walk the BP chain.  The innermost frame is always one of the three library stubs,
       and the one above it is the write-N-times or read-N-times primitive, so neither
       names a protection routine.  Borland keeps a proper frame pointer, so following
       [bp] upwards gives the actual call stack -- which is the map that five rounds of
       reading the disassembly were trying to build by hand. */
    char     stack[96];
    int      n     = 0;
    uint16_t bp    = BP;

    for (int f = 0; (f < 4) && bp && (n < (int) sizeof(stack) - 12); f++) {
        const uint32_t at = ((uint32_t) SS << 4) + bp;

        n += sprintf(stack + n, " %04X:%04X",
                     mem_readw_phys(at + 4), mem_readw_phys(at + 2));
        bp = mem_readw_phys(at); /* the saved bp of the caller */
    }

    pp_log("PPRAW %6d%s %-10s %02X\n", dev->n_raw, stack, what, val);
    }
}

/* ------------------------------------------------------------------------------- */
/* The 2001 generation: HDONGLE is a Microwire serial EEPROM                         */
/* ------------------------------------------------------------------------------- */

/* Read out of the DE MENU.EXE, whose transport is four small routines that each drive
   one line through the shadow-register helper at file 0x3767F:

       0x38007   mask 0x02  ->  CS      (DATA bit 1)
       0x380A1   mask 0x20  ->  SK      (DATA bit 5)
       0x38344   mask 0x60  ->  DI on bit 6, then a clock pulse on bit 5
       0x381BA   clock low, high, low, then STATUS bit 5  ->  DO

   and two frames built out of them:

       0x38492   CS low, SK low, CS high, then 1 1 0 = READ, then the address, then
                 sixteen clocks each sampling DO -- MSB first
       0x385F2   the same but 1 0 1 = WRITE, address, sixteen data bits, then CS low
                 and high again and a poll of DO until it reads back ready
       0x387D8   1 0 0 = the EWEN/EWDS group, sent before any write

   That is Microwire, the 93C46/93C66 shape, and it is the whole of HDONGLE.  There is
   no challenge and no crypto on the wire: the library reads the record straight out of
   the part.  Sections 13 to 15 of notes/HANDOFF2001.md read the same 896 status reads as a
   write-then-read-back memory test.  They are not: they are the 56 words of the block
   read, sixteen bits each, and the DATA writes that looked like a clock-stretch are the
   instruction being shifted out, three port writes per bit.

   Addresses are eight bits because the identity answer below advertises 256 words.

   What the part holds is NOT the record in clear.  0x37F6F unscrambles the buffer after
   a read, and scrambles a word before a write:

       word[idx]  ^=  (idx - 8) ^ password1        and also ^ 0xFF00 when idx < 8

   with password1 = 0x7477, which reaches it as state[+0x08] -- service 0x32 copies the
   caller's first password there (0x39B53).  The library also adds 8 to the requested
   start word (0x36FAA), so a game asking for words 0..55 is served words 8..63 and the
   scramble index cancels back to a plain 0..55.  hd_load() stores the record already
   scrambled, so the guest's own unscramble hands it the record. */

/* The record's content.  Every one of the 37 executables on the DE image -- the 36
   games and MENU.EXE -- carries the same field table, once each, and parses the record
   with the same routine (`0x1F626` in FINDIT.EXE, reached from the dongle read at
   `0x1F3CF`).  The record is TEXT:

       bytes  0..29   the banner
             30..35   a number, six columns
             36..41   a number, six columns
             42..47   a number, six columns
             48..53   a number, six columns
             54..59   a number, six columns
             60..65   a number, six columns
             66..71   a number, six columns
             72..82   a number, ELEVEN columns

   `0x1F626(buf, start, end)` copies that byte range, skips leading spaces, and copies up
   to the range's strlen into a scratch buffer; `atol` then turns it into the dword the
   game keeps.  The numbers are right-aligned in their columns, which is what the
   skip-spaces loop at `0x1F65C` is for, and the banner is copied out by the same routine
   -- so the banner has to be NUL-terminated (strlen stops there and the trailing padding
   is dropped) while everything after it must be spaces.

   Two things follow, and both were wrong in the first cut of this device:

   1. **The padding cannot be NUL.**  The unpack loop at `0x1F4D2` stops at the first
      zero WORD, so a banner NUL-padded to 30 ends the record at byte 18 and every field
      after it reads as garbage.  That is exactly what a wrong content key looks like on
      screen: FINDIT stalls loading its level database, AMORE draws scrambled pictures.
   2. **KEYN.COM is not a copy of the record.**  It answers the patched `int 2Bh` with
      62 bytes -- banner, then eight little-endian dwords at +0x1E, +0x22 ... +0x3A --
      which is the PARSED STRUCT this routine produces, not what the dongle holds.  The
      crack replaces the whole read-and-parse routine, so it never needs the text form.
      notes/HANDOFF2001.md section 11 read those bytes as the record; they are its output.

   The values are KEYN's, converted back to numbers.  The first six are the same fixed
   content keys the 1999 dongles carry (0x38B, 0x181CD ... 0x89D, in the same order);
   160678 and 4259233598 are new in 2001, and the last is the one that needs eleven
   columns. */
static const struct {
    int          at;    /* first byte of the field */
    int          cols;  /* how wide it is */
    unsigned long val;
} hd_fields[] = {
    { 30,  6,        907UL }, /* = 0x000038B, 1999's v[1] */
    { 36,  6,      98765UL }, /* = 0x00181CD           v[2] */
    { 42,  6,     120672UL }, /* = 0x001D760           v[3]  FINDIT's database key */
    { 48,  6,     170898UL }, /* = 0x0029B92           v[4] */
    { 54,  6,      75902UL }, /* = 0x001287E           v[5] */
    { 60,  6,       2205UL }, /* = 0x000089D           v[6] */
    { 66,  6,     160678UL }, /* = 0x00273A6           2001 only */
    { 72, 11, 4259233598UL }  /* = 0xFDDEBF3E          2001 only */
};
#define HD_FIELDS ((int) (sizeof(hd_fields) / sizeof(hd_fields[0])))
#define HD_TEXT   83 /* through the last column of the last field */

/* The record is stored XORed with the caller's first password, and every HASP
   generation uses a different pair (Docs/19).  The transport does not vary: IGO 3,
   5 and 7 carry the same Microwire primitives, the same four-entry identity table
   and the same scrambler as 2001, so only this constant has to follow the release.

   IGO 6's pair reads as 0000/0000 in every image to hand, which Docs/19 argues is
   a neutered copy rather than a real value, so it is left out and falls through to
   the default. */
/* Nine dongles were dumped with h5dmp (PhotoPlay2000_h5dmp), which settles all of
   this against hardware rather than inference.  Each dump opens with the two
   passwords little-endian -- agreeing with the values Docs/19 lifted out of the
   binaries, including 2006, whose images all carry 0000/0000 and are therefore the
   neutered copies that doc suspected.

   The dumps also confirm the byte order independently: a 2001 dump reads as its
   record only after every word is byte-swapped, while an I.G.O. dump reads
   directly, which is exactly the high-first / low-first split the guests use.

   And they show three record shapes, not one. */
enum {
    HD_R2001 = 0, /* 30-column right-aligned banner, then decimal columns */
    HD_RSION,     /* territory, NUL, "sion 2000 (SP)", then binary dwords */
    HD_RVERS      /* territory, '-', "Version", the token, then binary dwords */
};

/* Two of these releases do not carry their passwords as literals: MENU.EXE writes them
   at runtime and probes, keeping whichever pair the part answers service 5 with a 1 to.
   I.G.O. 6 does it at 0x3B2C8 and I.G.O. Italy at 0x3C252, and the sequence is the same
   in both -- try 7477/7D57, fall back to 68BB/1329, and if neither answers, set BOTH
   passwords to zero and carry on.

   On the hardware the probe settles on 68BB/1329: a real part refuses the 7477 burst
   (docs/research-v2/10.11), measured on the IGO 8 Italy dongle.  This part does the same
   now -- `probe` switches on its burst check, hs_burst_class() -- so these releases
   decode the record with the pair their dongle holds, like every other release.  What
   this part used to make them do instead is in hd_load(). */
static const struct {
    const char *banner;
    uint16_t    pass1;
    int         probe; /* MENU.EXE hunts for the pair: the part must refuse the wrong one */
    int         swap;  /* the guest unpacks each word low byte first */
    int         shape;
    int32_t     v6;    /* the last two dwords vary by generation */
    int32_t     v7;    /* per-unit; no game is known to read it */
    uint32_t    tkey;  /* the picture cipher's key -- docs/research/31 */
    uint16_t    tinit; /* and the register its preamble leaves behind */
    /* Which line the guest clocks an oracle query on, and what a round preamble
       looks like.  I.G.O. 2 clocks on DATA bit 4 and preambles with any bit-0
       rise; I.G.O. 3's library clocks on bit 0 instead and marks the preamble
       with one specific payload.  Read out of I.G.O. 3's own MENU.EXE -- see
       t_data() -- rather than guessed, and kept per release so that the
       generation which already works cannot be disturbed by the other. */
    /* Whether this release drives its session layer -- the identity ramp, the 64-step
       sweep and the liveness probe -- with bit 7 SET.  I.G.O. 3 does; I.G.O. 2 does not.
       It decides two things: whether the sweep matcher may ignore bit 7, and whether
       bit-7-set writes must be kept away from the Microwire decoder, whose CS and SK
       lines those payloads would otherwise clock.

       It is NOT about the oracle clock.  An earlier version of this table carried a
       per-release query clock, on the strength of I.G.O. 3 showing bit-0 triples and no
       bit-4 ones.  Measured with tools/dongcap/framing.py, those bit-0 triples are
       command bytes and I.G.O. 3 simply never enters a keyed round: I.G.O. 2 on real
       hardware gives 4,720 consultations in 118 rounds of exactly 40, I.G.O. 3 gives
       zero.  The clock never moved. */
    int         sess_hi;

    /* Answer the identity ramp, the sweep and the probe with the rule that used to
       stand in for a part -- XOR over addr %% 3 == 0, address 14 toggled, landing the
       library's accumulator on 0x1C, whose handler picks the size unconditionally --
       instead of the answer measured off the 68BB/1329 dongle.  That measured answer
       lands on 0x18, whose handler is conditional, and a 6B91/24A3 build evidently
       fails the condition: I.G.O. 5 reached its menu on the synthesised rule (1.5,
       2026-09-01) and says "wrong dongle version" on the measured one, having done
       nothing on the wire after the probe.  So the 68BB part's identity is the 68BB
       part's, and the family this build wants has never been captured.

       The value is the address toggled on top of the addr %% 3 == 0 set, and the
       accumulator lands on twice it: 14 gives 0x1C, 6 gives 0x0C.  Which one matters,
       because the 2005 library's identity table is not the 2001 one.  TOWERS.EXE
       0x2D1D9..0x2D255 maps 0x08 to type 1 with 64 words (or none), 0x0C to type 1
       with 256 words, 0x18 to type 3 with no memory and 0x1C to type 5 with no memory
       -- and every memory or data service then refuses a part with no memory (error
       3 at 0x2DE84 and 0x2DFC3).  0x1C got the record read and the menu up because
       the record reader does not check; it never got HaspDecodeData, which is what
       the two enciphered button faces need (docs/research/36).

       0x0C ("MemoHASP, 256 words") and 0x08 ("MemoHASP, 64 words") were both tried
       on the PT MB001 image, 2026-09-14: with either the menu runs ramp, command
       bytes, probe, sweep twice and reports "wrong dongle version" without ever
       reading memory, so the type-1 path checks something after the sweep that the
       type-5 path does not, and it is not yet mapped.  14 stays: it is the only
       answer that boots.

       What it checked was the sweep (2026-09-25, docs/research-v2/10.12): I.G.O. 5 was
       being served 68BB's sweep table, and its second sweep a replay of the first.
       With the part's own table and its bursts followed -- the second sweep opened by a
       burst the part refuses, so answered with nothing -- the measured identity is what
       a real 2005 part gives, and I.G.O. 5 is back on it.  The field stays for the
       record and is 0 in every row. */
    int         synth_ident;
} hd_keys[] = {
    { "Version 2001",  0x7477, 0, 0, HD_R2001, 160678,     -35733698, 0xCF47CB42, 0x7DF, 0, 0 },
    { "Version 2002",  0x68BB, 0, 1, HD_RSION, 160678,   -371202944, 0x3B227944, 0x7DF, 0, 0 }, /* I.G.O. 2 */
    { "Version 2003",  0x6B91, 0, 1, HD_RSION, 160678,   -738037894, 0xAB32E970, 0x5DF, 1, 0 }, /* I.G.O. 3 */
    /* I.G.O. 5 shares I.G.O. 3's password pair, so it should share the key.  Its FINDIT
       is plain, so nothing here has been able to check that -- it is the pair talking,
       not a measurement.

       It does share I.G.O. 3's transport, and that IS measured: on 1.9.1 the PT MB001
       image put the identity ramp on the wire as 80 82 .. FE and the liveness probe as
       C0, 8A/8B, F8/F9, DA/DB -- bit 7 set throughout, I.G.O. 3's form to the byte.
       Left bit-7-clear here the sweep matcher never recognised the probe, no sweep was
       served, the bytes were clocked into the Microwire decoder instead, and the menu
       stopped at "wrong dongle version" -- where the same image had reached the menu
       (garbled buttons) before the two forms were told apart. */
    { "Version 2005B", 0x6B91, 0, 1, HD_RVERS,      0,            0, 0xAB32E970, 0x5DF, 1, 0 }, /* I.G.O. 5 */
    /* The first I.G.O. 5, before the B: the IT CZ033 image, whose MAIN.SET is dated
       23.11.2004 and says "Version 2005 (IT)".  Its MENU.EXE and all 43 of its games are
       the older library, the one I.G.O. 2 and 3 carry, and that library does not parse
       the record at all -- it strcpy's a hardcoded "Version 2005 (", strcat's the record
       from byte 0 up to the first NUL, strcat's ")" (MENU.EXE 0x3521B), and the menu then
       strcmp's the result against MAIN.SET's "Version" (0x6541).  So byte 2 has to be the
       NUL that ends the territory, which is I.G.O. 3's shape; the B shape's '-' ran on
       into "Version 2005 (IT-Version2005)" and a "wrong dongle version" screen.  The
       transport is left as the B row's, which is what read that record off the wire. */
    { "Version 2005",  0x6B91, 0, 1, HD_RSION,      0,            0, 0xAB32E970, 0x5DF, 1, 0 }, /* I.G.O. 5, pre-B */
    /* 2006 and later ship plain GIF, so no picture needs the round -- but their parts
       compute it all the same, and their library asks: the 68BB/1329 part's key and
       register, measured on the 2006 PT and 2007 ES dongles (docs/research-v2/10.3).
       Their library is I.G.O. 3's and 5's, bit 7 set throughout its session layer; with
       these rows saying otherwise the sweep went unrecognised, a refused burst silenced
       nothing, and I.G.O. 6's probe took 7477 (docs/research-v2/10.12).  Scored against
       the real I.G.O. 7 ES boot, the session layer then agrees on 2,048 of 2,048 reads. */
    { "Version 2006",  0x68BB, 1, 1, HD_RVERS,      0,            0, 0x3B227944, 0x7DF, 1, 0 }, /* I.G.O. 6 */
    { "Version 2007",  0x68BB, 0, 1, HD_RVERS,      0,            0, 0x3B227944, 0x7DF, 1, 0 }, /* I.G.O. 7 */
    /* I.G.O. Italy reports NDONGLE rather than HDONGLE, which was read as meaning it is
       not on this path at all.  It is, and it is not even a special case: MENU.EXE
       0x3C322 is the same filler every other I.G.O. build uses, down to the format
       string -- read 15 words from word 0, then sprintf "%s %s (%c%c)" from record
       bytes 3..9, 10..13, 0 and 1.  What is special is only the needle it then looks
       for, "Version 08IT" at DG+0x4D63, and the bit it sets on failure, 0x400.  So the
       record wants "Version" and the token "08IT" in the usual slots and the space
       arrives from the format; writing the banner in as text cannot match, because
       byte 0 would be 'V' and the territory is taken from bytes 0 and 1.
       Its passwords are not literals either -- 0x3C252 is the same probe I.G.O. 6 runs,
       so the key is zero here too.  The 2008 pair is on record for when service 5 can
       tell the two apart. */
    { "Version 08",    0x68BB, 1, 1, HD_RVERS,      0,            0, 0x3B227944, 0x7DF, 1, 0 }  /* I.G.O. Italy */
};

/* The row this banner belongs to, or -1 if no release in the table claims it.  That
   answer is also what says whether the parallel HASP part should be on the port at all:
   the generations with a row are exactly the generations that have one.

   A row matches as a prefix and the first match wins, so "Version 2005B" has to stay
   above "Version 2005" -- the two are different records. */
static int
hd_release_opt(const char *banner)
{
    for (int n = 0; n < (int) (sizeof(hd_keys) / sizeof(hd_keys[0])); n++) {
        if (!strncmp(banner, hd_keys[n].banner, strlen(hd_keys[n].banner)))
            return n;
    }
    return -1;
}

/* Which row to build the record from.  Callers that have already decided the part is
   present want a row whatever happens, so an unrecognised banner falls back to 2001. */
static int
hd_release(const char *banner)
{
    const int n = hd_release_opt(banner);

    return (n < 0) ? 0 : n;
}

static void
hd_load(pp_t *dev, const char *banner)
{
    const int      rel  = hd_release(banner);
    /* History, kept because each step was measured: a probing release used to descramble
     * with the FIRST pair its probe tries, because this part accepted any pair.
     *
     * I.G.O. 6 and I.G.O. Italy write their passwords at runtime and hunt: try
     * 7477/7D57, fall back to 68BB/1329, and if service 5 answers neither, set both to
     * zero.  This used to serve them a record scrambled with 0x0000, on the reasoning
     * that this part answers service 5 for none of the pairs and so the guest ends up at
     * that last branch.  That was an inference and the screen disproves it.
     *
     * Measured: I.G.O. 6 DE puts `" EDGB (31)` where `Version 2006 (DE)` belongs.  Undo
     * our scramble with each candidate and only one reproduces it --
     *
     *     guest uses 0x0000   ->  "Version 2006 (DE)"    what we assumed
     *     guest uses 0x68BB   ->  ">...... .X.^. (.-)"   its own dumped pair
     *     guest uses 0x7477   ->  "\"...... EDGBw (31)"  <- what is on screen
     *
     * -- so the probe accepts 7477/7D57, the pair it tries first, and the record has to
     * be scrambled with that.  The dumped pair for these releases (0x68BB/0x1329) stays
     * on record in the table; it is what the hardware holds, and it is what this should
     * switch back to if service 5 is ever modelled well enough to tell the pairs apart. */
    /* ...and the part now refuses the pair it does not hold, so the probe settles on the
       real one, as it does on the hardware (docs/research-v2/10.11): the record is
       scrambled with the dongle's own pass1 for every release. */
    const uint16_t key  = hd_keys[rel].pass1;
    const int      swap = hd_keys[rel].swap;

    uint8_t rec[HD_RECORD];
    char    num[16];
    size_t  blen = strlen(banner);

    /* Spaces everywhere the record has content, zeros after it: the unpack loop reads
       until it meets a zero word, and byte 83 onwards is where it should stop. */
    memset(rec, 0, sizeof(rec));

    if (hd_keys[rel].shape == HD_R2001) {
        /* The 2001 dongle holds its banner RIGHT-aligned in the 30-column field --
           "Version 2001 (ES)" is 17 characters after 13 spaces -- which is what the
           extractor's skip-leading-spaces loop at 0x1F65C is for.  The numbers after
           it are zero-padded, and the last is written signed: the hardware says
           " -35733698", not 4259233598, and an atol of the unsigned form would
           overflow. */
        memset(rec, ' ', HD_TEXT);
        if (blen > HD_BANNER)
            blen = HD_BANNER;
        memcpy(&rec[HD_BANNER - blen], banner, blen);

        for (int f = 0; f < HD_FIELDS; f++) {
            const int32_t v = (f == 6) ? hd_keys[rel].v6
                            : (f == 7) ? hd_keys[rel].v7
                                       : (int32_t) hd_fields[f].val;
            int n;

            if (f == 7)
                n = snprintf(num, sizeof(num), "%ld", (long) v);
            else
                n = snprintf(num, sizeof(num), "%0*ld", hd_fields[f].cols, (long) v);
            if ((n > 0) && (n <= hd_fields[f].cols))
                memcpy(&rec[hd_fields[f].at + hd_fields[f].cols - n], num, (size_t) n);
        }
    } else {
        /* The I.G.O. record is not a banner at all.  MENU.EXE 0x3B300 and FINDIT.EXE
           0x23C0B read the territory from bytes 0..1, the release word from 3..9 and
           the version token from 10..14, and format the three with "%s %s (%c%c)".
           The 2005, 2006 and 2007 dongles hold

               "PT" '-' "Version" "2005B" 00 ')' 00

           and the 2002 and 2003 ones hold the older form, where the territory has been
           written over the first three characters of a 2000-era banner:

               "PT" 00 "sion 2000 (SP)" 00

           Both are copied verbatim from the dumps.  Feeding this parser 2001's record
           instead prints "sion 20 05B" with the territory as the first two characters,
           which is how the layout was found before the dumps existed.

           The older form is read by a different parser, which is why its byte 2 is a
           NUL: I.G.O. 2, 3 and the pre-B 2005 build compose "Version 200x (" + the
           record as a string + ")", so only the territory is ever seen. */
        const char *lp = strchr(banner, '(');
        const char *sp = strchr(banner, ' ');

        if ((lp != NULL) && lp[1] && lp[2]) {
            rec[0] = (uint8_t) lp[1];
            rec[1] = (uint8_t) lp[2];
        }
        if (hd_keys[rel].shape == HD_RSION) {
            memcpy(&rec[3], "sion 2000 (SP)", 14);
        } else {
            rec[2] = '-';
            memcpy(&rec[3], "Version", 7);
            if (sp != NULL) {
                size_t vl = strlen(sp + 1);

                if ((lp != NULL) && (lp > sp + 1))
                    vl = (size_t) (lp - (sp + 1));
                while (vl && (sp[vl] == ' '))
                    vl--;
                memcpy(&rec[10], sp + 1, (vl < 5) ? vl : 5);
            }
            rec[16] = ')';
        }

        /* The content keys are binary here: eight little-endian dwords at byte 30,
           which the guest reads as two-word pairs at start 0x0F + 2i (0x23C8D) and
           files at di+0x1E onwards -- the same slots 2001 fills from its columns.
           v0..v5 are identical on every dongle dumped; v6 is 160678 up to 2003 and
           zero from 2005 on. */
        for (int n = 0; n < 8; n++) {
            const size_t  o = 30 + ((size_t) n * 4);
            const uint32_t v = (n == 6) ? (uint32_t) hd_keys[rel].v6
                             : (n == 7) ? (uint32_t) hd_keys[rel].v7
                                        : (uint32_t) hd_fields[n].val;

            rec[o]     = (uint8_t) (v);
            rec[o + 1] = (uint8_t) (v >> 8);
            rec[o + 2] = (uint8_t) (v >> 16);
            rec[o + 3] = (uint8_t) (v >> 24);
        }
    }

    /* 2001 unpacks each word high byte first (0x362DF), so a word is just the next two
       record bytes in order.  The I.G.O. builds unpack low byte first, and serving them
       2001's order puts the banner on screen with every byte pair swapped -- "Version
       2005B (AT)" comes out as "roi n02 50 BA (eV)", which is how this was found.
       Everything outside the record decodes to zero, which costs nothing and keeps the
       part looking uniform. */

    /* What a real part holds past the content, read live through the passthrough on
       2026-09-24: every I.G.O. dongle (2003, 2005, 2006, 2007 and Italy) is FF from byte
       62, straight after the last dword, to the end; the 2001 dongle is zero after its
       columns but for FF FF in the last word.  No reader looks there -- the I.G.O. parser
       reads fixed slots and 2001's unpack stops at the first zero word -- but this is what
       a ReadBlock of the whole record returns off the hardware.

       Not reproduced: bytes 18..29 of the I.G.O. shapes, which on every dongle hold three
       far pointers left over from the programming PC (39 3F 12 3D ... on the 2003 PT, 62
       40 DE 3F ... on the 2005 PT), and the stray 'P' at byte 15 of the 2007 ES and Italy
       dongles.  Those differ per unit and are debris; the zeros here are as good as any. */
    if (hd_keys[rel].shape == HD_R2001) {
        rec[HD_RECORD - 2] = 0xFF;
        rec[HD_RECORD - 1] = 0xFF;
    } else
        memset(&rec[62], 0xFF, HD_RECORD - 62);

    for (int i = 0; i < HD_WORDS; i++) {
        const int j     = i - HD_START;
        uint16_t  plain = 0;

        if ((j >= 0) && (((j * 2) + 1) < HD_RECORD))
            plain = swap ? (uint16_t) ((rec[(j * 2) + 1] << 8) | rec[j * 2])
                         : (uint16_t) ((rec[j * 2] << 8) | rec[(j * 2) + 1]);

        dev->hd_mem[i] = (uint16_t) (plain ^ (uint16_t) (i - HD_START) ^ key
                                     ^ ((i < HD_START) ? 0xFF00 : 0x0000));
    }

    pp_log("PP: HD2001 EEPROM loaded -- record \"%.20s\" (%s layout), words %d..%d,"
           " scramble key %04X, %s byte order\n",
           (const char *) rec,
           (hd_keys[rel].shape == HD_R2001) ? "2001"
           : (hd_keys[rel].shape == HD_RSION) ? "I.G.O. sion" : "I.G.O.",
           HD_START, HD_START + (HD_RECORD / 2) - 1, key,
           swap ? "low-first" : "high-first");
}

/* One DATA write, decoded as Microwire.  Bits are clocked in on the rising edge of SK
   while CS is high; taking CS low abandons whatever was in flight. */
static void
hd_write_data(pp_t *dev, uint8_t val)
{
    /* Not cs/sk/di: cpu.h has macros of those names for the guest's own registers. */
    const int sel = (val & HD_CS) != 0;
    const int clk = (val & HD_SK) != 0;
    const int dat = (val & HD_DI) != 0;

    if (!sel) {
        dev->hd_ph = HD_IDLE;
        dev->hd_n  = 0;
    } else if (clk && !dev->hd_sk) {
        switch (dev->hd_ph) {
            case HD_IDLE:
                /* Leading zeros are not a start bit -- wait for DI high. */
                if (dat) {
                    dev->hd_ph    = HD_OP;
                    dev->hd_n     = 0;
                    dev->hd_op    = 0;
                    dev->hd_ready = 0;
                }
                break;

            case HD_OP:
                dev->hd_op = (dev->hd_op << 1) | dat;
                if (++dev->hd_n == 2) {
                    dev->hd_ph   = HD_ADDR;
                    dev->hd_n    = 0;
                    dev->hd_addr = 0;
                }
                break;

            case HD_ADDR:
                dev->hd_addr = (uint8_t) ((dev->hd_addr << 1) | dat);
                if (++dev->hd_n == dev->hd_abits) {
                    dev->hd_n = 0;
                    /* Eight address bits reach past the 64 words there are; the record
                       is read from word 8 and never gets there, so wrap rather than
                       overrun. */
                    dev->hd_addr &= HD_WORDS - 1;
                    switch (dev->hd_op) {
                        case 2: /* READ */
                            dev->hd_sr = dev->hd_mem[dev->hd_addr];
                            dev->hd_ph = HD_READ;
                            if (dev->hd_reads++ < 32)
                                pp_log("PP: HD2001 read word %02X -> %04X\n",
                                       dev->hd_addr, dev->hd_sr);
                            break;
                        case 1: /* WRITE */
                            dev->hd_sr = 0;
                            dev->hd_ph = HD_WRITE;
                            break;
                        case 3: /* ERASE */
                            if (dev->hd_wen)
                                dev->hd_mem[dev->hd_addr] = 0xFFFF;
                            dev->hd_ready = 1;
                            dev->hd_ph    = HD_DONE;
                            break;
                        default: /* EWEN/EWDS/ERAL/WRAL, told apart by the top address bits */
                            dev->hd_wen = (dev->hd_addr >> (dev->hd_abits - 2)) == 3;
                            dev->hd_ph  = HD_DONE;
                            break;
                    }
                }
                break;

            case HD_READ:
                /* The host clocks low, high, low and only then samples, so the bit has to
                   be on the line from this edge on.  Sixteen of them, MSB first. */
                if (dev->hd_n < 16)
                    dev->hd_do = (dev->hd_sr >> (15 - dev->hd_n)) & 1;
                dev->hd_n++;
                break;

            case HD_WRITE:
                dev->hd_sr = (uint16_t) ((dev->hd_sr << 1) | dat);
                if (++dev->hd_n == 16) {
                    if (dev->hd_wen)
                        dev->hd_mem[dev->hd_addr] = dev->hd_sr;
                    dev->hd_ready = 1;
                    dev->hd_ph    = HD_DONE;
                }
                break;

            default:
                break;
        }
    }

    dev->hd_sk = clk;
}

/* ------------------------------------------------------------------------------------
 * LPT PASSTHROUGH -- hand the guest's parallel port to a real one.
 *
 * Set PEEPEEBOX_LPT_PASSTHRU to the host port's base in hex (e.g. 378) and the four
 * accessors below stop emulating anything and drive that port instead.  The emulated
 * dongle is bypassed entirely; a real one on the host answers the guest.
 *
 * Why this lives here rather than in a generic passthrough device: these four functions
 * are already the whole of the guest's parallel-port surface, and pp_raw() already logs
 * every access with the guest's CS:IP.  Passing through at this point therefore yields a
 * complete, annotated wire trace for free -- which is the actual objective.  The picture
 * cipher's keyed round is computed inside the dongle (docs/research/20 section 3), and no
 * hand-written frame has ever made a real part answer, so the only way left to learn the
 * sequence is to watch the game itself perform it.
 *
 * Raw in/out rather than ppdev: ppdev's PPWCONTROL applies the parport layer's own
 * inversion to the control lines, so the byte on the wire would no longer be the byte the
 * guest wrote.  Direct port I/O keeps the register semantics identical to the emulated
 * path.  It needs ioperm, so run as root when using this.
 */
#if defined(__linux__) && (defined(__i386__) || defined(__x86_64__))
/* <sys/io.h> deliberately NOT included: 86Box's own io.h already declares inb/outb, and
   with the opposite argument order -- outb(port, val) here against glibc's
   outb(val, port).  Including both would either fail to compile or, worse, silently
   swap the arguments.  So declare ioperm and reach the port with inline asm. */
extern int ioperm(unsigned long from, unsigned long num, int turn_on);

static inline void
pp_outb_real(unsigned port, uint8_t v)
{
    __asm__ __volatile__("outb %0, %w1" : : "a"(v), "d"((unsigned short) port));
}

static inline uint8_t
pp_inb_real(unsigned port)
{
    uint8_t v;

    __asm__ __volatile__("inb %w1, %0" : "=a"(v) : "d"((unsigned short) port));
    return v;
}

static int      pp_pass_state = -1; /* -1 unknown, 0 off, 1 active */
static unsigned pp_pass_base  = 0;

static int
pp_passthru(void)
{
    if (pp_pass_state < 0) {
        const char *s = getenv("PEEPEEBOX_LPT_PASSTHRU");

        pp_pass_state = 0;
        if (s != NULL) {
            pp_pass_base = (unsigned) strtoul(s, NULL, 16);
            if (pp_pass_base == 0)
                pp_pass_base = 0x378;
            if ((ioperm(pp_pass_base, 3, 1) == 0) && (ioperm(0x80, 1, 1) == 0)) {
                pp_pass_state = 1;
                pp_log("PP: LPT PASSTHROUGH active at %03X -- emulated dongle bypassed\n",
                       pp_pass_base);
            } else
                pp_log("PP: LPT passthrough at %03X FAILED -- needs root\n", pp_pass_base);
        }
    }

    return pp_pass_state;
}

static void
pp_pass_out(unsigned off, uint8_t v)
{
    /* The cabinets have a plain SPP port, where CONTROL bit 5 means nothing.  The 1999
       guest keeps it set, and on a bidirectional host port it turns the data lines
       around -- which unpowers the 1999 dongle and reads STATUS as 00 for ever. */
    if (off == 2)
        v &= (uint8_t) ~0x20;
    pp_outb_real(pp_pass_base + off, v);
    (void) pp_inb_real(0x80); /* the traditional I/O delay, as the DOS library does */
    (void) pp_inb_real(0x80);
}

static uint8_t
pp_pass_in(unsigned off)
{
    return pp_inb_real(pp_pass_base + off);
}
#else
static int
pp_passthru(void)
{
    return 0;
}

static void
pp_pass_out(unsigned off, uint8_t v)
{
    (void) off;
    (void) v;
}

static uint8_t
pp_pass_in(unsigned off)
{
    (void) off;
    return 0xFF;
}
#endif

/* The last DATA value handed to the host port, or -1 before the first.  86Box calls
   write_data only while the guest's port is out of bidirectional-input mode, and the
   1999 guest keeps CONTROL bit 5 set throughout: its nibbles reach the port's latch and
   never the callback.  The emulated dongle reads the latch itself (pp_latch_nibble); the
   passthrough has to do the same, or the real part never sees a nibble. */
static int pp_pass_dat = -1;

static void
pp_pass_sync_data(pp_t *dev)
{
    const uint8_t cur = (dev->lpt != NULL) ? ((lpt_t *) dev->lpt)->dat : dev->last_data;

    if ((int) cur != pp_pass_dat) {
        pp_pass_out(0, cur);
        pp_pass_dat = cur;
        pp_raw(dev, "write_data", cur);
    }
}

/* ------------------------------------------------------------------------------------
 * The keyed round, answered here instead of by a physical part.
 *
 * From 2001 to I.G.O. 3 the photographs are enciphered with a cipher whose only keyed
 * step is a byte-to-bit question put to the dongle -- forty of them per eight bytes,
 * two of those eight-byte blocks per 4 KB buffer.  Everything around it is arithmetic
 * the game does itself, so this is the whole of what the hardware ever contributed.
 *
 * The part is a small shift register.  `t_cur` is seeded by the preamble, the query
 * carries five bits (the framing drops three), and the answer is
 * `((t_cur >> 11) ^ key_bit) & 1`.  How the key was recovered without owning three
 * dongles -- and why the software part reproduces all 46,036 rounds a real one answered
 * -- is docs/research/31.
 *
 * Two clock edges matter, and they are on different lines, which is what keeps this off
 * the Microwire decoder above:
 *
 *   DATA bit 0 rising, bit 7 set   a command byte: the round preamble, so reset
 *   DATA bit 4 rising, bit 7 set   a query: answer it, and hold the bit for STATUS
 *
 * Query payloads never move bit 0, and command bytes never move bit 4, so neither is
 * mistaken for the other.
 *
 * On a bit-7 release (I.G.O. 3) the bit-4 edge cannot be used, because the session
 * layer's own payloads move bit 4 too.  There a query is recognised at the STATUS read
 * instead, by its shape -- see t_read_query().
 */

/* HaspEncodeData's modes 1..4 are this same register -- the same key, the same starting
   state -- with one term more in the feedback bit.  Read off a real 6B91/24A3 part on
   2026-09-25: 2,000 live encodes through I.G.O. 3's own library, 500 per mode, every port
   access recorded.  A round's first eleven answers read only the starting register, so
   they agreed with mode 0 in every mode, and the feedback bits could be read back off the
   answers from step 11 on (docs/research-v2/10.10):

       mode 1   register bit 3
       mode 2   1 ^ parity(i5)
       mode 3   register bits 0 ^ 3
       mode 4   1 ^ parity(i5) ^ register bit 6

   Each fits every one of its ~1,160 complete rounds, 46,000-odd answers, without an
   exception; the 2005 part answers the same questions identically.  Through the library
   under unicorn this reproduces FOTO/GAMESTAT.OLD, which is what those modes are used
   for -- a recording of a 6B91 part that the games check themselves against, and which
   this device used to replay instead (docs/research/37). */
static unsigned
t_mode_term(int mode, uint32_t cur, unsigned i5)
{
    unsigned p = i5 & 0x1F;

    p ^= p >> 4;
    p ^= p >> 2;
    p ^= p >> 1;
    p &= 1;
    switch (mode) {
        case 1:  return (cur >> 3) & 1;
        case 2:  return 1 ^ p;
        case 3:  return (cur ^ (cur >> 3)) & 1;
        case 4:  return (1 ^ p ^ (cur >> 6)) & 1;
        default: return 0;
    }
}

/* One consultation: shift the register on the offered byte and return the answer. */
static int
t_step(pp_t *dev, uint8_t val)
{
    const unsigned i5 = (unsigned) (((val >> 1) & 0x07) | ((val >> 2) & 0x18));
    const unsigned st = (dev->t_key >> i5) & 1;
    unsigned       b0 = i5 ^ ((st ^ 1) & (i5 >> 3)) ^ (i5 >> 4);
    uint32_t       pre;

    b0 ^= dev->t_cur >> 10;
    b0 ^= dev->t_cur >> 7;
    if (i5 & 2)
        b0 ^= dev->t_cur >> 5;
    if (i5 & 4)
        b0 ^= dev->t_cur >> 8;
    b0 ^= t_mode_term(dev->t_mode, dev->t_cur, i5);

    pre        = dev->t_cur ^ (uint32_t) ((i5 & 1) << 2);
    dev->t_cur = (pre << 1) | (b0 & 1);

    dev->t_burst++;
    if (dev->t_queries++ == 0)
        pp_log("PP: picture cipher -- answering the keyed round in software, "
               "key %08X\n", dev->t_key);

    return (int) (((dev->t_cur >> 11) ^ st) & 1);
}

/* One consultation on a bit-7 release.  Every mode is computed now: see t_mode_term(). */
static int
t_answer(pp_t *dev, uint8_t val)
{
    const int k = dev->t_mode;

    if ((dev->t_qn++ == 38) && (k >= 1) && (k <= 4) && (dev->t_mode_rounds[k]++ == 0))
        pp_log("PP: EncodeData mode %d -- answered by the shift register with its mode term\n", k);
    return t_step(dev, val);
}

/* A query on a bit-7 release, recognised when its answer is read.
 *
 * I.G.O. 3 frames a query exactly as I.G.O. 2 does -- payload, payload|0x10, payload,
 * then one STATUS read, bit 7 set -- but its session layer also runs with bit 7 set and
 * its sweep payloads occupy bits 1..6, so an ordinary payload change (8A -> F8) raises
 * bit 4 with no query involved.  That is why t_data() cannot act on the edge there.
 *
 * The shape tells them apart where the edge cannot: a sweep step is one write and a
 * read, a query is three writes -- p, p|0x10, p -- and a read.  Found by running
 * I.G.O. 3's own HASP library in unicorn (tools/dongcap/hasplib.py): with this rule its
 * boot check's two rounds come out at exactly 40 consultations each and DS:0x50F6
 * decodes to "c:/foto/gamestat.old".  Without it the rounds were answered as ramp
 * noise and the block came back as garbage. */
static int
t_read_query(pp_t *dev, uint8_t *st)
{
    const uint8_t *r = &dev->t_recent[3 - dev->t_nrecent];
    uint8_t        p;

    if (!dev->t_sess_hi || !dev->t_key || (dev->t_nrecent < 3))
        return 0;
    p = r[0];
    if ((r[2] != p) || (r[1] != (p | 0x10)) || !(p & 0x80) || (p & 0x11))
        return 0;

    *st = t_answer(dev, r[1]) ? HD_DO : 0x00;
    return 1;
}

static void
t_data(pp_t *dev, uint8_t val)
{
    const uint8_t rose = (uint8_t) (val & ~dev->t_last);
    int           query;
    int           preamble;

    if ((dev->t_nrecent == 0) || (dev->t_recent[2] != val)) {
        dev->t_recent[0] = dev->t_recent[1];
        dev->t_recent[1] = dev->t_recent[2];
        dev->t_recent[2] = val;
        if (dev->t_nrecent < 3)
            dev->t_nrecent++;
    }

    /* EncodeData's mode: the 84/A4 run opens a round, then k clocks on CA follow.  It
       is latched below by the command byte that opens the round, not counted up to the
       first query -- that query's own payload can be CA too. */
    if (val == 0x84)
        dev->t_modeclk = 0;
    else if ((val == 0xDA) && (dev->t_last == 0xCA))
        dev->t_modeclk++;

    /* The held answer (see pp_read_status) lasts exactly as long as the DATA value that
       produced it.  The transport repeats every write, so repeats of the same byte must
       all read back the same bit -- but the moment the guest writes anything else, the
       part has been clocked again and whatever it was holding is gone.
       "Until the next bit-7-clear write" was too generous: the 64-step sweep is bit-7-set
       too, so the hold survived into it and swallowed every sweep read. */
    if (dev->t_sess_hi && (val != dev->t_hold_val))
        dev->t_pending = 0;

    if (!(val & 0x80))
        goto out;                       /* the 1999/2000 halves keep bit 7 clear */

    /* One framing, measured on both generations.  An earlier version of this had a
       bit-0 query variant for I.G.O. 3, on the strength of seeing bit-0 triples there and
       no bit-4 ones.  That was the wrong conclusion: those bit-0 triples are COMMAND
       BYTES -- the same primitive I.G.O. 2 uses, and their bursts of 14-17 match 0x1B50
       sending fifteen.  Measured with tools/dongcap/framing.py over both wires:

           I.G.O. 2, real part   4,720 consultations in 118 rounds of exactly 40
           I.G.O. 3, emulated    zero

       So I.G.O. 3 never enters a keyed round at all, and the clock never moved.  Query
       payloads never move bit 0 and command bytes never move bit 4, so neither is taken
       for the other. */
    preamble = (rose & 0x01) != 0;
    query    = !preamble && !dev->t_sess_hi && ((rose & 0x10) != 0);

    /* Why sess_hi suppresses the edge.
     *
     * On I.G.O. 2 a query is the only thing that moves bit 4 while bit 7 is set, so the
     * edge identifies it.  On I.G.O. 3 the session layer is bit-7-set AND its payloads
     * occupy bits 1..6 -- bit 4 included -- so an ordinary payload change from 8A to F8
     * raises bit 4 with no query involved.  Acting on that sets an answer this part then
     * holds, and the hold swallows the reads the 64-step sweep was owed: measured as
     * "sync lost" on every burst, plus a spurious "answering the keyed round" line.
     *
     * I.G.O. 3's rounds are answered by t_read_query() instead, which waits for the
     * read and looks at the three writes before it.  Until the liveness probe was
     * answered properly (see pp_read_status) the library never got as far as a round,
     * which is why none was ever counted there. */

    if (preamble) {
        /* A burst's length is what says what the burst WAS, and the two are not the
           same thing at all:

             40 consultations  a keyed round -- 39 shift steps and one more.  This is
                               EncodeData actually consulting the part.
             ~15               0x1B50 sending a service request: fifteen payloads
                               derived from the passwords, one bit collected each.

           Measured on I.G.O. 3: 17, 17, 16, 16, 14 -- never 40.  So the keyed round is
           never reached there, the library stops inside the service exchange, and the
           picture cipher's key has nothing to do with that failure.  Logged because
           this took several builds to notice and the number is the whole story.
           (That was with the liveness probe answered wrongly.  With it answered, the
           boot check's two rounds are 40 and 40 -- see t_read_query.) */
        if (dev->t_burst > 0 && dev->t_bursts++ < 24)
            pp_log("PP: bit-0 burst of %d consultations -- %s\n", dev->t_burst,
                   (dev->t_burst == 40) ? "a keyed round"
                                        : "NOT a round; a service request (0x1B50 sends 15)");
        dev->t_burst   = 0;
        dev->t_cur     = dev->t_init;
        dev->t_pending = 0;
        dev->t_qn      = 0;
        dev->t_mode    = dev->t_modeclk;
    } else if (query) {
        dev->t_ans      = (uint8_t) t_step(dev, val);
        dev->t_pending  = 1;
        dev->t_hold_val = val;
    }

out:
    dev->t_last = val;
}

/* Collect the command bytes a burst is made of -- clocked on DATA bit 0, bit 7 and bit 0
   dropped -- and judge each burst once its fifteen bytes are in. */
static void
hb_data(pp_t *dev, uint8_t val)
{
    if ((val & 1) && !(dev->hb_prev & 1)) {
        const uint8_t b = (uint8_t) (val & 0x7E);

        if (b == 0x46)
            dev->hb_n = 0;
        else if (dev->hb_n >= 0) {
            dev->hb_win[dev->hb_n++] = b;
            if (dev->hb_n == 15) {
                const int c = hs_burst_class(dev->hb_win, dev->hb_pw);

                if ((c == HB_FOREIGN) && (dev->hb_mode != HB_FOREIGN) && (dev->hb_foreign++ < 4))
                    pp_log("PP: a burst this part does not accept -- silent until it sees its"
                           " own\n");
                if (c != HB_OTHER)
                    dev->hb_mode = c;
                dev->hb_n = -1;
            }
        }
    }
    dev->hb_prev = val;
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
    if (pp_passthru()) {
        pp_pass_out(0, val);
        pp_pass_dat = val;
        return;
    }
    if (dev->hd_probe) {
        if (dev->hb_gate)
            hb_data(dev, val);
        if (dev->t_key)
            t_data(dev, val);
        /* Three protocols share these wires and only one of them is Microwire.  On
           I.G.O. 3 the session layer is driven with bit 7 SET, and its payloads move
           bit 1 and bit 5 -- which this decoder reads as CS and SK, so it clocks the
           sweep in as instruction bits, reaches HD_READ, and then owns the STATUS reads
           that the sweep was supposed to answer.  Measured: the sweep matcher reported
           "sync lost" on every burst because it never saw those reads at all.

           Microwire traffic is bit-7-clear, so gating on that bit separates them
           exactly, and the record read is untouched.  I.G.O. 2 keeps its session layer
           bit-7-clear and is deliberately left alone. */
        /* ...except on the synthesised-identity path, which is 1.5's behaviour whole:
           there the decoder saw every byte, bit 7 or not, and the record read on I.G.O. 5
           -- which drives the Microwire lines with bit 7 set, like the rest of its
           traffic -- only ever decoded that way.  Gated, the read never reached the
           decoder and the menu formatted an empty buffer into its banner. */
        /* ...and except for I.G.O. 3's own record read, which is Microwire with bit 7
           set: 9E/BE is a clock with DI low, DE/FE one with DI high, 9C drops CS.  Gated
           out, the part never served a word, the menu formatted "Version 2003 (" plus
           an empty record into its banner, and the screen said IDONGLE not found.  Those
           frames all carry bits 2..4, which the session payloads mostly do not -- and
           where one does, the STATUS read that follows every session write resets the
           decoder long before nine clocks could make an instruction.  Found with
           tools/dongcap/hasplib.py: with this the banner reads "Version 2003 (DE)". */
        if (!dev->t_sess_hi || !(val & 0x80) || dev->t_synth_ident || ((val & 0x1C) == 0x1C))
            hd_write_data(dev, val);
    }
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
    if (pp_passthru()) {
        pp_pass_sync_data(dev);         /* the nibble must be on the lines before STROBE */
        pp_pass_out(2, val);
        dev->last_ctrl = val;
        return;
    }

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

/* What we answer step `n` of the 64-step sweep with: the bit at the step's address in
 * the table the last burst selected (docs/research-v2/10.12).
 *
 * The newer libraries check that the part is not a replay -- they sweep twice and refuse
 * the part if any folded byte of the second sweep equals the first (I.G.O. 3's core
 * 0x20EA).  A real part passes because the second sweep is not opened by the same burst.
 * I.G.O. 3's library sets the burst's mode byte to 50, and the part answers from its
 * identity table; I.G.O. 5's and 7's change another byte, which the part does not accept,
 * and it answers nothing -- every bit 1.  This device used to XOR each sweep with a serial
 * number to the same end; with the bursts modelled that is no longer needed, and it made
 * one read in eight of every I.G.O. 5 sweep wrong against a real part. */
static int
hs_sweep_bit(pp_t *dev, int n)
{
    /* the address this step's payload names */
    const uint8_t addr = (uint8_t) (hs_sweep_w[n] >> 1);
    int           bit;

    /* A burst the part does not accept silences the sweep it opens -- every step 1, as the
       real part answered I.G.O. 5's second sweep and the library's 7477 probe -- and only
       that: silencing the rest of the session layer too (the identity ramp, the 1E/1C
       tail of a memory read) is what put "dongle error" on I.G.O. 3 when its library
       logged in again, and a real part does not do it. */
    if (dev->hb_gate && (dev->hb_mode == HB_FOREIGN))
        bit = 1;
    else if (dev->hb_gate && (dev->hb_mode == HB_IDENT))
        bit = (int) ((HD_SIGNATURE >> addr) & 1u);
    else
        bit = (int) ((dev->hs_table >> (63 - addr)) & 1u);

    /* Record what the guest is actually being handed, in its own terms.
     *
     * I.G.O. 3's 0x24FB folds these bits into four words -- bit of step i into word
     * i>>4, most significant first -- byte-swaps each word at 0x2621, and compares the
     * eight bytes against DS:0x4C86.  Reproducing that fold here means the log shows the
     * exact eight bytes the gate will see, so a reading of that gate can be checked
     * without parsing a wire trace, and a sweep that loses sync shows up as a step index
     * that does not advance by one. */
    if (n != dev->hs_fold_next) {
        if (dev->hs_fold_next != 0)
            pp_log("PP: sweep answered step %d after step %d -- sync lost, so the eight"
                   " bytes below are not what the guest folds\n", n, dev->hs_fold_next - 1);
        memset(dev->hs_fold, 0, sizeof(dev->hs_fold));
        dev->hs_fold_next = 0;
    }
    if (n < 64) {
        dev->hs_fold[n >> 3] = (uint8_t) ((dev->hs_fold[n >> 3] << 1) | (bit & 1));
        dev->hs_fold_next    = n + 1;
        if (n == 63) {
            pp_log("PP: 64-step sweep served, the eight bytes 0x24FB folds:"
                   " %02X %02X %02X %02X %02X %02X %02X %02X\n",
                   dev->hs_fold[0], dev->hs_fold[1], dev->hs_fold[2], dev->hs_fold[3],
                   dev->hs_fold[4], dev->hs_fold[5], dev->hs_fold[6], dev->hs_fold[7]);
            dev->hs_fold_next = 0;
        }
    }
    return bit;
}

static uint8_t
pp_read_status(void *priv)
{
    pp_t   *dev = (pp_t *) priv;
    uint8_t st  = 0;

    if (pp_passthru()) {
        pp_pass_sync_data(dev);         /* the 1999 ack is DATA bit 4, set by the latch */
        st = pp_pass_in(1);
        pp_raw(dev, "read_status", st);
        return st;
    }

    /* The 2001 generation.  Its dongle is a Microwire EEPROM -- see the section above
       pp_write_data -- and this line is that part's DO.  Three things drive it, in the
       order the library asks for them.

       Behind the hd2001 option because driving STATUS bit 5 at all disturbs the 1999
       and 2000 paths, which read this same port for something else. */
    if (dev->hd_probe) {
        /* A bit-7 release's query, told by the three writes before this read.  Every
           read closes the window, so the next query is judged on its own writes only. */
        const int rq = t_read_query(dev, &st);

        dev->t_nrecent = 0;
        if (rq) {
            dev->hd_ph = HD_IDLE;           /* a query is not Microwire either */
            dev->hd_n  = 0;
            pp_raw(dev, "read_status", st);
            return st;
        }

        /* A query was just clocked in: that answer owns the line, ahead of everything
           else.  The picture cipher and the record read share DO, but never at the same
           moment -- a query is three DATA writes and one STATUS read, with nothing else
           between them. */
        if (dev->t_pending) {
            /* The answer is consumed by one read on I.G.O. 2, where a query is three
               writes and one read with nothing between them.

               I.G.O. 3's transport repeats every write four times, so a query is
               followed by SEVERAL reads.  Clearing on the first one drops the rest
               through to the session matcher below -- where they alias onto the sweep
               table, because query payload F8 masks to 0x78 which is hs_sweep_w[0].
               That is what produced "sync lost" on every burst: the sweep was never
               involved, the queries were being answered as sweep step 0.

               So hold the bit instead, which is what the part does anyway -- DO stays
               driven until the next clock.  t_data() clears it on the next command
               byte, query or bit-7-clear write. */
            if (!dev->t_sess_hi)
                dev->t_pending = 0;
            st             = dev->t_ans ? HD_DO : 0x00;
            pp_raw(dev, "read_status", st);
            return st;
        }

        /* A read instruction is in progress: the part owns the line and clocks the
           addressed word out, MSB first. */
        if (dev->hd_ph == HD_READ) {
            st = dev->hd_do ? HD_DO : 0x00;
            pp_raw(dev, "read_status", st);
            return st;
        }

        /* Otherwise the library is still identifying the part.  That scan comes first
           and is not a liveness check, which is what section 4 of notes/HANDOFF2001.md took
           it for -- it is an IDENTITY.

           0x37E1D walks a 64-step ramp with one STATUS read each and accumulates
           acc = 0x7E ^ XOR{ addr<<1 : DO set at addr }.  0x36B02 rejects 0x7E, then
           looks acc up in a four-entry table at cs:0x06FC -- 0008, 000C, 0018, 001C --
           and the matching entry sets the part's size in es:[bx+0x10].  Miss the table
           and the library gives up exactly as it does on 0x7E, which is why a merely
           varied answer once got no further than a constant one.

           0x001C is the value to aim for: its handler at 0x36C48 sets the size to 4 --
           256 words -- unconditionally, and the guest asks for 56, so the bounds check
           at 0x36F77 passes.  (0x0018 would do too, but its handler is conditional;
           0x0008 and 0x000C select size 0 and fail.)  XOR over { addr : addr % 3 == 0 }
           is 0x7E on its own, and toggling one further address a moves the result by
           a << 1, so adding addr 14 contributes 0x1C and lands acc on 0x1C exactly.  23
           of the 64 addresses end up set, which also keeps the liveness gate at 0x37EA7
           -- which only rejects a line stuck at one level -- satisfied.

           A physical dongle has now said otherwise, and this is no longer synthesised.
           The passthrough capture in docs/research/evidence holds a real 68BB/1329 part
           answering that ramp 96 times, identically every time, and HD_SIGNATURE is what
           it puts on DO: 37 of the 64 addresses set, giving acc = 0x18 rather than the
           0x1C this used to aim for.  Scored against those 96 ramps the measured answer
           agrees on 6144 of 6144 reads and the old rule on 2880 -- chance.

           0x18's handler is the conditional one, which is why 0x1C was picked in the
           first place; a real part evidently satisfies that condition.  See
           docs/research/32. */
        /* The address is DATA bits 1..6, so mask to six bits rather than taking the
           whole byte.  2001 drives the ramp as 00, 02 ... 7E and the top bit never
           appears, but I.G.O. 5 drives the identical sequence with bit 7 set --
           its sixteen clocked values are 2001's plus 0x80, and its ramp runs 80,
           82 ... FE.  The guest accumulates the loop index either way, so masking
           is all that is needed for the same answer to serve both. */
        const uint8_t w    = dev->last_data;
        const uint8_t addr = (uint8_t) ((w >> 1) & 0x3F);
        /* The 64-step sweep is the same sixty-four payloads in every generation --
           they are the output of the LCG `x = x*0x1989 + 5` seeded with 100, masked
           0x7E, which is what I.G.O. 3's MENU.EXE computes at 0x24FB and what
           hs_sweep_w[] was measured to be.  I.G.O. 2 puts them on the wire with bit 7
           clear and I.G.O. 3 with bit 7 set, so the matcher has to ignore that bit --
           but only for the releases that need it, so the generation that already works
           cannot be disturbed.  For I.G.O. 2 sw == w and nothing changes. */
        const uint8_t sw   = dev->t_sess_hi ? (uint8_t) (w & 0x7F) : w;
        int           bit;

        /* The library never reads STATUS in the middle of shifting an instruction --
           only during the sixteen data clocks of a read, which returned above.  So a
           read here says that whatever the decoder had half-collected was not an
           instruction, and dropping it stops the identity and liveness phases, which
           write and read alternately, from ever being taken for one. */
        dev->hd_ph = HD_IDLE;
        dev->hd_n  = 0;

        if (dev->t_synth_ident) {
            /* The pre-measurement rule, for the family the capture does not cover:
               see synth_ident in hd_keys.  One rule for ramp, sweep and probe alike,
               which is what 1.5 did and what I.G.O. 5 booted on. */
            bit             = ((addr % 3) == 0) != (addr == dev->t_synth_ident);
            dev->hs_ramping = 0;
            dev->hs_sweep   = 0;
        } else if (dev->hs_ramping && (w == (uint8_t) (dev->hs_ramp_prev + 2))) {
            dev->hs_ramp_prev = w;                 /* the ramp, continuing */
            dev->hs_sweep     = 0;
            bit               = (int) ((HD_SIGNATURE >> addr) & 1u);
        } else if ((w == 0x00) || (w == 0x80)) {
            dev->hs_ramping   = 1;                 /* ...or starting, either polarity */
            dev->hs_ramp_prev = w;
            dev->hs_sweep     = 0;
            bit               = (int) ((HD_SIGNATURE >> addr) & 1u);
        } else if ((dev->hs_sweep < 64) && (sw == hs_sweep_w[dev->hs_sweep])) {
            bit = hs_sweep_bit(dev, dev->hs_sweep);
            dev->hs_sweep++;
            dev->hs_ramping = 0;
        } else if (sw == hs_sweep_w[0]) {
            bit             = hs_sweep_bit(dev, 0);
            dev->hs_sweep   = 1;
            dev->hs_ramping = 0;
        } else if (sw == 0x1E) {
            /* The liveness probe's high half.  Matched on sw, like the sweep, because
               I.G.O. 3 sends it as 9E.  Matching the raw byte answered it from the
               signature -- address 15, which is clear -- and that 0 is precisely what
               the library's HaspEncodeData refuses with status -8, the "dongle error"
               I.G.O. 3 always stopped on.  Found by running the library in unicorn. */
            bit             = 1;
            dev->hs_ramping = 0;
            dev->hs_sweep   = 0;
        } else {
            /* A finished write leaves the part busy until it answers ready, which is
               what 0x385F2 polls for after raising CS again. */
            bit             = (int) ((HD_SIGNATURE >> addr) & 1u);
            dev->hs_ramping = 0;
            dev->hs_sweep   = 0;
        }

        st = (dev->hd_ready || bit) ? HD_DO : 0x00;

        pp_raw(dev, "read_status", st);
        return st;
    }

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

    if (pp_passthru()) {
        const uint8_t raw = pp_pass_in(0);

        pp_raw(dev, "read_data", raw);
        return raw;
    }

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
       The games check only the CRC.  FN_SYS.EXE, however, turns the serial into
       the cabinet's machine licence for fun.net -- [Photo Play] machlic; the
       default is the old fixed "PPBOX" (50 50 42 4F 58 00). */
    uint8_t           serial[6];
    static const char text[] = "Photo Play 2000 Version 3";

    photoplay_machlic_serial(serial);
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

    dev->hd_abits = HD_ABITS; /* until a release's identity says otherwise */
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

    /* The 2008 generation moved its dongle off the parallel port entirely: a serial
       smart-card reader on COM2, in dongle_igo8.c.  Docs/18.

       It used to be brought up unconditionally, on the grounds that a guest from
       any other generation never says a word to it.  That is true and it was
       still wrong: the reader *claims* COM2 whether or not anyone talks to it,
       and COM2 is where these cabinets put the Dataprint -- so on every
       generation but 2008 the printer found the port taken and could not
       attach.  The window said "not attached to any port" and its connect
       switch did nothing, because there was no device behind it.

       So it is attached when the image is a 2008 one, and when the image cannot
       be identified at all.  That second case is deliberate: an unreadable image
       keeps the old behaviour, because losing the dongle is a worse failure than
       losing the printer.  PEEPEEBOX_NO_SC=1 still forces it off.

       Photo Play 2.0 is not that second case, though photoplay_image_ident() says
       "not identified" for it: it has no MAIN.SET, so there is no banner to give
       the dongle.  The image was read all the same, it has no dongle of any kind to
       lose, and its menu does drive a Data-Print -- so it gets COM2 for that. */
    if (getenv("PEEPEEBOX_NO_SC") != NULL)
        pp_log("PP: PEEPEEBOX_NO_SC set -- the 2008 reader is not attached, COM2 is free\n");
    else {
        char banner[64] = "";
        const int known = photoplay_image_ident(banner, sizeof(banner), NULL, 0);

        if (!known && photoplay_image_is_pp20())
            pp_log("PP: Photo Play 2.0 image, which has no dongle -- the 2008 reader is not "
                   "attached, COM2 is free\n");
        else if (!known || (strstr(banner, "2008") != NULL)) {
            device_add(&igo8_reader_device);
            if (!known)
                pp_log("PP: image not identified -- attaching the 2008 reader anyway\n");
        } else
            pp_log("PP: %s is not a 2008 image -- the 2008 reader is not attached, "
                   "COM2 is free\n", banner);
    }

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

    /* The 2001 transport, which is a different part on the same port -- see the HDONGLE
       section.  Fill its EEPROM with the record the guest will read out of it.  The
       sweep this option used to arm is gone: sweeping taught nothing, because neither
       gate reads a value (notes/HANDOFF2001.md section 4), and the device now answers for
       real. */
    /* Whether the parallel HASP part is on the port at all.  Auto is the right default
       and now the shipped one: the releases that carry that part are exactly the ones
       hd_keys[] has a row for, and the banner is already resolved here.  Photo Play 99
       and 2000 must have it off -- driving STATUS bit 5 disturbs their own use of the
       same line -- and I.G.O. 4 and I.G.O. 8 do not want it either, the first being a
       CDONGLE and the second a reader on COM2.  A hand-set value still wins, so an image
       can be run against the wrong part deliberately.

       The ini key stays "hd2001" so rigs staged before this keep working: their
       "hd2001 = 1" still reads as On.  Its label no longer says 2001, because the part
       it switches on is used by every generation from 2001 to Italy. */
    const int hd_opt = device_get_config_int("hd2001");
    const int hd_rel = hd_release_opt(banner);

    dev->hd_probe = (hd_opt < 0) ? (hd_rel >= 0) : (hd_opt != 0);
    if (dev->hd_probe) {
        const int trel = hd_release(banner);

        hd_load(dev, banner);

        /* The picture cipher's key, for the releases that have one.  Without it the
           photographs come back as noise -- the buffer goes in and the same buffer comes
           out -- and I.G.O. 3 does not reach its menu at all, because its boot check
           runs the same round before anything else.  docs/research/31. */
        dev->t_key  = hd_keys[trel].tkey;
        dev->t_init = hd_keys[trel].tinit;
        dev->t_cur  = dev->t_init;
        dev->t_sess_hi = hd_keys[trel].sess_hi;
        dev->t_synth_ident = hd_keys[trel].synth_ident;
        dev->hs_table  = hs_sweep_table(hd_keys[trel].pass1);
        /* Every I.G.O. release's part follows its bursts, as the real ones do -- scored
           against three real boots (I.G.O. 2, 5 and 7) the session layer then agrees on
           all but two of 17,463 reads.  Not 2001: its library hands the part its password
           in another form (the 0x7DF register, docs/research-v2/10.3), which the burst
           tables have not been measured against. */
        dev->hb_gate   = (hd_keys[trel].shape != HD_R2001);
        dev->hb_pw     = ((uint32_t) hd_pass2(hd_keys[trel].pass1) << 16) | hd_keys[trel].pass1;
        dev->hb_n      = -1;
        dev->hb_mode   = HB_SWEEP;
        /* 0x1C selects the 256-word size, so the library addresses the part with eight
           bits; the measured 0x18 selects 64 words and six.  The decoder has to expect
           what the identity it gave promised. */
        dev->hd_abits      = dev->t_synth_ident ? 8 : HD_ABITS;
        if (dev->t_key)
            pp_log("PP: picture cipher key %08X, register %03X (%s)%s\n",
                   dev->t_key, dev->t_init, hd_keys[trel].banner,
                   dev->t_sess_hi ? ", session layer carries bit 7" : "");
        else
            pp_log("PP: no picture cipher on this release -- its pictures are plain\n");
        if (dev->t_synth_ident)
            pp_log("PP: session layer answered by the synthesised rule, identity 0x%02X, not the measured 68BB part\n",
                   dev->t_synth_ident << 1);
    }

    /* Say what all of that resolved to, where the user can see it. */
    {
        const char *src = (have_img && (bi < 0) && (ti < 0)) ? "read from the disk image"
                        : (have_img && ((bi < 0) || (ti < 0))) ? "part image, part pinned by hand"
                                                               : "pinned by hand";
        char        how[96];

        if (!dev->hd_probe)
            snprintf(how, sizeof(how), "no parallel HASP part on the port%s",
                     (hd_opt < 0) ? " (Auto: this release does not use one)" : " (switched off)");
        else if (hd_keys[hd_rel < 0 ? 0 : hd_rel].probe)
            snprintf(how, sizeof(how), "parallel HASP, record key %04X"
                                       " (this release probes; the part refuses 7477)",
                     hd_keys[hd_rel < 0 ? 0 : hd_rel].pass1);
        else
            snprintf(how, sizeof(how), "parallel HASP, record key %04X",
                     hd_keys[hd_rel < 0 ? 0 : hd_rel].pass1);

        /* With nothing read from the image and nothing pinned, the banner is only
           this device's fallback.  Photo Play 2.0 is the case that matters: it has no
           MAIN.SET to be identified from and no dongle either, so saying it reports
           "Version 99 (AT)" invents a machine that is not there.  Only the line about
           the port is true then, and only that is shown. */
        if (!have_img && (bi < 0) && (ti < 0)) {
            if ((how[0] >= 'a') && (how[0] <= 'z'))
                how[0] = (char) (how[0] - ('a' - 'A'));
            snprintf(pp_ident_text, sizeof(pp_ident_text), "%s.", how);
        } else
            snprintf(pp_ident_text, sizeof(pp_ident_text),
                     "Reporting \"%s\" (%s).%c%s.", banner, src, '\n', how);
        pp_log("PP: %s\n", pp_ident_text);
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
    for (int k = 1; k <= 4; k++)
        if (dev->t_mode_rounds[k])
            pp_log("PP: EncodeData mode %d -- %ld rounds\n", k, dev->t_mode_rounds[k]);
    free(dev);
}

static const device_config_t pp_config[] = {
    // clang-format off
    {
        .name           = "identity",
        .description    = pp_ident_text,
        .type           = CONFIG_LABEL,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
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
            { .description = "Photo Play 2001 / I.G.O. 1", .value = 2 },
            { .description = "I.G.O. 2",              .value = 3 },
            { .description = "I.G.O. 3",              .value = 4 },
            { .description = "I.G.O. 4",              .value = 5 },
            { .description = "I.G.O. 5",              .value = 6 },
            { .description = "I.G.O. 6",              .value = 7 },
            { .description = "I.G.O. 7",              .value = 8 },
            { .description = "I.G.O. 8 / I.G.O. Italy", .value = 9 },
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
            { .description = "ZA - South Africa",         .value = 13 },
            { .description = ""                                       }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "ngdata",
        .description    = "NG-DONGLE data-readback sweep (research)",
        .type           = CONFIG_BINARY | CONFIG_HIDDEN,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        /* Named for 2001 because that was the only release using it when it was added;
           every generation from 2001 to Italy does.  The key stays "hd2001" so rigs
           staged before Auto existed keep reading as On. */
        .name           = "hd2001",
        .description    = "Parallel HASP dongle (2001 to 2007, and Italy)",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Auto (on for the releases that have one)", .value = -1 },
            { .description = "Off",                                      .value =  0 },
            { .description = "On",                                       .value =  1 },
            { .description = ""                                                       }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "ngsweep",
        .description    = "NG-DONGLE probe sweep (research)",
        .type           = CONFIG_BINARY | CONFIG_HIDDEN,
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
        .type           = CONFIG_BINARY | CONFIG_HIDDEN,
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
