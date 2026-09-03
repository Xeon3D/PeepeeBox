/*
 * PeepeeBox - Funny's Interactive Playworld protection dongle emulation.
 *
 * A different cabinet from the same corner of the arcade world: OrgaControl Systemhaus
 * built it for Funny's Planet International GmbH in 2001, and it shares the Photo Play
 * chassis, the funworld FN_* portal suite and even the `UPDATE\CHECK.BAT` that hunts for
 * `pp2000.bat` on removable media.  What it does not share is the token.
 *
 * `GAME\FSYSTEM.EXE` links Aladdin's obfuscated DOS library -- the HASPDOSDRV device
 * name is in the binary, and the call sites use the classic
 *
 *     hasp(service, seed, lptnum, pass1, pass2, &p1, &p2, &p3, &p4)
 *
 * with the prologue at CS:0xBBF5 mapping the frame onto AX = seed, BH = service,
 * BL = lptnum, CX = pass1, DX = pass2 and returning AX->p1, BX->p2, CX->p3, DX->p4.
 * Four gates run before the machine will start, all of them at boot:
 *
 *     service 1  IsHasp                p1 must be non-zero
 *     service 6  dongle ID             p3 must be zero
 *     service 3  read words 0..5       must spell "ORGACONTROL "
 *     service 4  write word            used at runtime for the play counters
 *
 * Fail any of them and FSYSTEM.EXE prints COPYPROTECTION ... and drops into an endless
 * Sound(800)/Delay(500)/NoSound/Delay(300) loop.
 *
 * UNDERNEATH THE ALADDIN WRAPPER IT IS NOT A HASP.  Running the real library offline
 * (Inst/scripts/p2-haspsim.py) and reading its decrypted transport shows two layers on
 * one port:
 *
 *   1. A HASP-4 style presence handshake -- the C6 C7 C6 80 wake, then fifteen clocked
 *      bytes, then a descending even ramp with one STATUS read per step.  This is
 *      exactly what upstream 86Box's src/device/hasp.c models for Savage Quest, and the
 *      state machine below is a port of it.  The handshake is byte-identical for every
 *      service, seed, password and address: nothing parameter-dependent reaches the wire
 *      until the library believes something is there.
 *
 *   2. Behind it, plain Microwire to a 93Cxx serial EEPROM, on the same pins the funworld
 *      2001 HDONGLE uses (dongle_photoplay.c):
 *
 *          CS = DATA bit 1, SK = DATA bit 5 (rising edge), DI = DATA bit 6,
 *          DO = STATUS bit 5 -- which the library also samples as bit 7 inverted.
 *
 *      Nine address bits, `1 1 0` = READ, sixteen data bits out MSB first.  The client
 *      biases the address by 8 and descrambles with its own first password:
 *
 *          returned(n) = raw[n + 8] XOR pass1 XOR n,      pass1 = 0x43B5
 *
 *      the same shape as the 2001 part, arrived at independently from a different
 *      product.  So the record is stored pre-scrambled and the guest's own unscramble
 *      hands it back the text.
 *
 * The passwords are not literals in the guest: CS:0x02F7 stores them times 1024 as
 * longints (0x010ED400 and 0x01652800) and divides them down through Borland's LongDiv,
 * giving pass1 = 0x43B5 and pass2 = 0x594A.
 *
 * Full derivation in Inst/Docs/02-hasp-wire.md; Inst/scripts/p2-verify.py drives the real
 * library against this same model and checks every gate.
 *
 * Authors:    The HUEG PP team.
 *
 *             Released under the GNU General Public License version 2 or later.
 *             See COPYING for more information.
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
#include <86box/lpt.h>

/* Bring-up: log the exchange.  Cheap -- a boot produces a few dozen lines. */
#define ENABLE_DONGLE_FUNNY_LOG 1

/* ---- the Microwire part ------------------------------------------------------------ */

#define FN_CS     0x02 /* DATA bit 1 */
#define FN_SK     0x20 /* DATA bit 5 */
#define FN_DI     0x40 /* DATA bit 6 */
#define FN_DO     0x20 /* STATUS bit 5 */
#define FN_BUSY   0x80 /* STATUS bit 7, which the library reads inverted */

#define FN_ABITS  9    /* measured: 8 gives addr = 4 + n/2, 9 gives addr = 8 + n */
#define FN_WORDS  512
#define FN_BIAS   8    /* the library adds 8 to the word the caller asked for */
#define FN_PASS1  0x43B5
#define FN_PASS2  0x594A

