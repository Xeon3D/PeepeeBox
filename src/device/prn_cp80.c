/*
 * PeepeeBox   A fork of 86Box that emulates the funworld Photo Play / I.G.O.
 *             arcade kiosk hardware, including its protection token.
 *
 *             The cabinet's receipt printer, on a serial port.
 *
 *             Where it comes from: the I/O card's DB15 has two pins that join a
 *             DB9 whose other three go to a serial port (Marcos, 2026-09-05).
 *             Three wires to a UART is TxD, RxD and ground -- the minimum for a
 *             serial device -- and the card's two are the 12 V and ground that
 *             power it.  So the printer is an ordinary RS-232 peripheral that
 *             happens to be fed from the I/O card, and nothing about it touches
 *             the 8255.
 *
 *             It does **not** speak a printer dialect.  That was the first
 *             guess -- a CP80 is a dot matrix printer of the right era and those
 *             are Epson ESC/P almost without exception -- and MENU.EXE says
 *             otherwise.  The wire carries funworld's own framing to a box the
 *             software calls the Dataprint:
 *
 *               the unit sends ENQ (05) continuously, as a keepalive
 *               the host sends XON, ESC S, XOFF, ETX, LF LF
 *               the unit answers with a line whose byte 15 is 'C'
 *               the host then sends the report, and a checksum trailer
 *
 *             The report's *text* is 24 columns.  The formatter at 0x1D762
 *             copies 24 characters and appends an LF -- 25 bytes per record --
 *             and filters everything outside 0x20..0x3F, 0x41..0x5A and
 *             0x61..0x7A to a space.
 *
 *             But that formatter is not the only thing that fills the buffer.
 *             The first real report began **ESC K**, so something writes escape
 *             sequences in alongside the filtered text, and the Dataprint is
 *             very likely passing them to whatever printer it drives.  Which
 *             means a printer manual may decode part of this after all -- just
 *             not the framing above, which is funworld's own.
 *
 *             So nothing here skips data on a guess about which dialect those
 *             sequences belong to.  Every control sequence is named in a trace,
 *             and an unrecognised one costs a line rather than the payload.
 *
 * Authors:    The HUEG PP team.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#include <stdarg.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/serial.h>
#include <86box/sound.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/photoplay.h>
#include <86box/prn_cp80.h>

/* Enough for a very long session; past it the buffer stops growing and says so
   once, because a printer that has run away should cost a line rather than the
   host's memory. */
#define CP80_PAPER_MAX (4u * 1024u * 1024u)
#define CP80_TRACE_MAX (1u * 1024u * 1024u)

#define CP80_TABS      8

/* The DPU-414 is not a stationary line-thermal printer.  Its nine-dot thermal
   head rides across the paper on a carriage.  SII specifies 52.5 normal
   characters/second, with seven dot columns and one inter-character column;
   that makes the head motor's high-speed step rate 52.5 * 8 = 420 Hz.  The
   NSK recording's narrow 421--422 Hz tone lands directly on that prediction;
   other units in the recordings run a little slower, around 401--407 Hz.

   A normal line is nine printed dots plus the manual's default six-dot line
   space.  In the clean portable-printer recording each such advance lasts about
   0.20 seconds while its geared motor produces a 150 Hz pulse train, or about
   thirty audible phase transitions for fifteen dots of paper travel.  The
   service schematic exposes four phase outputs for each motor (CM1--CM4 and
   FM1--FM4), although SII does not publish the paper motor's drive sequence. */
#define CP80_HEAD_STEP_HZ  420.0
#define CP80_FEED_STEP_HZ  150.0
#define CP80_CHAR_STEPS       8
#define CP80_FEED_STEPS      30

#define CP80_SOUND_Q        256

enum {
    CP80_SOUND_IDLE = 0,
    CP80_SOUND_HEAD,
    CP80_SOUND_FEED
};

typedef struct cp80_sound_event_t {
    uint16_t columns;              /* carriage's logical-seek distance */
    uint16_t ink;                  /* non-blank cells: a small load change */
    uint16_t speed;                /* 1000 at full battery speed */
} cp80_sound_event_t;

typedef struct cp80_sound_t {
    cp80_sound_event_t q[CP80_SOUND_Q];
    int                q_head;
    int                q_tail;
    int                button_pending;
    int                tear_pending;

    int      on;
    int      mode;
    int      direction;
    int      head_steps;
    int      head_total;
    int      feed_steps;
    double   head_phase;
    double   feed_phase;
    double   head_hz;
    double   feed_hz;
    double   head_load;

    int      rate;
    double   body1_c;
    double   body1_r2;
    double   body1_z1;
    double   body1_z2;
    double   body2_c;
    double   body2_r2;
    double   body2_z1;
    double   body2_z2;
    double   noise_lp;

    /* The two front-panel pushbuttons are mechanical even when the printer is
       off.  Their snap is mixed independently of the motor event queue so an
       ON LINE or FEED press is heard immediately, including during printing. */
    int      button_noise_samples;
    int      button_noise_total;
    double   button_noise_a;
    double   button_noise_z;
    double   button1_c;
    double   button1_r2;
    double   button1_z1;
    double   button1_z2;
    double   button2_c;
    double   button2_r2;
    double   button2_z1;
    double   button2_z2;

    /* Tearing is a separate, asynchronous sheet event.  A broad rubbing bed
       carries irregular short fibre failures, followed by the lower, softer
       flutter of the released receipt.  It may overlap a motor already in the
       queue, just as an operator can tear the exposed part while data remains. */
    int      tear_samples;
    int      tear_total;
    int      tear_next;
    int      tear_burst_samples;
    int      tear_burst_total;
    int      tear_final;
    double   tear_burst_gain;
    double   tear_hp_a;
    double   tear_lp_a;
    double   tear_flutter_a;
    double   tear_hp_z;
    double   tear_lp_z;
    double   tear_flutter_z;

    /* The roll is its own sound source, rather than more motor harmonics.  Two
       one-pole filters colour microscopic sliding friction; three short modal
       resonators give the exposed 64-micron sheet its measured flex/rattle
       bands.  All are driven from the same deterministic generator. */
    double   paper_hp_a;
    double   paper_lp_a;
    double   paper_texture_a;
    double   paper_hp_z;
    double   paper_lp_z;
    double   paper_texture_z;
    double   paper_speed;
    double   paper_gain;
    double   paper1_c;
    double   paper1_r2;
    double   paper1_z1;
    double   paper1_z2;
    double   paper2_c;
    double   paper2_r2;
    double   paper2_z1;
    double   paper2_z2;
    double   paper3_c;
    double   paper3_r2;
    double   paper3_z1;
    double   paper3_z2;
    uint32_t rng;
} cp80_sound_t;

typedef struct cp80_buf_t {
    char  *s;
    size_t len;
    size_t cap;
    int    full;                   /* hit the cap and said so */
} cp80_buf_t;

/* COM2, and not a setting.  MENU.EXE programs 0x2F8 by hand and holds no other
   port as an immediate, so there is nothing to choose between.  There was a
   dropdown here while that was still open; a control whose only correct value
   is the default is a way to break a working setup. */
#define CP80_PORT      1           /* zero-based: COM2 */
#define CP80_PORTS_MAX 1

typedef struct cp80_port_t {
    struct cp80_t *dev;
    serial_t      *serial;
    int            port;
    int            seen;           /* a byte has arrived here          */
    int            announced;      /* the first ENQ has been logged    */
} cp80_port_t;

/* The Dataprint announces itself, and until it does the software will not
   believe it is there.

   From MENU.EXE on the I.G.O. 6 image, disassembled at file offset 0x1D0E5 and
   after.  It programs COM2 directly -- LCR 0x2FB gets 0x80 for DLAB, divisor
   12 into 0x2F8/0x2F9, then LCR 0x03 -- which is 115200/12 = **9600 baud, 8N1
   on 0x2F8**.  Then the whole of its detection is:

       call read_LSR (in al, 0x2FD) ; test al, 1   -- a byte waiting?
       call read_RBR (in al, 0x2F8) ; cmp  al, 5   -- is it 05?
       jne  -> return, leaving "Connect the interfaces of the Dataprint"

   So the device sends **ENQ (0x05)** to the host unprompted, and the host looks
   for one already sitting in the receive register.  A printer that only ever
   listens is never found, which is exactly what was happening.

   It is sent continuously, and that is not a guess either.  The send loop at
   0x1D47D does this after every frame it writes:

       call read_LSR ; test al, 1 ; je skip
       call read_RBR ; cmp  al, 5 ; jne skip
       mov  dword [timeout], 0        -- an ENQ resets the watchdog
   skip:
       cmp  dword [timeout], 0x5DC    -- 1500 without one and it gives up

   So ENQ is a keepalive for the whole exchange, not a hello.  The first version
   of this hushed for two seconds whenever the guest sent us something, on the
   reasonable-sounding theory that a device would not chatter over an incoming
   print job -- which stopped the announcements at precisely the moment the
   watchdog started counting.  The guest timed out, retried, timed out, retried,
   and gave up after three attempts.  That is what the first capture was: the
   same seven-byte frame three times and no print at all.

   PEEPEEBOX_PRN_ENQ=0 turns it off, which is how to check that this is really
   the mechanism rather than something that merely correlates with it. */
