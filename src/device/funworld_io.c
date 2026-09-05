/*
 * PeepeeBox   A fork of 86Box that emulates the funworld Photo Play / I.G.O.
 *             arcade kiosk hardware, including its protection token.
 *
 *             The funworld I/O card.
 *
 *             An ISA card the cabinets carried, built around an NEC D71055C --
 *             an 8255-compatible PPI, so three 8-bit ports and a control
 *             register at base+3.  A ULN2003 drives what leaves the card
 *             (the coin acceptor's inhibit line, the counters, the lamps); a
 *             74HC14 conditions what arrives; a 74LS682 compares the address
 *             against an 8-way DIP switch, which is what makes the base
 *             address a setting rather than a constant.
 *
 *             Wired to it: a Coin Controls C120 validator on a 10-way IDC, and
 *             the two buttons behind the cabinet door -- one for the operator
 *             setup, one for the touchscreen calibration.
 *
 *             The C120's contract (its manual, section 4.2) is the part that
 *             constrains this: six *separate* accept lines, one per coin, each
 *             an open-collector NPN pulled **low** for 100 ms +/- 20% on a good
 *             coin.  The manual is emphatic that the host must see the line
 *             held, not merely edge-detect it -- "NOT LESS THAN 50 mS" -- so a
 *             coin here is a timer, not a flag poked and cleared.  Anything
 *             shorter is a coin the software will not count, and it would fail
 *             silently, which is the failure mode this cabinet specialises in.
 *
 *             Where it lives was not documented anywhere; the disk was asked
 *             instead.  Booting I.G.O. 7 with PEEPEEBOX_IO_PROBE=00 caught a
 *             resident program at segment 06FC writing control word **0x99** to
 *             **0x213** and then reading 0210 and 0212 and writing 0211 --
 *             which is an 8255 at base 0x210 with, decoded out of 0x99:
 *
 *                 mode 0 throughout, port A input, port B output,
 *                 port C input in both halves.
 *
 *             So the coins and the buttons arrive on A and C, and B is what
 *             drives the ULN2003 -- the acceptor's inhibit line, the counters
 *             and the lamps.  Which *bit* is which is still open, and the map
 *             below is a guess kept in one place until it is not.
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
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/funworld_io.h>

/* The 8255's four registers. */
#define FWIO_PORT_A   0
#define FWIO_PORT_B   1
#define FWIO_PORT_C   2
#define FWIO_CTRL     3
#define FWIO_LEN      4

/* The addresses the software sweeps looking for the card, from a boot traced
   with PEEPEEBOX_IO_TRACE: 0203, 0207, 0233 ... 02F7, every base+3 in the
   range.  The sweep is over 4-byte blocks, so these are the bases. */
#define FWIO_PROBE_FIRST 0x200
#define FWIO_PROBE_LAST  0x2fc

/* How long a coin holds its line.  The C120 says 100 ms +/- 20%, and the host
   is required to want at least 50 ms of it. */
#define FWIO_COIN_MS  100.0

typedef struct fwio_t {
    uint16_t   base;

    uint8_t    ctrl;               /* last control word written           */
    uint8_t    out[3];             /* what the guest last drove outward   */
    uint8_t    in[3];              /* what the card presents to the guest */

    pc_timer_t release[FWIO_IN_LINES];
    uint8_t    held;               /* bitmap of lines currently asserted  */

    pc_timer_t train;              /* a burst of pulses on one line       */
    int        train_left;
    uint8_t    train_port;
    uint8_t    train_bit;
} fwio_t;

static fwio_t *fwio_inst = NULL;

#ifdef ENABLE_FUNWORLD_IO_LOG
int funworld_io_do_log = ENABLE_FUNWORLD_IO_LOG;
#else
int funworld_io_do_log = -1;   /* -1: ask the environment on first use */
#endif

/* Also on with PEEPEEBOX_IO_TRACE, which is what is set when someone is looking
   at this card at all -- having to rebuild to see it answer is a poor trade. */
static void
fwio_log(const char *fmt, ...)
{
    va_list ap;

    if (funworld_io_do_log < 0)
        funworld_io_do_log = (getenv("PEEPEEBOX_IO_TRACE") != NULL);
    if (!funworld_io_do_log)
        return;
    va_start(ap, fmt);
    pclog_ex(fmt, ap);
    va_end(ap);
}