/* The record FSYSTEM.EXE demands, built on its stack at CS:0x030F as six words
   4F52 4741 434F 4E54 524F 4C20 -- "ORGACONTROL " in big-endian words. */
#define FN_SYSCODE "ORGACONTROL "

enum {
    FN_IDLE = 0,
    FN_OP,
    FN_ADDR,
    FN_READ,
    FN_WRITE,
    FN_DONE
};

/* ---- the HASP-4 presence handshake -------------------------------------------------- */

enum {
    HS_NONE = 0,
    HS_PW_BEGIN,
    HS_PW_END,
    HS_READ
};

/* Ported from hasp.c.  The values that answer with STATUS bit 5 set; the rest answer
   clear.  What makes the client accept is the *phase*, which the password length sets --
   fourteen.  The byte values are compared but a mismatch is not fatal here, and this
   client's own preamble is fifteen bytes long, so the comparison never matches and does
   not need to. */
#define HS_PASS_LEN 14

static int
hs_answers(uint8_t val)
{
    switch (val) {
        case 0x88: case 0x94: case 0x98: case 0x9C: case 0x9E: case 0xA0:
        case 0xA4: case 0xAA: case 0xAE: case 0xB0: case 0xB2: case 0xBC:
        case 0xBE: case 0xC2: case 0xC6: case 0xC8: case 0xCE: case 0xD0:
        case 0xD6: case 0xD8: case 0xDC: case 0xE0: case 0xE6: case 0xEA:
        case 0xEE: case 0xF2: case 0xF6:
            return 1;
        default:
            return 0;
    }
}

typedef struct {
    void *lpt;

    /* presence handshake */
    int     hs_index;
    int     hs_state;
    int     hs_passn;
    uint8_t hs_status;

    /* Microwire */
    uint8_t  prev_data;
    int      mw_state;
    int      mw_n;
    int      mw_op;
    uint16_t mw_addr;
    uint16_t mw_sr;
    int      mw_do;
    int      mw_driving; /* ours from the first read bit until CS drops */
    int      mw_wen;
    int      mw_reads;
    int      mw_writes;
    uint16_t mem[FN_WORDS];
} fn_t;

#ifdef ENABLE_DONGLE_FUNNY_LOG
int dongle_funny_do_log = ENABLE_DONGLE_FUNNY_LOG;