#define CP80_ENQ      0x05
#define CP80_ENQ_MS   100.0

/* The reply the unit owes the host after a command frame.

   The protocol engine is the state machine at 0x1E8FC in MENU.EXE.  Once the
   frame is away it enters a receive state at 0x1EA4A that stores every byte
   arriving into a buffer until it sees **LF**, and then at 0x1EA81:

       mov al, [bp-0x4AF]     -- that is buffer index 15
       mov [bp-0x39], al
   ... 0x1EAEA:
       cmp byte [bp-0x39], 0x43    -- must be 'C'
       jne -> si = 8               -- the error state; nothing prints

   **Byte 15 is the whole test.**  Nothing else in the reply is read anywhere in
   that function, so the rest is ours to make readable rather than to guess at.
   Sixteen characters with a C in the last one, then the LF that ends the line.

   What the real unit sends there is unknown and probably identifies it -- the
   binary carries "Geraete-Nr.: %ld" and "serialnumber: %ld" nearby, so a device
   number likely lives in this line.  Nothing reads it yet, and inventing a
   plausible serial number would only make a wrong guess harder to spot later. */
#define CP80_REPLY    "DATAPRINT V1.0 C" "\n"
#define CP80_REPLY_C  15           /* the index the guest checks */

/* One byte per tick, because 17 bytes pushed into the receive register at once
   is an overrun on a UART with the FIFO off, and the guest is reading them one
   at a time in a loop.  Roughly a byte time at 9600 baud. */
#define CP80_OUT_MS   1.5
#define CP80_OUT_MAX  64

typedef struct cp80_t {
    cp80_port_t ports[CP80_PORTS_MAX];
    int         nports;
    int         setting;

    pc_timer_t  enq;
    int         enq_on;
    int         connected;         /* the unit is plugged in */

    pc_timer_t  out;               /* paces what we send back  */
    uint8_t     out_q[CP80_OUT_MAX];
    int         out_head;
    int         out_tail;
    int         out_port;          /* which port the reply goes to */

    cp80_buf_t paper;
    cp80_buf_t trace;
    mutex_t   *lock;

    cp80_sound_t sound;

    int        dirty;              /* the guest has sent at least one byte */
    int        col;                /* for tab stops */
    int        pending_cr;         /* CR seen, waiting to see whether LF follows */

    /* escape sequence in progress */
    int        esc;                /* 0 none, 1 want command, 2 collecting */
    uint8_t    esc_intro;          /* ESC or GS */
    uint8_t    esc_cmd;
    int        esc_want;           /* parameter bytes still expected        */
    int        esc_zero;           /* collecting until a NUL                */
    int        esc_image;          /* after the count, this many data bytes */
    uint8_t    esc_par[4];
    int        esc_got;
} cp80_t;

static cp80_t *cp80_inst = NULL;

/* Why there is no printer, when there is no printer.  Set before init gives up,
   because by then there is no instance to hang it on and the window otherwise
   shows an empty state with a switch that does nothing -- which reads as broken
   rather than as "not on this machine". */
static char cp80_why[96] = "";

/* Set when the printer has brought itself online, so the UI can put the window
   on screen.  Read-and-clear. */
static int cp80_attention = 0;

/* How hard the guest polls COM2's line status, which is how the printer knows
   the operator has gone looking for it.  Measured on I.G.O. 6:

       attract screen, nothing touched   22,757 -- 22,758 reads/second
       after the operator setup button   29,839 -- 29,840 reads/second

   Flat to within one count either side, so the threshold sits between them with
   room to spare.  Both are counted against an emulated-time tick, so the ratio
   does not move with the speed of the host.

   Two seconds of it before acting, because the second in which the screen
   changes reads low -- 2,242 then 17,992 in that run -- and a threshold crossed
   once on the way past is not a screen being opened. */
#define CP80_POLL_BUSY 26000
#define CP80_POLL_SECS 2

static int cp80_polls     = 0;   /* line status reads since the last tick */
static int cp80_poll_show = 0;
static int cp80_busy_secs = 0;

/* ------------------------------------------------------------- the buffers */

static void
cp80_put(cp80_buf_t *b, const char *s, size_t n, size_t max)
{
    static const char note[] = "\n[buffer full -- not recording any more]\n";

    if (b->full)
        return;

    /* Past the cap, the last thing written is the reason there is nothing
       after it. */
    if ((b->len + n) > max) {
        s       = note;
        n       = sizeof(note) - 1;
        b->full = 1;
    }

    if ((b->len + n + 1) > b->cap) {
        size_t want = (b->cap ? b->cap : 4096);
        char  *grown;

        while ((b->len + n + 1) > want)
            want *= 2;
        grown = (char *) realloc(b->s, want);
        if (grown == NULL)
            return;
        b->s   = grown;
        b->cap = want;
    }

    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = '\0';
}

static void
cp80_paper(cp80_t *dev, const char *s, size_t n)
{
    cp80_put(&dev->paper, s, n, CP80_PAPER_MAX);
}

/* Literal onto the paper, so the length is never counted by hand. */
#define cp80_lit(dev, lit) cp80_paper((dev), (lit), sizeof(lit) - 1)

static void
cp80_tracef(cp80_t *dev, const char *fmt, ...)
{
    char    line[192];
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    if (n < 0)
        return;
    if (n > (int) (sizeof(line) - 1))
        n = (int) (sizeof(line) - 1);
    cp80_put(&dev->trace, line, (size_t) n, CP80_TRACE_MAX);
}

/* ------------------------------------------------------- mechanical sound */

#define CP80_PI 3.14159265358979323846

static uint32_t
cp80_sound_rand(cp80_sound_t *sound)
{
    uint32_t x = sound->rng;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    sound->rng = x;
    return x;
}

static void cp80_sound_paper_speed(cp80_sound_t *sound, double speed);

static void
cp80_sound_coefficients(cp80_sound_t *sound, int rate)
{
    double r;

    if ((rate <= 0) || (sound->rate == rate))
        return;

    sound->rate = rate;

    /* A step is a short mechanical impulse into the carriage and case.  These
       two lightly damped modes provide the hollow plastic body around the
       motor tone; neither is a recording or a wavetable. */
    r                = exp(-1.0 / ((double) rate * 0.0060));
    sound->body1_c   = 2.0 * r * cos((2.0 * CP80_PI * 910.0) / (double) rate);
    sound->body1_r2  = r * r;
    r                = exp(-1.0 / ((double) rate * 0.0020));
    sound->body2_c   = 2.0 * r * cos((2.0 * CP80_PI * 2380.0) / (double) rate);
    sound->body2_r2  = r * r;

    /* A short, sample-free pushbutton snap: a dull plastic plunger mode and a
       quicker contact/cap mode, excited together with a sub-millisecond burst
       of high-passed noise.  These are deliberately less resonant than the
       printer case so the result is a click rather than another motor note. */
    r                  = exp(-1.0 / ((double) rate * 0.0035));
    sound->button1_c   = 2.0 * r * cos((2.0 * CP80_PI * 680.0) / (double) rate);
    sound->button1_r2  = r * r;
    r                  = exp(-1.0 / ((double) rate * 0.0012));
    sound->button2_c   = 2.0 * r * cos((2.0 * CP80_PI * 2850.0) / (double) rate);
    sound->button2_r2  = r * r;
    sound->button_noise_a = 1.0 - exp((-2.0 * CP80_PI * 1350.0)
                                      / (double) rate);

    /* Thin thermal stock fails as broadband, non-periodic fibre bursts rather
       than a pitched cutter sound.  The high-pass removes the hissy air-like
       bottom from white noise; the second pole rolls it off before Nyquist.
       A much slower path supplies the sheet flex after it leaves the bar. */
    sound->tear_hp_a      = 1.0 - exp((-2.0 * CP80_PI * 900.0)
                                     / (double) rate);
    sound->tear_lp_a      = 1.0 - exp((-2.0 * CP80_PI * 8200.0)
                                     / (double) rate);
    sound->tear_flutter_a = 1.0 - exp((-2.0 * CP80_PI * 310.0)
                                     / (double) rate);

    /* The feed-over-carriage spectrum in the clean self-test has broad maxima
       near 1.35, 3.1 and 6--8 kHz.  Very short decays keep these as a sheet
       rattle instead of three pitched notes. */
    r                 = exp(-1.0 / ((double) rate * 0.0045));
    sound->paper1_c   = 2.0 * r * cos((2.0 * CP80_PI * 1350.0) / (double) rate);
    sound->paper1_r2  = r * r;
    r                 = exp(-1.0 / ((double) rate * 0.0024));
    sound->paper2_c   = 2.0 * r * cos((2.0 * CP80_PI * 3100.0) / (double) rate);
    sound->paper2_r2  = r * r;
    r                 = exp(-1.0 / ((double) rate * 0.0012));
    sound->paper3_c   = 2.0 * r * cos((2.0 * CP80_PI * 6900.0) / (double) rate);
    sound->paper3_r2  = r * r;

    /* Preserve the physical filter frequencies if the host output rate changes
       while a line is in flight. */
    if (sound->paper_speed > 0.0)
        cp80_sound_paper_speed(sound, sound->paper_speed);
}