/* ---------------------------------------------------------------- the card */

/* What the inputs read when nothing is happening.

   This is the guess that matters.  A C120's accept output is active low, but the
   74HC14 between it and the 8255 inverts, so whether "no coin" arrives at the
   port as a 1 or a 0 depends on wiring nobody has traced.  Idling high and
   pulsing low found the operator setup on A0 and the CRC check on A1, and
   nothing at all on the other fourteen lines -- which is what a wrong idle level
   looks like: the two lines wired the other way answer, and every line that is
   not has been sitting asserted since boot and so never makes a transition.

   PEEPEEBOX_IO_IDLE=00 rests them low instead, and a pulse then drives high.  An
   environment variable rather than a rebuild, because it is one run either way.

   It does not apply to the two lines already known, and it must not: A0 and A1
   answer a falling edge, so resting them low holds them asserted from power-on.
   The first attempt at idling low went straight into the CRC check before the
   machine had finished booting, because A1 was held down the whole time.  They
   keep the idle they are known to want whatever the experiment is doing. */
#define FWIO_A_KNOWN 0x03          /* A0 the setup button, A1 the CRC check */

static uint8_t fwio_idle = 0xff;

/* PEEPEEBOX_IO_PHASE: only present an asserted line while port B bit 7 is 0 or 1.

   B7 is not idle -- the software drives it as a roughly 3 Hz square wave for the
   whole run, thousands of transitions, which is not what a coin counter or a
   lamp looks like.  A card with a ULN2003 and one spare output line squaring away
   like that is doing one of two things: kicking a watchdog, or **selecting a bank
   of inputs**.  If it is a bank select, then port A means one set of signals when
   B7 is low and another when it is high, and a line held across both phases is
   contradictory rather than asserted -- which would look exactly like what has
   been happening: the card answers, the software polls it twenty thousand times,
   and nothing arrives.

   With this set, an asserted line is presented only in the chosen phase and reads
   idle in the other. */
static int  fwio_phase        = -1;

static uint8_t
fwio_idle_of(uint8_t port)
{
    uint8_t idle = fwio_idle;

    if (port == FWIO_PORT_A)
        idle |= FWIO_A_KNOWN;

    return idle;
}

static void
fwio_idle_all(fwio_t *dev)
{
    for (uint8_t port = 0; port < 3; port++)
        dev->in[port] = fwio_idle_of(port);
}

static void
fwio_reset(fwio_t *dev)
{
    const char *env = getenv("PEEPEEBOX_IO_IDLE");

    if (env != NULL)
        fwio_idle = (uint8_t) strtoul(env, NULL, 16);

    dev->ctrl = 0x9b;              /* all ports input, mode 0 */
    memset(dev->out, 0x00, sizeof(dev->out));
    fwio_idle_all(dev);
    dev->held = 0;
}

static uint8_t
fwio_read(uint16_t port, void *priv)
{
    fwio_t       *dev = (fwio_t *) priv;
    const uint8_t reg = (uint8_t) (port - dev->base);
    uint8_t       ret = 0xff;

    switch (reg) {
        case FWIO_PORT_A:
        case FWIO_PORT_B:
        case FWIO_PORT_C:
            ret = dev->in[reg];
            if ((fwio_phase >= 0) && (reg != FWIO_PORT_B)) {
                const int b7 = (dev->out[FWIO_PORT_B] >> 7) & 1;

                if (b7 != fwio_phase)
                    ret = fwio_idle_of(reg);   /* the other bank: nothing here */
            }
            break;

        case FWIO_CTRL:
            /* An 8255's control register is write-only; a real one leaves the
               bus floating and the host reads 0xFF.  Kept explicit because the
               software's card-detection reads exactly this address. */
            ret = 0xff;
            break;

        default:
            break;
    }

    fwio_log("FWIO: read  %04X (reg %d) = %02X\n", port, reg, ret);
    return ret;
}