static void
fn_log(const char *fmt, ...)
{
    va_list ap;

    if (dongle_funny_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define fn_log(fmt, ...)
#endif

/* Put `value` where a client reading word `n` will see it. */
static void
fn_store(fn_t *dev, int n, uint16_t value)
{
    const int at = (n + FN_BIAS) % FN_WORDS;

    dev->mem[at] = (uint16_t) (value ^ FN_PASS1 ^ (uint16_t) n);
}

static uint16_t
fn_fetch(const fn_t *dev, int n)
{
    const int at = (n + FN_BIAS) % FN_WORDS;

    return (uint16_t) (dev->mem[at] ^ FN_PASS1 ^ (uint16_t) n);
}

static void
fn_load_record(fn_t *dev, const char *code, uint16_t word9)
{
    const size_t len = strlen(code);

    memset(dev->mem, 0, sizeof(dev->mem));
    /* Zero plaintext everywhere, so an unpopulated word reads as 0 rather than as
       whatever the scramble of an empty part happens to be. */
    for (int n = 0; n < FN_WORDS - FN_BIAS; n++)
        fn_store(dev, n, 0x0000);

    for (size_t i = 0; i < len; i += 2) {
        const uint8_t hi = (uint8_t) code[i];
        const uint8_t lo = (uint8_t) ((i + 1 < len) ? code[i + 1] : ' ');

        fn_store(dev, (int) (i / 2), (uint16_t) ((hi << 8) | lo));
    }

    /* Word 9 is what the guest hands FUNNY.DLL through INT 50h AX=1235.  What it is for
       is not yet known -- surface D -- so it is a knob rather than a constant. */
    fn_store(dev, 9, word9);

    fn_log("FN: record loaded, \"%s\"; words 0..5 read back %04X %04X %04X %04X %04X %04X,"
           " word 9 = %04X\n", code, fn_fetch(dev, 0), fn_fetch(dev, 1), fn_fetch(dev, 2),
           fn_fetch(dev, 3), fn_fetch(dev, 4), fn_fetch(dev, 5), fn_fetch(dev, 9));
}

/* ---- wire ------------------------------------------------------------------------- */

static void
fn_mw_write_data(fn_t *dev, uint8_t val)
{
    const int sel = (val & FN_CS) != 0;
    const int clk = (val & FN_SK) != 0;
    const int dat = (val & FN_DI) != 0;
    const int was = (dev->prev_data & FN_SK) != 0;

    if (!sel) {
        dev->mw_state   = FN_IDLE;
        dev->mw_n       = 0;
        dev->mw_driving = 0;
        return;
    }
    if (!(dev->prev_data & FN_CS)) { /* CS just rose: a new frame */
        dev->mw_state = FN_IDLE;
        dev->mw_n     = 0;
    }
    if (!clk || was)
        return;

    switch (dev->mw_state) {
        case FN_IDLE:
            if (dat) { /* start bit; leading zeros are not one */
                dev->mw_state = FN_OP;
                dev->mw_n     = 0;
                dev->mw_op    = 0;
            }
            break;

        case FN_OP:
            dev->mw_op = (dev->mw_op << 1) | dat;
            if (++dev->mw_n == 2) {
                dev->mw_state = FN_ADDR;
                dev->mw_n     = 0;
                dev->mw_addr  = 0;
            }
            break;

        case FN_ADDR:
            dev->mw_addr = (uint16_t) ((dev->mw_addr << 1) | dat);
            if (++dev->mw_n == FN_ABITS) {
                dev->mw_n    = 0;
                dev->mw_addr = (uint16_t) (dev->mw_addr % FN_WORDS);
                switch (dev->mw_op) {
                    case 2: /* READ */
                        dev->mw_sr      = dev->mem[dev->mw_addr];
                        dev->mw_state   = FN_READ;
                        dev->mw_driving = 1;
                        if (dev->mw_reads++ < 24)
                            fn_log("FN: read word %03X -> %04X (guest sees %04X)\n",
                                   dev->mw_addr, dev->mw_sr,
                                   (uint16_t) (dev->mw_sr ^ FN_PASS1
                                               ^ (uint16_t) (dev->mw_addr - FN_BIAS)));
                        break;
                    case 1: /* WRITE */
                        dev->mw_sr    = 0;
                        dev->mw_state = FN_WRITE;
                        break;
                    case 3: /* ERASE */
                        if (dev->mw_wen)
                            dev->mem[dev->mw_addr] = 0xFFFF;
                        dev->mw_do    = 1;
                        dev->mw_state = FN_DONE;
                        break;
                    default: /* EWEN / EWDS / ERAL / WRAL, told apart by the top address bits */
                        dev->mw_wen   = (dev->mw_addr >> (FN_ABITS - 2)) == 3;
                        dev->mw_state = FN_DONE;
                        break;
                }
            }
            break;

        case FN_READ:
            /* The host clocks low, high, low and only then samples, so the bit has to be
               on the line from this edge on.  Sixteen of them, MSB first.  DO stays ours
               until CS drops: releasing it after the sixteenth edge hands the last bit
               back to the handshake layer and the record reads "OS GA" instead of
               "OR GA". */
            if (dev->mw_n < 16)
                dev->mw_do = (dev->mw_sr >> (15 - dev->mw_n)) & 1;
            dev->mw_n++;
            break;

        case FN_WRITE:
            dev->mw_sr = (uint16_t) ((dev->mw_sr << 1) | dat);
            if (++dev->mw_n == 16) {
                /* The counters at word 13+ are written every game.  Take the write
                   whether or not an EWEN was seen: this part has no write-protect pin
                   and refusing silently would leave the guest polling a busy flag. */
                dev->mem[dev->mw_addr] = dev->mw_sr;
                if (dev->mw_writes++ < 24)
                    fn_log("FN: write word %03X <- %04X (guest wrote %04X)\n",
                           dev->mw_addr, dev->mw_sr,
                           (uint16_t) (dev->mw_sr ^ FN_PASS1
                                       ^ (uint16_t) (dev->mw_addr - FN_BIAS)));
                dev->mw_do    = 1; /* ready */
                dev->mw_state = FN_DONE;
            }
            break;

        default:
            break;
    }
}

static void
fn_hs_write_data(fn_t *dev, uint8_t val)
{
    switch (dev->hs_index) {
        case 0:
            dev->hs_index = (val == 0xC6) ? 1 : 0;
            break;
        case 1:
            dev->hs_index = (val == 0xC7) ? 2 : 0;
            break;
        case 2:
            if (val == 0xC6) {
                dev->hs_index = 3;
            } else {
                dev->hs_index = 0;
                dev->hs_state = HS_NONE;
            }
            break;
        case 3:
            dev->hs_index = 0;
            if (val == 0x80) {
                dev->hs_state = HS_PW_BEGIN;
                dev->hs_passn = 0;
                return; /* status deliberately left alone */
            }
            break;
        default:
            break;
    }

    dev->hs_status = 0;

    if (dev->hs_state == HS_READ) {
        if (hs_answers(val))
            dev->hs_status = FN_DO;
    } else if (dev->hs_state == HS_PW_END) {
        if (val & 1)
            dev->hs_state = HS_READ;
    } else if ((dev->hs_state == HS_PW_BEGIN) && (val & 1)) {
        if (++dev->hs_passn == HS_PASS_LEN) {
            dev->hs_state = HS_PW_END;
            dev->hs_passn = 0;
        }
    }
}

static void
fn_write_data(uint8_t val, void *priv)
{
    fn_t *dev = (fn_t *) priv;

    fn_hs_write_data(dev, val);
    fn_mw_write_data(dev, val);
    dev->prev_data = val;
}

static uint8_t
fn_read_status(void *priv)
{
    fn_t   *dev = (fn_t *) priv;
    uint8_t st  = dev->hs_status;

    /* While the part is clocking a word out it owns DO.  BUSY (bit 7) carries the same
       level inverted, because the library probes the line both ways -- bit 5 straight
       and bit 7 complemented -- and gets the same answer either way. */
    if (dev->mw_driving) {
        st = (uint8_t) ((st & ~FN_DO) | (dev->mw_do ? FN_DO : 0));
        st = (uint8_t) ((st & ~FN_BUSY) | (dev->mw_do ? 0 : FN_BUSY));
    }

    return st;
}

static void
fn_write_ctrl(uint8_t val, void *priv)
{
    fn_t *dev = (fn_t *) priv;

    /* The library resets the link by toggling CTRL bits 2 and 3.  Drop the handshake
       state so a restarted probe starts clean; the EEPROM contents survive. */
    (void) val;
    dev->hs_index = 0;
    dev->hs_state = HS_NONE;
    dev->hs_passn = 0;
}

/* ---- device ------------------------------------------------------------------------ */

static void *
fn_init(const device_t *info)
{
    fn_t *dev = calloc(1, sizeof(fn_t));

    if (dev == NULL)
        return NULL;

    dev->hs_status = 0x80;
    dev->prev_data = 0xFF;
    dev->mw_do     = 1;

    const char *w9 = device_get_config_string("word9");

    fn_load_record(dev, FN_SYSCODE,
                   (uint16_t) (w9 ? strtoul(w9, NULL, 16) : 0));

    dev->lpt = lpt_attach(fn_write_data, fn_write_ctrl, NULL, fn_read_status, NULL,
                          NULL, NULL, dev);

    /* A plain SPP port: bit 5 of CTRL is a don't-care and the data lines are always
       driven.  86Box otherwise treats bit 5 as the bidirectional direction bit and
       suppresses write_data -- and this guest leaves it set. */
    if (dev->lpt != NULL)
        lpt_set_ext((lpt_t *) dev->lpt, 0);

    fn_log("FN: Funny's Interactive Playworld dongle attached; passwords %04X/%04X,"
           " %d-bit addressing, word bias %d\n", FN_PASS1, FN_PASS2, FN_ABITS, FN_BIAS);

    return dev;
}

static void
fn_close(void *priv)
{
    fn_t *dev = (fn_t *) priv;

    fn_log("FN: closing after %d reads, %d writes\n", dev->mw_reads, dev->mw_writes);
    free(dev);
}

/* A free-form 16-bit hex value rather than a spinner: device_config_spinner_t's bounds
   are int16_t, so a spinner cannot express the top half of the range, and word 9 is a
   whole word whose meaning is not yet known. */
static const device_config_t fn_config[] = {
  // clang-format off
    {
        .name           = "word9",
        .description    = "EEPROM word 9, hex (the game reads it through INT 50h AX=1235)",
        .type           = CONFIG_STRING,
        .default_string = "0000",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t lpt_dongle_funny_device = {
    .name          = "Protection Dongle for Funny's Interactive Playworld",
    .internal_name = "dongle_funny",
    .flags         = DEVICE_LPT | DEVICE_HOTPLUG,
    .local         = 0,
    .init          = fn_init,
    .close         = fn_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = fn_config
};