static void
cp80_sound_paper_speed(cp80_sound_t *sound, double speed)
{
    double colour;

    if (sound->rate <= 0)
        return;
    if (speed < 0.1)
        speed = 0.1;
    if (speed > 1.0)
        speed = 1.0;

    /* Sliding asperities pass more slowly on a weak pack, so their spectrum
       moves down as well as becoming quieter.  Keeping 35% of the full-speed
       colour at the lowest emulated drive avoids turning paper into a low hum. */
    sound->paper_speed     = speed;
    colour                 = 0.35 + (0.65 * speed);
    sound->paper_hp_a      = 1.0 - exp((-2.0 * CP80_PI * 720.0 * colour)
                                      / (double) sound->rate);
    sound->paper_lp_a      = 1.0 - exp((-2.0 * CP80_PI * 9800.0 * colour)
                                      / (double) sound->rate);
    sound->paper_texture_a = 1.0 - exp((-2.0 * CP80_PI * 75.0 * speed)
                                      / (double) sound->rate);
    /* Unit-amplitude discrete white noise contains less energy in a fixed-Hz
       band as the sample rate rises.  This square-root correction keeps the
       paper level stable at every output rate. */
    sound->paper_gain      = 250.0 * (0.45 + (0.55 * speed))
                           * sqrt((double) sound->rate / 48000.0);
}

static void
cp80_sound_begin_feed(cp80_sound_t *sound)
{
    sound->mode       = CP80_SOUND_FEED;
    sound->feed_phase = 0.0;

    /* Taking up slack flexes the curved strip over the platen and cutter. */
    sound->paper1_z1 += 0.62;
    sound->paper2_z1 -= 0.31;
    sound->paper3_z1 += 0.16;
}

static int
cp80_sound_pop(cp80_t *dev)
{
    cp80_sound_t      *sound = &dev->sound;
    cp80_sound_event_t event;

    thread_wait_mutex(dev->lock);
    if (sound->q_head == sound->q_tail) {
        thread_release_mutex(dev->lock);
        return 0;
    }
    event         = sound->q[sound->q_head];
    sound->q_head = (sound->q_head + 1) % CP80_SOUND_Q;
    thread_release_mutex(dev->lock);

    if (event.speed < 100)
        event.speed = 100;
    if (event.speed > 1000)
        event.speed = 1000;

    sound->head_hz  = CP80_HEAD_STEP_HZ * ((double) event.speed / 1000.0);
    sound->feed_hz  = CP80_FEED_STEP_HZ * ((double) event.speed / 1000.0);
    sound->head_load = (event.columns > 0)
                     ? (double) event.ink / (double) event.columns : 0.0;
    sound->head_phase = 0.0;
    sound->feed_phase = 0.0;
    sound->feed_steps = CP80_FEED_STEPS;
    cp80_sound_paper_speed(sound, (double) event.speed / 1000.0);

    if (event.columns > 0) {
        sound->head_steps = event.columns * CP80_CHAR_STEPS;
        sound->head_total = sound->head_steps;
        sound->mode       = CP80_SOUND_HEAD;
    } else {
        sound->head_steps = 0;
        sound->head_total = 0;
        cp80_sound_begin_feed(sound);
    }

    /* The carrier taking up the drive is audible before its regular steps. */
    sound->body1_z1 += 0.55;
    sound->body2_z1 += 0.25;
    return 1;
}

static void
cp80_sound_enqueue(cp80_t *dev, unsigned columns, unsigned ink,
                   unsigned speed)
{
    cp80_sound_t *sound;
    int           next;

    if ((dev == NULL) || !dev->sound.on)
        return;

    if (columns > 80)
        columns = 80;
    if (ink > columns)
        ink = columns;
    if (speed < 100)
        speed = 100;
    if (speed > 1000)
        speed = 1000;

    sound = &dev->sound;
    thread_wait_mutex(dev->lock);
    next = (sound->q_tail + 1) % CP80_SOUND_Q;
    if (next != sound->q_head) {
        sound->q[sound->q_tail].columns = (uint16_t) columns;
        sound->q[sound->q_tail].ink     = (uint16_t) ink;
        sound->q[sound->q_tail].speed   = (uint16_t) speed;
        sound->q_tail                   = next;
    }
    thread_release_mutex(dev->lock);
}

void
prn_cp80_sound_line(unsigned columns, unsigned ink, unsigned speed)
{
    cp80_sound_enqueue(cp80_inst, columns, ink, speed);
}

void
prn_cp80_sound_feed(unsigned speed)
{
    cp80_sound_enqueue(cp80_inst, 0, 0, speed);
}

void
prn_cp80_sound_button(void)
{
    cp80_t *dev = cp80_inst;

    if ((dev == NULL) || !dev->sound.on)
        return;

    thread_wait_mutex(dev->lock);
    if (dev->sound.button_pending < 8)
        dev->sound.button_pending++;
    thread_release_mutex(dev->lock);
}

void
prn_cp80_sound_tear(void)
{
    cp80_t *dev = cp80_inst;

    if ((dev == NULL) || !dev->sound.on)
        return;

    thread_wait_mutex(dev->lock);
    if (dev->sound.tear_pending < 2)
        dev->sound.tear_pending++;
    thread_release_mutex(dev->lock);
}

static void
cp80_sound_take_controls(cp80_t *dev, int *buttons, int *tears)
{
    thread_wait_mutex(dev->lock);
    *buttons                  = dev->sound.button_pending;
    *tears                    = dev->sound.tear_pending;
    dev->sound.button_pending = 0;
    dev->sound.tear_pending   = 0;
    thread_release_mutex(dev->lock);
}