static void
fwio_write(uint16_t port, uint8_t val, void *priv)
{
    fwio_t       *dev = (fwio_t *) priv;
    const uint8_t reg = (uint8_t) (port - dev->base);

    switch (reg) {
        case FWIO_PORT_A:
        case FWIO_PORT_B:
        case FWIO_PORT_C:
            dev->out[reg] = val;
            break;

        case FWIO_CTRL:
            if (val & 0x80) {
                /* Mode set: the 8255 clears its output latches. */
                dev->ctrl = val;
                memset(dev->out, 0x00, sizeof(dev->out));
            } else {
                /* Bit set/reset on port C. */
                const uint8_t bit = (val >> 1) & 7;

                if (val & 1)
                    dev->out[FWIO_PORT_C] |= (uint8_t) (1 << bit);
                else
                    dev->out[FWIO_PORT_C] &= (uint8_t) ~(1 << bit);
            }
            break;

        default:
            break;
    }

    fwio_log("FWIO: write %04X (reg %d) = %02X\n", port, reg, val);
}

/* ------------------------------------------------------------- input lines */

/* Port A bit 0 is the operator setup button.  That one is known: it was mapped as
   coin 1 to begin with, and pressing the coin button opened the operator setup
   instead (Marcos, on an I.G.O. 8 rig).  Nothing on port C did anything.

   That arithmetic -- eight lines, two buttons and six coins -- was tried next and
   is wrong.  **A1 starts the CRC check**, not the calibration, and **A2 does
   nothing at all**.  So port A is not six coins in a row after the buttons, and
   the coins are somewhere else; port C is the other input port and the obvious
   place to look.  Marcos, on an I.G.O. 8 rig, 2026-09-05.

   Until the walk says otherwise the coin and calibration entries below are
   placeholders that are known to be wrong, kept only so the buttons have
   somewhere to point.  A0 is the only line here that is real.

   One table, so that correcting it stays a one-line job. */
static const struct {
    uint8_t port;
    uint8_t bit;
} fwio_line_map[FWIO_IN_LINES] = {
    { FWIO_PORT_A, 2 }, /* coin 1 */
    { FWIO_PORT_A, 3 }, /* coin 2 */
    { FWIO_PORT_A, 4 }, /* coin 3 */
    { FWIO_PORT_A, 5 }, /* coin 4 */
    { FWIO_PORT_A, 6 }, /* coin 5 */
    { FWIO_PORT_A, 7 }, /* coin 6 */
    { FWIO_PORT_A, 0 }, /* operator setup button -- confirmed */
    { FWIO_PORT_A, 1 }, /* calibration button                 */
};

static void
fwio_set_bit(fwio_t *dev, uint8_t port, uint8_t bit, int asserted)
{
    const uint8_t mask = (uint8_t) (1 << bit);
    const int     high = !(fwio_idle_of(port) & mask); /* asserted is away from idle */

    if (asserted == high)
        dev->in[port] |= mask;
    else
        dev->in[port] &= (uint8_t) ~mask;
}

static void
fwio_set_line(fwio_t *dev, int line, int asserted)
{
    fwio_set_bit(dev, fwio_line_map[line].port, fwio_line_map[line].bit, asserted);
}

/* PEEPEEBOX_IO_WALK: every pulse steps to the next line of the card rather than
   using the map above -- A0..A7, then B0..B7, then C0..C7, and round again.  One
   button, clicked through, names every line in a single sitting, which beats a
   rebuild per guess.  Each step is logged and shown on screen, so there is no
   click-counting to get wrong.

   Steps the walk refuses to take.  B is an output -- control word 0x99 says so
   and the guest never reads it -- so driving it could only waste clicks.  And A1
   starts the CRC check, which takes the machine away for minutes and says
   nothing new.  That leaves A0, A2..A7 and C0..C7: fifteen clicks. */
/* Twenty-four lines on the card, then the C120's four on COM2 -- the same
   button covers both, because "which of these twenty-eight things is the coin"
   is one question and splitting it across two tools invites losing count. */
#define FWIO_CARD_STEPS 24
#define FWIO_WALK_STEPS (FWIO_CARD_STEPS + C120_LINES)

static int  fwio_walk         = -1;
static int  fwio_walk_at      = 0;
static int  fwio_pin          = -1;   /* PEEPEEBOX_IO_LINE=A6: stay on one line  */
static int  fwio_only_port    = -1;   /* PEEPEEBOX_IO_LINE=C:  walk one port only */