static void
cp80_sound_get_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    cp80_t       *dev   = (cp80_t *) priv;
    cp80_sound_t *sound;
    const int     rate  = sound_sample_rate;

    if ((dev == NULL) || (buffer == NULL) || (rate <= 0))
        return;

    sound = &dev->sound;
    cp80_sound_coefficients(sound, rate);

    if (sound->mode == CP80_SOUND_IDLE)
        cp80_sound_pop(dev);

    int button_presses = 0;
    int tear_events    = 0;

    cp80_sound_take_controls(dev, &button_presses, &tear_events);

    if (tear_events > 0) {
        /* About 320 ms crosses the 112 mm paper at a brisk hand pull; the last
           140 ms is the now-free receipt flexing and settling.  Keeping the
           timing in samples locks every internal envelope to the UI event. */
        sound->tear_total = sound->tear_samples
                          = (rate * 460 + 500) / 1000;
        sound->tear_next          = 0;
        sound->tear_burst_samples = 0;
        sound->tear_burst_total   = 0;
        sound->tear_final         = 0;
    }

    for (uint16_t i = 0; i < len; i++) {
        double impulse = 0.0;
        double direct  = 0.0;
        double grain   = 0.0;
        double paper   = 0.0;
        double paper_impulse = 0.0;
        double button_impulse = 0.0;
        double button_noise   = 0.0;
        double tear_noise     = 0.0;

        if ((i == 0) && (button_presses > 0)) {
            button_impulse = (double) button_presses;
            sound->button_noise_total = sound->button_noise_samples
                                      = (rate + 999) / 1000;
            /* Let the same cabinet body answer quietly underneath the local
               button modes, tying the click to the printer enclosure. */
            impulse += 0.50 * button_impulse;
        }

        if (sound->button_noise_samples > 0) {
            const double white = ((double) (int32_t) cp80_sound_rand(sound))
                               / 2147483648.0;
            const double envelope = (double) sound->button_noise_samples
                                  / (double) sound->button_noise_total;

            sound->button_noise_z += sound->button_noise_a
                                   * (white - sound->button_noise_z);
            button_noise = (white - sound->button_noise_z) * 1300.0 * envelope;
            sound->button_noise_samples--;
        }

        if (sound->tear_samples > 0) {
            const int    elapsed  = sound->tear_total - sound->tear_samples;
            const double progress = (double) elapsed / (double) sound->tear_total;
            const double white    = ((double) (int32_t) cp80_sound_rand(sound))
                                  / 2147483648.0;
            double       high;
            double       zip_env = 0.0;

            sound->tear_hp_z += sound->tear_hp_a * (white - sound->tear_hp_z);
            high              = white - sound->tear_hp_z;
            sound->tear_lp_z += sound->tear_lp_a * (high - sound->tear_lp_z);
            sound->tear_flutter_z += sound->tear_flutter_a
                                   * (white - sound->tear_flutter_z);

            /* The diagonal pull reaches the teeth after a brief bend and has
               crossed the bar by 70% of the event.  Random spacing keeps the
               fine crackle from turning into an artificial zipper pitch. */
            if ((progress >= 0.07) && (progress < 0.70)) {
                const double z = (progress - 0.07) / 0.63;

                zip_env = 0.30 + (0.70 * sin(CP80_PI * z));
                if (sound->tear_next <= 0) {
                    const double spacing = 0.0045
                                         + (0.0075 * ((double) (cp80_sound_rand(sound)
                                                                & 0xffffu) / 65535.0));
                    const double length  = 0.0011
                                         + (0.0022 * ((double) (cp80_sound_rand(sound)
                                                                & 0xffffu) / 65535.0));
                    const double strength = (double) (cp80_sound_rand(sound) & 0xffffu)
                                          / 65535.0;

                    sound->tear_next = (int) (spacing * (double) rate);
                    sound->tear_burst_total = sound->tear_burst_samples
                                            = (int) (length * (double) rate) + 1;
                    sound->tear_burst_gain = 1150.0 + (2500.0 * strength * strength);
                    paper_impulse += (0.035 + (0.16 * strength * strength)) * zip_env;
                }
                sound->tear_next--;
            }

            /* The release burst begins at the boundary above and continues on
               following samples, where the travelling-tear envelope is no
               longer active.  Give that stored burst its own full envelope. */
            if (sound->tear_final && (sound->tear_burst_samples > 0))
                zip_env = 1.0;

            if (sound->tear_burst_samples > 0) {
                const double burst_env = (double) sound->tear_burst_samples
                                       / (double) sound->tear_burst_total;

                tear_noise += sound->tear_lp_z * sound->tear_burst_gain
                            * burst_env * burst_env * zip_env;
                sound->tear_burst_samples--;
            }

            /* Continuous rubbing under the fractures, then a broad, lower
               flap as the free edge clears the cutter. */
            if (progress < 0.74) {
                const double bend = fmin(1.0, progress * 10.0)
                                  * fmin(1.0, (0.74 - progress) * 8.0);

                tear_noise += sound->tear_lp_z * (150.0 + (260.0 * bend));
            } else {
                const double release = (progress - 0.74) / 0.26;
                const double envelope = 1.0 - release;

                tear_noise += sound->tear_flutter_z * 980.0 * envelope
                            * (0.72 + (0.28 * sin(2.0 * CP80_PI * release * 3.0)));
            }

            if (!sound->tear_final && (progress >= 0.70)) {
                /* The last connected strip lets go in one brighter snap. */
                sound->tear_final = 1;
                sound->tear_burst_total = sound->tear_burst_samples
                                        = (rate + 399) / 400;
                sound->tear_burst_gain = 3900.0;
                paper_impulse         += 0.42;
            }

            sound->tear_samples--;
        }

        if (sound->mode == CP80_SOUND_HEAD) {
            const int    done = sound->head_total - sound->head_steps;
            const int    edge = (done < sound->head_steps) ? done : sound->head_steps;
            const double ramp = (edge >= 7) ? 1.0 : ((double) (edge + 1) / 8.0);
            const double p    = (sound->direction > 0)
                              ? sound->head_phase : (1.0 - sound->head_phase);
            const double load = 0.82 + (0.18 * sound->head_load);

            /* The recordings put the 422 Hz fundamental about 17 dB above its
               second and third harmonics.  A mostly sinusoidal four-phase-driven
               motor, two small harmonics and the step-excited case modes
               recreate that spectrum without a sample. */
            direct = 1750.0 * load * ramp
                   * (sin(2.0 * CP80_PI * p)
                      + (0.11 * sin(4.0 * CP80_PI * p + 0.25))
                      + (0.12 * sin(6.0 * CP80_PI * p + 0.80)));

            sound->head_phase += sound->head_hz / (double) rate;
            if (sound->head_phase >= 1.0) {
                sound->head_phase -= 1.0;
                sound->head_steps--;
                impulse = 0.70 + (0.16 * sound->head_load);

                /* The carriage has a rapid ticking texture underneath its
                   420 Hz motor pitch in the reference passes.  Eight motor
                   transitions make one normal-width character, and the
                   broadband envelope has a weaker group near that 50--53 Hz
                   cadence.  Give that boundary a stronger, still synthetic
                   case excitation.  The short body-mode decays keep the
                   individual ticks separate instead of adding another tone. */
                if (!(sound->head_steps % CP80_CHAR_STEPS))
                    impulse += (3.20 + (0.55 * sound->head_load)) * ramp;

                if (sound->head_steps <= 0) {
                    cp80_sound_begin_feed(sound);
                    sound->body1_z1  += 0.45;
                    sound->body2_z1  -= 0.20;
                }
            }

            /* A little brushless-drive grain stops the tone sounding like a
               laboratory oscillator.  It is deterministic and high-passed. */
            {
                const double white = ((double) (int32_t) cp80_sound_rand(sound))
                                   / 2147483648.0;

                sound->noise_lp += 0.035 * (white - sound->noise_lp);
                grain = (white - sound->noise_lp) * 115.0 * ramp;
            }
        } else if (sound->mode == CP80_SOUND_FEED) {
            const double p        = sound->feed_phase;
            const double progress = ((double) (CP80_FEED_STEPS - sound->feed_steps)
                                    + p) / (double) CP80_FEED_STEPS;
            const double edge     = fmin(1.0, fmin(progress * 12.0,
                                                   (1.0 - progress) * 9.0));
            double       white;
            double       rough;
            double       texture;

            /* The clean feed captures contain strong components at about 150,
               300, 450 and 600 Hz.  Keep their combined level close to the old
               model while giving the geared motor its measured third harmonic. */
            direct = 1025.0
                   * (sin(2.0 * CP80_PI * p)
                      + (0.65 * sin(4.0 * CP80_PI * p + 0.20))
                      + (0.50 * sin(6.0 * CP80_PI * p + 0.42))
                      + (0.54 * sin(8.0 * CP80_PI * p + 0.55)));

            /* Paper sliding over the rubber platen/cutter is continuous
               friction, not another oscillator.  Band-limited noise is the
               standard compact physical proxy for microscopic roughness.  A
               slow independent noise process varies contact pressure so it
               rustles instead of sounding like a steady air leak; the 150 Hz
               motor steps add only a shallow corrugation. */
            white = ((double) (int32_t) cp80_sound_rand(sound))
                  / 2147483648.0;
            sound->paper_hp_z += sound->paper_hp_a * (white - sound->paper_hp_z);
            rough = white - sound->paper_hp_z;
            sound->paper_lp_z += sound->paper_lp_a * (rough - sound->paper_lp_z);

            white = ((double) (int32_t) cp80_sound_rand(sound))
                  / 2147483648.0;
            sound->paper_texture_z += sound->paper_texture_a
                                    * (white - sound->paper_texture_z);
            texture = 0.78 + (2.8 * fabs(sound->paper_texture_z));
            paper = sound->paper_lp_z * sound->paper_gain * edge * texture
                  * (0.90 + (0.10 * cos(2.0 * CP80_PI * p)));

            sound->feed_phase += sound->feed_hz / (double) rate;
            if (sound->feed_phase >= 1.0) {
                const double fleck = fabs(((double) (int32_t)
                                           cp80_sound_rand(sound))
                                          / 2147483648.0);

                sound->feed_phase -= 1.0;
                sound->feed_steps--;
                impulse = 1.10;
                /* Irregular fibres and the curved roll release a tiny flex on
                   most platen steps.  Squaring the random value makes quiet
                   events common and conspicuous ticks rare. */
                paper_impulse = 0.025 + (0.105 * fleck * fleck);

                if (sound->feed_steps <= 0) {
                    sound->mode       = CP80_SOUND_IDLE;
                    sound->direction = -sound->direction;
                    sound->body1_z1  -= 0.38;
                    sound->body2_z1  += 0.18;
                    paper_impulse    -= 0.34;
                    cp80_sound_pop(dev);
                }
            }
        }

        /* Two resonant case modes, excited only by the synthetic motor steps
           and take-up/turnaround impulses above. */
        {
            const double y1 = (sound->body1_c * sound->body1_z1)
                            - (sound->body1_r2 * sound->body1_z2) + impulse;
            const double y2 = (sound->body2_c * sound->body2_z1)
                            - (sound->body2_r2 * sound->body2_z2) + impulse;
            const double by1 = (sound->button1_c * sound->button1_z1)
                             - (sound->button1_r2 * sound->button1_z2)
                             + button_impulse;
            const double by2 = (sound->button2_c * sound->button2_z1)
                             - (sound->button2_r2 * sound->button2_z2)
                             - (button_impulse * 0.72);
            int32_t      out;

            sound->body1_z2 = sound->body1_z1;
            sound->body1_z1 = y1;
            sound->body2_z2 = sound->body2_z1;
            sound->body2_z1 = y2;
            sound->button1_z2 = sound->button1_z1;
            sound->button1_z1 = by1;
            sound->button2_z2 = sound->button2_z1;
            sound->button2_z1 = by2;

            /* The paper modes ring after the feed itself stops, which matters
               most on a single FEED press. */
            {
                const double py1 = (sound->paper1_c * sound->paper1_z1)
                                 - (sound->paper1_r2 * sound->paper1_z2)
                                 + paper_impulse;
                const double py2 = (sound->paper2_c * sound->paper2_z1)
                                 - (sound->paper2_r2 * sound->paper2_z2)
                                 - (paper_impulse * 0.58);
                const double py3 = (sound->paper3_c * sound->paper3_z1)
                                 - (sound->paper3_r2 * sound->paper3_z2)
                                 + (paper_impulse * 0.30);

                sound->paper1_z2 = sound->paper1_z1;
                sound->paper1_z1 = py1;
                sound->paper2_z2 = sound->paper2_z1;
                sound->paper2_z1 = py2;
                sound->paper3_z2 = sound->paper3_z1;
                sound->paper3_z1 = py3;

                paper += (py1 * 23.0) + (py2 * 15.0) + (py3 * 8.0);
            }

            /* The visually aligned press near 7.405 s in the clean test video
               peaks within about 1 dB of the mechanism that follows it.  Keep
               this very short transient at roughly the feed model's peak too;
               its integrated loudness remains much lower because it is only a
               click.  The low mode carries slightly more of the gain, matching
               the recording's chunky plastic rather than making a sharp UI
               tick. */
            out = (int32_t) (direct + grain + paper + button_noise + tear_noise
                             + (y1 * 36.0) + (y2 * 18.0)
                             + (by1 * 160.0) + (by2 * 170.0));
            buffer[(i << 1)]     += out;
            buffer[(i << 1) + 1] += out;
        }

    }
}

/* --------------------------------------------------------- the character set */

/* Code page 437's upper half, which is what a DOS-era printer prints unless it
   has been told otherwise.  Without this a German receipt is unreadable, and
   this software is German on the image we have.

   ESC R selects a national set that also remaps a handful of ASCII codes; that
   is not done here, but the selection is traced, so a receipt that comes out
   with the wrong umlauts will say why. */
static const uint16_t cp80_cp437[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0
};

static void
cp80_printable(cp80_t *dev, uint8_t c)
{
    char     utf8[4];
    unsigned n = 0;

    if (c < 0x80) {
        utf8[n++] = (char) c;
    } else {
        const uint16_t u = cp80_cp437[c - 0x80];

        if (u < 0x800) {
            utf8[n++] = (char) (0xc0 | (u >> 6));
            utf8[n++] = (char) (0x80 | (u & 0x3f));
        } else {
            utf8[n++] = (char) (0xe0 | (u >> 12));
            utf8[n++] = (char) (0x80 | ((u >> 6) & 0x3f));
            utf8[n++] = (char) (0x80 | (u & 0x3f));
        }
    }

    cp80_paper(dev, utf8, n);
    dev->col++;
}

/* --------------------------------------------------------------- the parser */

/* How many parameter bytes follow ESC <cmd>.  The two dialects agree on the
   shape and disagree on the contents, so anything not in here is named in the
   trace rather than assumed to take none -- guessing a length wrong desyncs the
   stream and turns the rest of the receipt into noise. */
#define CP80_ESC_UNKNOWN (-1)
#define CP80_ESC_ZERO    (-2)      /* parameters run until a NUL */

/* There was a CP80_ESC_IMAGE here that read two count bytes after ESC K, L, Y
   or Z and skipped that many data bytes, which is correct for Epson ESC/P bit
   images and wrong for this device.  The first real report began ESC K 21 0A
   and it swallowed 2593 bytes of the payload -- the whole print job -- on the
   strength of a dialect this link has now been shown not to speak.

   Nothing is skipped in bulk any more.  An unrecognised sequence costs a line
   in the trace and the stream carries on, which at worst prints a few stray
   characters and at best does not hide the thing we are trying to read. */

/* Only the codes where ESC/P and ESC/POS *agree* on the length are parsed.

   Where they disagree the answer is CP80_ESC_UNKNOWN, which stops the parser
   and says so in the trace, because taking the wrong length desyncs the stream
   and turns the rest of the receipt into noise that looks like data.  The ones
   deliberately left out for that reason:

       ESC E   ESC/P bold on, no parameter -- ESC/POS emphasise, one
       ESC G   ESC/P double strike, none   -- ESC/POS double strike, one
       ESC M   ESC/P 12 cpi, none          -- ESC/POS select font, one
       ESC p   ESC/P proportional, one     -- ESC/POS drawer pulse, two
       ESC c   not in ESC/P                -- ESC/POS ESC c 3/4/5, two
       ESC (   ESC/P2 extended, carries its own length word

   Each of those appearing in a real capture identifies the dialect on its own,
   which is worth more than rendering it. */
static int
cp80_esc_len(uint8_t cmd)
{
    switch (cmd) {
        /* The Dataprint's own framing, which is not a printer dialect at all.
           ESC S is the command the host sends inside XON..XOFF, and ESC C is
           what the reply is built around -- 'C' is the byte at index 15 that
           the host checks.  Neither takes a parameter, and letting ESC S eat
           the XOFF after it (which the ESC/P reading of 'S' would) mis-frames
           every capture. */
        case 'S':
            return 0;

        /* ESC K <tag> LF prefixes a line of the report, from a real capture:
           ! before "funworld", " before "Photo Play 2000", $ before the serial
           number, & before the first transaction and B before the total.  What
           the tags mean is not known -- they are not line numbers and nothing in
           MENU.EXE writes them as a constant, so they are built at run time --
           but they are consistently three bytes and the trailing LF belongs to
           the tag rather than to the text, so it is eaten with it.  Printing
           them would put a stray "!" on its own line above every heading. */
        case 'K':
            return 2;

        /* ESC C in the trailer introduces the four hex digits of the checksum,
           which is the running sum di accumulates at 0x1EA9E.  A real one:
           04 1B 43 39 45 36 37 16 -- EOT, ESC C, "9E67", SYN. */
        case 'C':
            return 4;

        /* no parameters, both dialects */
        case '@': case 'F': case 'H':
        case '0': case '1': case '2': case '4': case '5':
        case '6': case '7': case '8': case '9':
        case '<': case '=': case '>':
            return 0;

        /* one parameter, both dialects */
        case '!': case '-': case '3': case 'A':
        case 'J': case 'N': case 'Q': case 'R':
        case 'U': case 'W': case 'a': case 'd': case 'l':
        case 'r': case 's': case 'w': case '%': case '/':
            return 1;

        /* two parameters */
        case '$': case 'f':
            return 2;

        /* three */
        case ':':
            return 3;

        /* tab stop lists, terminated by NUL */
        case 'D': case 'B': case 'b':
            return CP80_ESC_ZERO;

        default:
            return CP80_ESC_UNKNOWN;
    }
}