/* PEEPEEBOX_IO_PULSES: how many pulses one click sends.

   The six-separate-lines reading comes from the C120's own manual, and it is
   certainly how a C120 behaves.  But the machine takes a 5 EUR *note* as well as
   coins, so there is a second validator on the loom, and nothing says funworld
   wired either of them one-line-per-denomination rather than counting pulses on
   a single credit line -- which is the other common arrangement and would
   explain totals that decompose into no single denomination.

   One pulse is a C120 coin.  More than one, at the same 100 ms cadence, is the
   pulse-counting reading.  A run each settles which. */
static int  fwio_pulses       = 1;

static char fwio_walk_last[48] = "";

static const uint8_t fwio_walk_skip[FWIO_WALK_STEPS] = {
    0, 1, 0, 0, 0, 0, 0, 0,   /* port A -- A1 is the CRC check, never walked */
    1, 1, 1, 1, 1, 1, 1, 1,   /* port B -- outputs: the mechanical coin counter */
    0, 0, 0, 0, 0, 0, 0, 0,   /* port C */
    0, 0, 0, 0                /* COM2: CTS, DSR, DCD, RI */
};

static void
fwio_train_tick(UNUSED(void *priv))
{
    fwio_t *dev = fwio_inst;

    if (dev == NULL)
        return;

    dev->train_left--;
    if (dev->train_left <= 0) {
        fwio_set_bit(dev, dev->train_port, dev->train_bit, 0);
        pclog("FWIO-WALK: burst finished\n");
        return;
    }

    /* Odd counts are the gaps, even the holds -- an even number of transitions
       per pulse, so the line always finishes released. */
    fwio_set_bit(dev, dev->train_port, dev->train_bit, dev->train_left & 1);
    timer_on_auto(&dev->train, FWIO_COIN_MS * 1000.0);
}

static void
fwio_walk_release(UNUSED(void *priv))
{
    fwio_t *dev = fwio_inst;

    if (dev != NULL) {
        fwio_idle_all(dev);
        pclog("FWIO-WALK: released\n");
    }
}

static void
fwio_release(void *priv)
{
    fwio_t *dev = fwio_inst;
    const int line = (int) (intptr_t) priv;

    if (dev == NULL)
        return;

    fwio_set_line(dev, line, 0);
    dev->held &= (uint8_t) ~(1 << line);
    fwio_log("FWIO: line %d released\n", line);
}