static int
cp80_gs_len(uint8_t cmd)
{
    switch (cmd) {
        case 'V': case '!': case 'B': case 'h': case 'w':
        case 'f': case 'r':
            return 1;
        case 'L': case 'W': case 'H':
            return 2;
        default:
            return CP80_ESC_UNKNOWN;
    }
}

static const char *
cp80_esc_name(uint8_t intro, uint8_t cmd)
{
    if (intro == 0x1b) {
        switch (cmd) {
            case '@': return "reset";
            case 'S': return "Dataprint command";
            case 'K': return "record tag";
            case 'C': return "checksum";
            case 'F': return "bold off";
            case 'H': return "double strike off";
            case '!': return "select print mode";
            case '-': return "underline";
            case 'a': return "justify";
            case 'J': return "feed n/216";
            case 'd': return "feed n lines";
            case 'R': return "national character set";
            case 'D': return "horizontal tab stops";
            case 'B': return "vertical tab stops";
            default:  return NULL;
        }
    }
    switch (cmd) {
        case 'V': return "cut paper";
        case '!': return "character size";
        case 'L': return "left margin";
        case 'H': return "HRI position";
        default:  return NULL;
    }
}

static void
cp80_esc_done(cp80_t *dev)
{
    const char *name = cp80_esc_name(dev->esc_intro, dev->esc_cmd);
    char        pars[32];
    int         at = 0;

    pars[0] = '\0';
    for (int i = 0; (i < dev->esc_got) && (i < (int) sizeof(dev->esc_par)); i++)
        at += snprintf(pars + at, sizeof(pars) - (size_t) at, " %02X",
                       dev->esc_par[i]);

    cp80_tracef(dev, "%s %c%s%s%s\n",
                (dev->esc_intro == 0x1b) ? "ESC" : "GS ",
                (dev->esc_cmd >= 0x20 && dev->esc_cmd < 0x7f) ? (char) dev->esc_cmd : '?',
                pars,
                name ? "   " : "",
                name ? name : "");

    /* The two that change what the paper looks like rather than how it is
       printed.  Everything else is styling this does not render. */
    if (dev->esc_intro == 0x1b) {
        if (dev->esc_cmd == '@') {
            dev->col = 0;
        } else if ((dev->esc_cmd == 'd') && (dev->esc_got >= 1)) {
            for (int i = 0; i < dev->esc_par[0]; i++)
                cp80_paper(dev, "\n", 1);
            dev->col = 0;
        }
    } else if (dev->esc_cmd == 'V') {
        cp80_lit(dev, "\n----------------------- cut -----------------------\n");
        dev->col = 0;
    }

    dev->esc = 0;
}

static void
cp80_byte(cp80_t *dev, uint8_t c)
{
    /* A CR that turned out not to be the front half of a CRLF is a line on its
       own -- some drivers end lines with CR, some with LF, some with both, and
       a receipt run into one long line is no use to anybody. */
    if (dev->pending_cr && (c != 0x0a)) {
        cp80_paper(dev, "\n", 1);
        dev->col = 0;
    }
    dev->pending_cr = 0;

    /* ---- an escape sequence in progress ---- */
    if (dev->esc == 1) {
        int len;

        dev->esc_cmd = c;
        dev->esc_got = 0;
        len = (dev->esc_intro == 0x1b) ? cp80_esc_len(c) : cp80_gs_len(c);

        if (len == CP80_ESC_UNKNOWN) {
            cp80_tracef(dev, "%s %c (%02X)   ** not recognised -- either the two "
                             "dialects disagree on its length, or it is not a "
                             "code we know; the rest of this sequence prints as "
                             "text **\n",
                        (dev->esc_intro == 0x1b) ? "ESC" : "GS ",
                        ((c >= 0x20) && (c < 0x7f)) ? (char) c : '?', c);
            dev->esc = 0;
            return;
        }
        if (len == CP80_ESC_ZERO) {
            dev->esc_zero = 1;
            dev->esc      = 2;
            return;
        }
        if (len == 0) {
            cp80_esc_done(dev);
            return;
        }
        dev->esc_want = len;
        dev->esc      = 2;
        return;
    }

    if (dev->esc == 2) {
        if (dev->esc_zero) {
            if (c == 0x00) {
                dev->esc_zero = 0;
                cp80_esc_done(dev);
            } else if (dev->esc_got < (int) sizeof(dev->esc_par))
                dev->esc_par[dev->esc_got++] = c;
            return;
        }

        if (dev->esc_got < (int) sizeof(dev->esc_par))
            dev->esc_par[dev->esc_got] = c;
        dev->esc_got++;
        dev->esc_want--;

        if (dev->esc_want > 0)
            return;

        if (dev->esc_image == -1) {
            /* the two count bytes have arrived; skip the data itself */
            dev->esc_image = dev->esc_par[0] + (dev->esc_par[1] << 8);
            cp80_tracef(dev, "ESC %c %02X %02X   bit image, %d bytes skipped\n",
                        (char) dev->esc_cmd, dev->esc_par[0], dev->esc_par[1],
                        dev->esc_image);
            if (dev->esc_image > 0) {
                dev->esc_want = dev->esc_image;
                dev->esc_got  = (int) sizeof(dev->esc_par);   /* stop recording */
                dev->esc_image = -2;                          /* now eating data */
                return;
            }
            dev->esc_image = 0;
            dev->esc       = 0;
            return;
        }
        if (dev->esc_image == -2) {
            dev->esc_image = 0;
            dev->esc       = 0;
            return;
        }

        cp80_esc_done(dev);
        return;
    }

    /* ---- not in a sequence ---- */
    switch (c) {
        case 0x1b:                          /* ESC */
        case 0x1d:                          /* GS  */
            dev->esc_intro = c;
            dev->esc       = 1;
            dev->esc_zero  = 0;
            dev->esc_image = 0;
            return;

        case 0x0a:                          /* LF */
            cp80_paper(dev, "\n", 1);
            dev->col = 0;
            return;

        case 0x0d:                          /* CR */
            dev->pending_cr = 1;
            return;

        case 0x0c:                          /* FF */
            cp80_lit(dev, "\n-------------------- form feed --------------------\n");
            dev->col = 0;
            cp80_tracef(dev, "FF          form feed\n");
            return;

        case 0x09: {                        /* HT */
            const int to = ((dev->col / CP80_TABS) + 1) * CP80_TABS;

            while (dev->col < to) {
                cp80_paper(dev, " ", 1);
                dev->col++;
            }
            return;
        }

        case 0x08:                          /* BS */
            if ((dev->paper.len > 0) && (dev->paper.s[dev->paper.len - 1] != '\n')) {
                dev->paper.len--;
                dev->paper.s[dev->paper.len] = '\0';
                if (dev->col > 0)
                    dev->col--;
            }
            return;

        case 0x00:
            return;                         /* padding; printers ignore it */

        default:
            break;
    }

    if (c < 0x20) {
        const char *what = NULL;

        /* The Dataprint's frame, from the state machine at 0x1E8FC: XON, the
           ESC S command, XOFF, then ETX and two LFs. */
        switch (c) {
            case 0x11: what = "XON -- frame start";            break;
            case 0x13: what = "XOFF -- frame end";             break;
            case 0x03: what = "ETX -- end of command";         break;
            case 0x05: what = "ENQ";                           break;
            case 0x04: what = "EOT -- report done";            break;
            case 0x16: what = "SYN -- end of trailer";         break;
            default:   what = "control byte, not printed";     break;
        }
        cp80_tracef(dev, "%02X          %s\n", c, what);
        return;
    }

    cp80_printable(dev, c);
}

/* ---------------------------------------------------------------- the wire */

/* The reply queue, defined with the rest of the port handling below. */
static int  cp80_out_pending(const cp80_t *dev);
static void cp80_say(cp80_t *dev, int port, const char *bytes, size_t len);

static void
cp80_write(UNUSED(serial_t *serial), void *priv, uint8_t val)
{
    cp80_port_t *p   = (cp80_port_t *) priv;
    cp80_t      *dev = (p != NULL) ? p->dev : NULL;

    if (dev == NULL)
        return;

    thread_wait_mutex(dev->lock);

    /* Which port it turned up on is the answer to the question this device was
       built for, so it is said once, loudly, per port. */
    if (!p->seen) {
        p->seen = 1;
        pclog("CP80: the guest is printing to COM%d\n", p->port + 1);
        cp80_tracef(dev, "-- printing to COM%d --\n", p->port + 1);
    }

    if (!dev->dirty)
        dev->dirty = 1;

    cp80_byte(dev, val);

    /* ETX ends a command frame, and the guest then sits in a receive state
       waiting for a line.  Answer it.  Replying on the ETX rather than on the
       trailing LFs is deliberate: the reply is paced a byte at a time and the
       guest's FIFO holds it until it looks, so being early is free and being
       late is a timeout. */
    if ((val == 0x03) && dev->connected && !cp80_out_pending(dev)) {
        cp80_say(dev, p->port, CP80_REPLY, sizeof(CP80_REPLY) - 1);
        cp80_tracef(dev, "-- answered with %s --\n", CP80_REPLY);
        pclog("CP80: command frame ended; answering \"%s\"\n", CP80_REPLY);
    }

    thread_release_mutex(dev->lock);
}

/* ------------------------------------------------------------- for the UI */

int
prn_cp80_present(void)
{
    return (cp80_inst != NULL) && (cp80_inst->nports > 0);
}

/* Defined with the rest of the port handling, below. */
static void cp80_wire(cp80_t *dev, int up);

int
prn_cp80_attention(void)
{
    const int a = cp80_attention;

    cp80_attention = 0;
    return a;
}

int
prn_cp80_connected(void)
{
    return (cp80_inst != NULL) && cp80_inst->connected;
}

void
prn_cp80_set_connected(int on)
{
    cp80_t *dev = cp80_inst;

    if ((dev == NULL) || (dev->connected == !!on))
        return;

    dev->connected = !!on;
    cp80_wire(dev, dev->connected);
    pclog("CP80: Dataprint %s\n", dev->connected ? "plugged in" : "unplugged");
}

/* "COM1", or "COM1 and COM2", for the window to say where it is listening. */
void
prn_cp80_where(char *out, size_t len)
{
    const cp80_t *dev = cp80_inst;

    if ((out == NULL) || (len == 0))
        return;
    if ((dev == NULL) || (dev->nports == 0)) {
        snprintf(out, len, "%s", cp80_why[0] ? cp80_why : "No printer attached");
        return;
    }
    /* One port, since the port stopped being a choice.  There was a branch here
       naming two, left over from listening on COM1 and COM2 at once while it was
       still open which one the software used -- unreachable with CP80_PORTS_MAX
       at 1, and the compiler was right to say the index was out of bounds. */
    snprintf(out, len, "COM%d", dev->ports[0].port + 1);
}

int
prn_cp80_dirty(void)
{
    return (cp80_inst != NULL) && cp80_inst->dirty;
}

size_t
prn_cp80_take(int which, size_t *pos, int *reset, char *out, size_t max)
{
    cp80_t     *dev = cp80_inst;
    cp80_buf_t *b;
    size_t      n;

    if (reset != NULL)
        *reset = 0;
    if ((dev == NULL) || (pos == NULL) || (out == NULL) || (max == 0))
        return 0;

    thread_wait_mutex(dev->lock);

    b = (which == PRN_CP80_TRACE) ? &dev->trace : &dev->paper;

    if (*pos > b->len) {
        *pos = 0;
        if (reset != NULL)
            *reset = 1;
    }

    n = b->len - *pos;
    if (n > max)
        n = max;
    if (n > 0) {
        memcpy(out, b->s + *pos, n);
        *pos += n;
    }

    thread_release_mutex(dev->lock);
    return n;
}

void
prn_cp80_clear(void)
{
    cp80_t *dev = cp80_inst;

    if (dev == NULL)
        return;

    thread_wait_mutex(dev->lock);
    dev->paper.len  = 0;
    dev->trace.len  = 0;
    dev->paper.full = 0;
    dev->trace.full = 0;
    if (dev->paper.s != NULL)
        dev->paper.s[0] = '\0';
    if (dev->trace.s != NULL)
        dev->trace.s[0] = '\0';
    dev->col = 0;
    thread_release_mutex(dev->lock);
}

/* -------------------------------------------------------------- the device */

/* PEEPEEBOX_PRN_TEST=<file> feeds a file through the parser at start-up, as
   though the guest had printed it.

   Two uses.  It puts the window on screen with something in it before the
   cabinet has ever printed, so "the printer does nothing" can be told apart
   from "the window does nothing" -- the same distinction the port B run cost a
   session to learn.  And once a real cp80-raw.bin exists, feeding it back in is
   how a change to the parser gets checked without booting anything.

   The rendered result goes to the log as well, so a run is readable without
   looking at the screen. */
static void
cp80_self_test(cp80_t *dev, const char *path)
{
    FILE *f = fopen(path, "rb");
    int   c;

    if (f == NULL) {
        pclog("CP80: PEEPEEBOX_PRN_TEST=%s cannot be opened\n", path);
        return;
    }

    pclog("CP80: feeding %s through the parser\n", path);
    while ((c = fgetc(f)) != EOF)
        cp80_byte(dev, (uint8_t) c);
    fclose(f);

    dev->dirty = 1;
    pclog("CP80: --- paper ---\n%s\nCP80: --- control codes ---\n%s\n",
          dev->paper.s ? dev->paper.s : "", dev->trace.s ? dev->trace.s : "");
}

/* PEEPEEBOX_PRN_PORT=1..4 still moves it for one run.  Not a setting, because
   the answer is known -- but a knob for the next image that disagrees, without
   which finding that out means a rebuild. */
static int
cp80_setting(void)
{
    const char *env = getenv("PEEPEEBOX_PRN_PORT");

    if ((env != NULL) && (env[0] >= '1') && (env[0] <= '4'))
        return env[0] - '1';
    return CP80_PORT;
}

static int
cp80_out_pending(const cp80_t *dev)
{
    return dev->out_head != dev->out_tail;
}

static void
cp80_say(cp80_t *dev, int port, const char *bytes, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        const int next = (dev->out_tail + 1) % CP80_OUT_MAX;

        if (next == dev->out_head) {
            pclog("CP80: reply queue full; dropping the rest\n");
            return;
        }
        dev->out_q[dev->out_tail] = (uint8_t) bytes[i];
        dev->out_tail             = next;
    }
    dev->out_port = port;
}

static void
cp80_out_tick(void *priv)
{
    cp80_t *dev = (cp80_t *) priv;

    if (dev == NULL)
        return;

    if (cp80_out_pending(dev) && dev->connected) {
        serial_t *ser = NULL;

        for (int i = 0; i < dev->nports; i++)
            if (dev->ports[i].port == dev->out_port)
                ser = dev->ports[i].serial;

        if (ser != NULL)
            serial_write_fifo(ser, dev->out_q[dev->out_head]);
        dev->out_head = (dev->out_head + 1) % CP80_OUT_MAX;
    }

    timer_on_auto(&dev->out, CP80_OUT_MS * 1000.0);
}

static void
cp80_enq_tick(void *priv)
{
    cp80_t *dev = (cp80_t *) priv;

    if (dev == NULL)
        return;

    /* Reported before anything else returns.  Offline is exactly when the
       interesting polling happens -- the guest is looking for a printer it
       cannot find -- so a report that only runs when connected would measure
       the one case nobody needs measured. */
    {
        static int ticks = 0;

        if (++ticks >= (int) (1000.0 / CP80_ENQ_MS)) {
            ticks = 0;

            if (cp80_poll_show && (cp80_polls > 0))
                pclog("CP80-POLL: COM%d line status read %d times in the last "
                      "second (printer %s)\n",
                      dev->ports[0].port + 1, cp80_polls,
                      dev->connected ? "online" : "offline");

            /* Busy enough for long enough, and not already on: the operator has
               gone looking for the printer, so stop making them find a switch. */
            if (dev->connected || (cp80_polls < CP80_POLL_BUSY))
                cp80_busy_secs = 0;
            else if (++cp80_busy_secs >= CP80_POLL_SECS) {
                cp80_busy_secs = 0;
                pclog("CP80: COM%d polled %d times a second; the operator is "
                      "looking for the printer, coming online\n",
                      dev->ports[0].port + 1, cp80_polls);
                prn_cp80_set_connected(1);
                cp80_attention = 1;
            }

            cp80_polls = 0;
        }
    }

    /* Not while a reply is going out.  The guest's receive state stores every
       byte it sees until LF, so an ENQ landing mid-reply shifts byte 15 and the
       check fails on a reply that was otherwise right.  The gap is a few tens of
       milliseconds against a watchdog measured in the hundreds, so this is not
       the blanket hush that broke the last version. */
    if (!dev->connected || cp80_out_pending(dev)) {
        timer_on_auto(&dev->enq, CP80_ENQ_MS * 1000.0);
        return;
    }

    for (int i = 0; i < dev->nports; i++) {
        cp80_port_t *p = &dev->ports[i];

        if (p->serial == NULL)
            continue;

        serial_write_fifo(p->serial, CP80_ENQ);
        if (!p->announced) {
            p->announced = 1;
            pclog("CP80: announcing on COM%d with ENQ (05) every %g ms\n",
                  p->port + 1, CP80_ENQ_MS);
        }
    }

    timer_on_auto(&dev->enq, CP80_ENQ_MS * 1000.0);
}