void
funworld_io_pulse(int line)
{
    fwio_t *dev = fwio_inst;

    if ((dev == NULL) || (line < 0) || (line >= FWIO_IN_LINES))
        return;

    if (fwio_walk < 0) {
        const char *line = getenv("PEEPEEBOX_IO_LINE");

        const char *pulses = getenv("PEEPEEBOX_IO_PULSES");

        fwio_walk = (getenv("PEEPEEBOX_IO_WALK") != NULL) || (line != NULL);

        const char *phase = getenv("PEEPEEBOX_IO_PHASE");

        if (phase != NULL)
            fwio_phase = (atoi(phase) != 0);

        if (pulses != NULL) {
            const int n = atoi(pulses);

            if ((n >= 1) && (n <= 64))
                fwio_pulses = n;
        }

        /* "A6" pins one line.  "C" on its own walks that port and nothing else,
           which is how to reach port C without click one being A0 -- A0 opens
           the operator setup, and everything after it then happens in the wrong
           machine state.  That is what spoiled the first port C pass. */
        if ((line != NULL) && (line[0] != '\0')) {
            const int port = (line[0] & ~0x20) - 'A';

            if ((port >= 0) && (port < 3)) {
                if (line[1] == '\0') {
                    fwio_only_port = port;
                } else {
                    const int bit = line[1] - '0';

                    if ((bit >= 0) && (bit < 8))
                        fwio_pin = (port * 8) + bit;
                }
            }
        }
    }

    /* Only the coin button is hijacked.  The setup and calibration buttons keep
       working off the map, because reaching the operator setup is how you get the
       machine into the state where the one positive result so far happened -- and
       until now turning the walk on took that away, since every button went to
       the same stepper. */
    if (fwio_walk && (line == FWIO_LINE_COIN1)) {
        uint8_t port;
        uint8_t bit;

        /* PEEPEEBOX_IO_LINE=A6 pins the walk to one line, so the button pulses
           the same thing every click.  "It credited once and never again" is a
           different fault from "it credited once because I only pressed it once",
           and stepping past the line is no way to tell them apart. */
        if (fwio_pin >= 0)
            fwio_walk_at = fwio_pin;
        else if (fwio_only_port >= 0) {
            if ((fwio_walk_at / 8) != fwio_only_port)
                fwio_walk_at = fwio_only_port * 8;
        }

        for (int guard = 0; fwio_walk_skip[fwio_walk_at] && (guard < FWIO_WALK_STEPS); guard++)
            fwio_walk_at = (fwio_walk_at + 1) % FWIO_WALK_STEPS;

        /* Past the card's own lines, the walk is on the validator instead. */
        if (fwio_walk_at >= FWIO_CARD_STEPS) {
            const int sline = fwio_walk_at - FWIO_CARD_STEPS;

            snprintf(fwio_walk_last, sizeof(fwio_walk_last), "COM2 %s  (C120)",
                     coin_c120_line_name(sline));
            pclog("FWIO-WALK: %s\n", fwio_walk_last);
            coin_c120_pulse(sline);

            fwio_walk_at = (fwio_walk_at + 1) % FWIO_WALK_STEPS;
            if ((fwio_only_port >= 0) && ((fwio_walk_at / 8) != fwio_only_port))
                fwio_walk_at = fwio_only_port * 8;
            return;
        }

        port = (uint8_t) (fwio_walk_at / 8);
        bit  = (uint8_t) (fwio_walk_at % 8);

        fwio_idle_all(dev);
        fwio_set_bit(dev, port, bit, 1);

        dev->train_left = 0;
        if (fwio_pulses > 1) {
            /* One transition already made; the tick alternates for the rest and
               always lands released. */
            dev->train_port = port;
            dev->train_bit  = bit;
            dev->train_left = (fwio_pulses * 2) - 1;
            timer_on_auto(&dev->train, FWIO_COIN_MS * 1000.0);
        }
        if (fwio_pulses > 1)
            snprintf(fwio_walk_last, sizeof(fwio_walk_last), "port %c bit %d  x%d",
                     (char) ('A' + port), bit, fwio_pulses);
        else
            snprintf(fwio_walk_last, sizeof(fwio_walk_last), "port %c bit %d  (idle %02X)",
                     (char) ('A' + port), bit, fwio_idle);
        pclog("FWIO-WALK: %s held %g ms\n", fwio_walk_last, FWIO_COIN_MS);
        timer_on_auto(&dev->release[0], FWIO_COIN_MS * 1000.0);
        fwio_walk_at = (fwio_walk_at + 1) % FWIO_WALK_STEPS;
        if ((fwio_only_port >= 0) && ((fwio_walk_at / 8) != fwio_only_port))
            fwio_walk_at = fwio_only_port * 8;
        return;
    }

    /* A second coin while the first is still on the wire is not a thing the
       validator can do -- it holds the line for 100 ms and will not start
       another until it lets go.  Restarting the timer instead of stacking
       keeps that true. */
    /* A coin is the validator's business, not the card's. */
    if ((line <= FWIO_LINE_COIN6) && coin_c120_present()) {
        coin_c120_pulse(C120_LINE_CTS);
        return;
    }

    fwio_set_line(dev, line, 1);
    dev->held |= (uint8_t) (1 << line);
    timer_on_auto(&dev->release[line], FWIO_COIN_MS * 1000.0);
    fwio_log("FWIO: line %d asserted for %g ms\n", line, FWIO_COIN_MS);
}

int
funworld_io_present(void)
{
    return fwio_inst != NULL;
}

/* -------------------------------------------------------------- the device */

static void *
fwio_init(const device_t *info)
{
    fwio_t *dev = (fwio_t *) calloc(1, sizeof(fwio_t));

    if (dev == NULL)
        return NULL;

    dev->base = (uint16_t) device_get_config_hex16("base");
    if (fwio_walk < 0)
        fwio_walk = (getenv("PEEPEEBOX_IO_WALK") != NULL);
    fwio_reset(dev);

    timer_add(&dev->train, fwio_train_tick, NULL, 0);

    for (int i = 0; i < FWIO_IN_LINES; i++)
        timer_add(&dev->release[i],
                  (getenv("PEEPEEBOX_IO_WALK") != NULL) ? fwio_walk_release : fwio_release,
                  (void *) (intptr_t) i, 0);

    io_sethandler(dev->base, FWIO_LEN, fwio_read, NULL, NULL,
                  fwio_write, NULL, NULL, dev);

    fwio_inst = dev;
    fwio_log("FWIO: %s at %04X-%04X\n", info->name, dev->base,
             dev->base + FWIO_LEN - 1);
    return dev;
}

static void
fwio_close(void *priv)
{
    fwio_t *dev = (fwio_t *) priv;

    if (dev == NULL)
        return;

    io_removehandler(dev->base, FWIO_LEN, fwio_read, NULL, NULL,
                     fwio_write, NULL, NULL, dev);
    free(dev);
    fwio_inst = NULL;
}

static const device_config_t fwio_config[] = {
  // clang-format off
    {
        /* The card's own DIP switch, which is the only reason this is a
           setting.  The default is a guess until a disk says otherwise. */
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x210,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x200", .value = 0x200 },
            { .description = "0x210 (what the disks use)", .value = 0x210 },
            { .description = "0x230", .value = 0x230 },
            { .description = "0x240", .value = 0x240 },
            { .description = "0x250", .value = 0x250 },
            { .description = "0x260", .value = 0x260 },
            { .description = "0x270", .value = 0x270 },
            { .description = "0x280", .value = 0x280 },
            { .description = "0x290", .value = 0x290 },
            { .description = "0x2A0", .value = 0x2a0 },
            { .description = "0x2B0", .value = 0x2b0 },
            { .description = "0x2C0", .value = 0x2c0 },
            { .description = "0x2D0", .value = 0x2d0 },
            { .description = "0x2E0", .value = 0x2e0 },
            { .description = "0x2F0", .value = 0x2f0 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t funworld_io_device = {
    .name          = "funworld I/O card (8255)",
    .internal_name = "funworld_io",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = fwio_init,
    .close         = fwio_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = fwio_config
};

/* --------------------------------------------------------------- the probe */

/* Where does the card belong?  The DIP switch decided on a real cabinet, and
   the disk was set up to match, so the answer is in the image rather than in
   any document -- and the software will say it out loud if asked.  It sweeps
   the control register of every 4-byte block hunting for the card; this claims
   all of them at once and reports what it is asked and what it answered.
   Whichever block the software goes on to talk to after the sweep -- the one it
   reads base+0..2 from -- is the base address.

   PEEPEEBOX_IO_PROBE is the byte the sweep is answered with, in hex, because
   what the software will accept as "card here" is exactly the unknown.  One run
   per candidate answer, watching where the sweep stops.  This is a diagnostic,
   not part of a cabinet: it exists to be deleted once the base is known. */
static uint8_t fwio_probe_answer = 0x00;

static uint8_t
fwio_probe_read(uint16_t port, UNUSED(void *priv))
{
    pclog("FWIO-PROBE: %04X read -> %02X\n", port, fwio_probe_answer);
    return fwio_probe_answer;
}

static void
fwio_probe_write(uint16_t port, uint8_t val, UNUSED(void *priv))
{
    pclog("FWIO-PROBE: %04X written %02X\n", port, val);
}

void
funworld_io_probe_init(void)
{
    const char *env = getenv("PEEPEEBOX_IO_PROBE");

    if (env == NULL)
        return;

    fwio_probe_answer = (uint8_t) strtoul(env, NULL, 16);
    pclog("FWIO-PROBE: answering every control register %04X..%04X with %02X\n",
          FWIO_PROBE_FIRST + FWIO_CTRL, FWIO_PROBE_LAST + FWIO_CTRL,
          fwio_probe_answer);

    for (uint16_t base = FWIO_PROBE_FIRST; base <= FWIO_PROBE_LAST; base += 4) {
        /* Only the control register.  Claiming whole blocks would sit on the
           sound card and the game port and break the boot before the sweep
           ever runs. */
        io_sethandler((uint16_t) (base + FWIO_CTRL), 1,
                      fwio_probe_read, NULL, NULL,
                      fwio_probe_write, NULL, NULL, NULL);
    }
}

/* For the UI: is the walk on, and which line did the last click hold?  The
   answer belongs on screen -- counting clicks against a comment in a batch file
   is exactly the kind of bookkeeping that produces a wrong answer. */
int
funworld_io_walk_state(char *out, size_t len)
{
    if (fwio_walk <= 0)
        return 0;
    snprintf(out, len, "%s", fwio_walk_last[0] ? fwio_walk_last : "not started");
    return 1;
}