/* Coming online by itself, and how it knows when to.

   The first attempt used the line control register: MENU.EXE programs the port
   at 0x1D0E5 on its way to the Dataprint, so an LCR write looked like the
   operator going looking for the printer.  It fires during a plain boot, so it
   is not that signal, and this is being measured rather than guessed at a
   second time.

   What the Dataprint screen actually does is poll: read the line status, test
   bit 0, and give up if nothing is waiting -- over and over for as long as the
   dialog is open.  A spare UART with nothing on it has no other reason to be
   read at all, so the *rate* of those reads should separate "the operator is on
   that screen" from everything else.  pp_serial_lsr_read counts them and the
   tick below reports the count, so the thresholds can come from a real run.

   PEEPEEBOX_PRN_POLL=1 reports the rate every second, which is how the numbers
   at CP80_POLL_BUSY were got and how to check them against another image. */
static void
cp80_lsr_read(int port)
{
    const cp80_t *dev = cp80_inst;

    if ((dev != NULL) && (dev->nports > 0) && (port == dev->ports[0].port))
        cp80_polls++;
}

static void
cp80_lcr(UNUSED(serial_t *serial), void *priv, UNUSED(uint8_t data_bits))
{
    const cp80_port_t *p = (const cp80_port_t *) priv;

    if ((p != NULL) && cp80_poll_show)
        pclog("CP80-POLL: the guest programmed COM%d\n", p->port + 1);
}

/* Plugged in or not.  Modem lines follow, because "no printer" should look to
   the guest the way an unplugged one does and not merely go quiet. */
static void
cp80_wire(cp80_t *dev, int up)
{
    for (int i = 0; i < dev->nports; i++) {
        serial_t *ser = dev->ports[i].serial;

        if (ser == NULL)
            continue;
        serial_set_cts(ser, up);
        serial_set_dsr(ser, up);
        serial_set_dcd(ser, up);
    }
}

static void
cp80_attach(cp80_t *dev, int port)
{
    cp80_port_t *p;

    if (dev->nports >= CP80_PORTS_MAX)
        return;

    p         = &dev->ports[dev->nports];
    p->dev    = dev;
    p->port   = port;
    p->seen   = 0;
    /* lcr rather than dtr: attach_ex carries one or the other, and knowing
       when the link is opened is worth more than knowing when DTR moved. */
    p->serial = serial_attach_ex(port, NULL, cp80_write, NULL, cp80_lcr, p);

    if (p->serial == NULL) {
        pclog("CP80: COM%d is already taken; not listening there\n", port + 1);
        return;
    }

    /* Modem lines are set from the connected state once everything is up; see
       cp80_wire.  Online means CTS, DSR and DCD asserted, because a serial
       printer that never says it is ready is a guest that waits forever. */
    dev->nports++;
    pclog("CP80: receipt printer listening on COM%d, ready\n", port + 1);
}

static void *
cp80_init(UNUSED(const device_t *info))
{
    cp80_t *dev = (cp80_t *) calloc(1, sizeof(cp80_t));

    if (dev == NULL)
        return NULL;

    /* Not on I.G.O. 8.  That generation moved its dongle onto COM2 -- a serial
       smart-card reader, in dongle_igo8.c -- so the port is spoken for and two
       devices on it would be a fight neither wins.  serial_attach would refuse
       the second comer anyway, but failing quietly at the wrong layer is how a
       morning goes missing; say so instead.

       Whether these cabinets carried a Dataprint at all on 2008 hardware, and
       where it went if they did, is a separate question for later. */
    {
        char banner[64] = "";

        if (photoplay_image_ident(banner, sizeof(banner), NULL, 0) &&
            (strstr(banner, "2008") != NULL)) {
            snprintf(cp80_why, sizeof(cp80_why),
                     "No printer: %s keeps its dongle on COM2", banner);
            pclog("CP80: %s is an I.G.O. 8 image and its dongle owns COM2; "
                  "no printer attached\n", banner);
            free(dev);
            return NULL;
        }
    }

    dev->lock = thread_create_mutex();
    if (dev->lock == NULL) {
        free(dev);
        return NULL;
    }

    dev->setting = cp80_setting();
    cp80_attach(dev, dev->setting);

    if (dev->nports == 0) {
        snprintf(cp80_why, sizeof(cp80_why),
                 "No printer: COM%d is already taken", dev->setting + 1);
        pclog("CP80: no free port; no printer attached\n");
        thread_close_mutex(dev->lock);
        free(dev);
        return NULL;
    }

    /* Unplugged until asked.  With the unit visible the DATAPRINT menu drops
       straight into the print dialog and there is no way back to the rest of
       it, so a cabinet that boots with the printer already connected is a
       cabinet whose operator menu you cannot use. */
    dev->connected     = 0;
    cp80_poll_show     = (getenv("PEEPEEBOX_PRN_POLL") != NULL);
    pp_serial_lsr_read = cp80_lsr_read;
    cp80_wire(dev, 0);
    dev->enq_on    = (device_get_config_int_ex("enq", 1) != 0);
    {
        const char *env = getenv("PEEPEEBOX_PRN_ENQ");

        if (env != NULL)
            dev->enq_on = (atoi(env) != 0);
    }

    dev->sound.on        = (device_get_config_int_ex("sound", 1) != 0);
    dev->sound.direction = 1;
    dev->sound.rng       = 0x414d5055u;
    {
        const char *env = getenv("PEEPEEBOX_PRN_SOUND");

        if (env != NULL)
            dev->sound.on = (atoi(env) != 0);
    }
    if (dev->sound.on)
        sound_add_handler(cp80_sound_get_buffer, dev);

    /* The one byte of the reply that matters, checked out loud.  Editing that
       string and quietly moving the C off index 15 would put the guest back on
       the error path with nothing to say why. */
    if (((sizeof(CP80_REPLY) - 1) <= CP80_REPLY_C) || (CP80_REPLY[CP80_REPLY_C] != 'C'))
        pclog("CP80: reply byte %d is not 'C' -- the guest will reject it\n",
              CP80_REPLY_C);

    timer_add(&dev->out, cp80_out_tick, dev, 0);
    timer_on_auto(&dev->out, CP80_OUT_MS * 1000.0);

    if (dev->enq_on) {
        timer_add(&dev->enq, cp80_enq_tick, dev, 0);
        timer_on_auto(&dev->enq, CP80_ENQ_MS * 1000.0);
    } else
        pclog("CP80: ENQ announcements are off; the software will not find "
              "the printer\n");

    cp80_inst = dev;

    {
        const char *test = getenv("PEEPEEBOX_PRN_TEST");

        if (test != NULL)
            cp80_self_test(dev, test);
    }
    return dev;
}

static void
cp80_close(void *priv)
{
    cp80_t *dev = (cp80_t *) priv;

    if (dev == NULL)
        return;

    /* The UART keeps calling it otherwise, into a device that has gone. */
    pp_serial_lsr_read = NULL;

    if (dev->lock != NULL)
        thread_close_mutex(dev->lock);
    free(dev->paper.s);
    free(dev->trace.s);
    free(dev);
    cp80_inst = NULL;
}

static const device_config_t cp80_config[] = {
  // clang-format off
    {
        .name           = "enq",
        .description    = "Announce itself (ENQ)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "sound",
        .description    = "Mechanical sound",
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

const device_t prn_cp80_device = {
    .name          = "Receipt printer (serial)",
    .internal_name = "prn_cp80",
    .flags         = DEVICE_COM,
    .local         = 0,
    .init          = cp80_init,
    .close         = cp80_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = cp80_config
};
