/*
 * PeepeeBox   A fork of 86Box that emulates the funworld Photo Play / I.G.O.
 *             arcade kiosk hardware, including its protection token.
 *
 *             The modem on COM4: a Diamond SupraExpress 56e PRO, or an ELSA
 *             MicroLink 56k.
 *
 * Authors:    The HUEG PP team.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 *
 * ---------------------------------------------------------------------------
 *
 * The cabinets that were on fun.net had an external modem hanging off COM4.
 * Which one is not a guess: `\FN_SYS\FN_SYS.EXE` carries the whole modem
 * database on the disk itself, and two of its thirty entries are the parts these
 * machines turn up with --
 *
 *     2  ELSA MicroLink 56k             9 11 -1 -1  ATi3  0  0
 *     3  Diamond SupraExpress 56e PRO  15 -1 -1 -1  ATi7  0  0
 *
 * from `\FN_SYS\DATABASE\NETWORK\MD_NAME.CSV`.  Columns 3..6 are token ids into
 * `MD_INFOS.CSV`, which says how to recognise each part:
 *
 *      9  ATi6  MicroLink
 *     11  ATi6  56
 *     15  ATi3  SupraExpress
 *
 * -- so the Supra is known by `SupraExpress` in its `ATI3`, and the ELSA by
 * `MicroLink` and `56` both being in its `ATI6`.  Column 7 is the command whose
 * answer gets filed as the firmware, and the two parts disagree about that too:
 * `ATI7` for the Supra, `ATI3` for the ELSA.  FN_SYS prints both back at the
 * operator as
 *
 *     MODEM   : %s
 *     FIRMWARE: %s
 *
 * and stores them in `techdata.tab` as `MODEMTYP` / `MODEMFMW`.  A modem that
 * answers nothing recognisable is reported as `Standard-Modem`; a port with
 * nothing on it gets `ERROR: can't talk to modem`.
 *
 * So the identity a model reports is not decoration -- it is the thing the guest
 * measures, and the answers in the model table below are chosen so that exactly
 * one of the thirty rows can match.  See docs/research/33-modem.md.
 *
 * The rest of the wiring is equally well attested.  An IGO 6 image still has the
 * `NET.CFG` its last dial session wrote:
 *
 *     LINK DRIVER PPP
 *         PORT 02E8
 *         INT 10
 *         BAUD 57,600
 *         FLOW CONTROL HARDWARE
 *         CONNECTION MODEM
 *         DIAL 0,0718915252
 *         MODEM INIT1 "ate0m1l3S8=5S7=20S10=40x3"
 *         MODEM INIT2 ""
 *         MODEM DIAL "ATDT"
 *
 * COM4 at 0x02E8 on IRQ 10, 57600 8N1, RTS/CTS -- which is why photoplay.c
 * enables COM4 on IRQ 10 rather than the PC-standard 3 when the modem is
 * fitted.  That capture also says which part *that* cabinet had: `MD_INIT2.CSV`
 * gives modem 3 an `AT%C3&K3BN1W2` on every one of its 51 ISP rows and gives
 * modem 2 nothing at all, and the file above has INIT2 empty.  ND003 was not
 * running a SupraExpress.
 *
 * What this device does *not* do is pretend there is a telephone network.  The
 * fun.net dial-up servers are gone.  By default the line is dead: the modem
 * identifies, accepts every init string the cabinet sends, dials, and reports
 * the failure a real modem would report into a dead socket.  If a host is
 * configured instead, dialling opens a TCP connection to it and the modem
 * becomes a transparent pipe, which is what the guest's PPP stack wants.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/ini.h>
#include <86box/char.h>
#include <86box/log.h>
#include <86box/plat.h>
#include <86box/plat_netsocket.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_CHAR_MODEM_LOG
int char_modem_do_log = ENABLE_CHAR_MODEM_LOG;

static void
char_modem_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (char_modem_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define char_modem_log(priv, fmt, ...)
#endif

#define MODEM_OUT_SIZE 4096 /* modem -> DTE ring */
#define MODEM_CMD_SIZE 256

enum { /* what is on the other side of the RJ11 */
       MODEM_LINE_DEAD = 0, /* nothing: dialling fails the way it would */
       MODEM_LINE_TCP  = 1  /* a TCP host stands in for the whole PSTN */
};

enum { /* line state machine */
       MODEM_ST_IDLE = 0,
       MODEM_ST_DIALING,    /* off hook, waiting out the dial  */
       MODEM_ST_CONNECTING, /* TCP connect in flight           */
       MODEM_ST_ANSWERING,  /* carrier found: training, then CONNECT */
       MODEM_ST_ONLINE
};

/* A TCP connect to localhost completes in well under a millisecond; a V.34
   handshake takes several seconds.  The gap matters: the cabinet's PPP driver
   (Klos, PPP.EXE) sends ATDT and then reads result lines until it sees its
   CONNECTSTRING, and it also watches DCD.  Hand it DCD and the CONNECT line in
   the same instant and it goes by the carrier, never reads the text, and the
   cabinet logs the call as NO RESPONSE -- PPPMENU's placeholder for "the modem
   never said".  So: wait a plausible while, say CONNECT, let the DTE drain it,
   and only then raise DCD and go on line, which is the order a real modem does
   it in and what the cabinet's own logs show (CONNECT 48000/LAPM, every night
   for a decade). */
#define MODEM_TRAIN_MS   2000 /* dial to CONNECT                          */
#define MODEM_SETTLE_MS  100  /* CONNECT drained to DCD and data mode     */

enum { /* result codes, in the numeric order every Hayes modem uses */
       RES_OK = 0,
       RES_CONNECT,
       RES_RING,
       RES_NO_CARRIER,
       RES_ERROR,
       RES_CONNECT_1200,
       RES_NO_DIALTONE,
       RES_BUSY,
       RES_NO_ANSWER,
       RES_NONE = -1
};

static const char *modem_res_text[] = {
    "OK", "CONNECT", "RING", "NO CARRIER", "ERROR",
    "CONNECT 1200", "NO DIALTONE", "BUSY", "NO ANSWER"
};

/* The two modems these cabinets are found with, as MD_NAME.CSV rows 2 and 3.

   `ident` is the string the guest matches on, and `ident_at` says which `ATIn`
   carries it -- the two parts disagree about that, which is exactly why this is
   a table and not a pair of if statements.  `fmw_at` is MD_NAME column 7, the
   command whose answer is filed as MODEMFMW.  `info` is everything else, and a
   NULL there means the modem says nothing to that query.

   Each model's answers were chosen so that **exactly one** row of the guest's
   thirty can match, whichever order it evaluates them in:

     row  3  Diamond SupraExpress 56e PRO  needs {15}         = SupraExpress@I3
     row 14  ...the same part's V.92 variant needs {15, 28}, and 28 is V.92@I3
     row  2  ELSA MicroLink 56k            needs {9, 11}      = MicroLink@I6, 56@I6
     rows 7/18/22 are MicroLink 56k variants needing 9 and 11 *and* one more --
             Internet@I6, -i@I6 + "k i"@I6, pro@I6 -- none of which appear here.

   docs/research/33-modem.md walks all thirty. */
enum {
    MODEM_MODEL_SUPRA = 0,
    MODEM_MODEL_ELSA  = 1
};

typedef struct {
    const char *name;        /* MD_NAME.CSV, verbatim                        */
    int         ident_at;    /* which ATIn carries the identification string */
    const char *ident;       /* ...and its default text                      */
    int         fmw_at;      /* MD_NAME column 7, filed as MODEMFMW          */
    const char *fmw;         /* ...and its default text                      */
    int         country_at;  /* which ATIn reports the country code          */
    const char *info[10];    /* the rest of ATI0..ATI9                       */
} modem_model_t;

static const modem_model_t modem_models[] = {
    // clang-format off
    [MODEM_MODEL_SUPRA] = {
        .name       = "Diamond SupraExpress 56e PRO",
        .ident_at   = 3, .ident = "SupraExpress 56e PRO",
        .fmw_at     = 7, .fmw   = "V1.100-V90_2M_DLS",
        .country_at = 5,
        .info       = {
            [0] = "1794",                                    /* product code */
            [1] = "168",                                     /* ROM checksum */
            [2] = "OK",                                      /* RAM test     */
            [4] = "Diamond Multimedia SupraExpress 56e PRO",
            [6] = "RCVDL56ACF/SP Rev 1.100"                  /* the chipset  */
        }
    },
    [MODEM_MODEL_ELSA] = {
        .name       = "ELSA MicroLink 56k",
        .ident_at   = 6, .ident = "ELSA MicroLink 56k",
        .fmw_at     = 3, .fmw   = "2.274",
        .country_at = 5,
        .info       = {
            [0] = "1442",
            [1] = "168",
            [2] = "OK",
            [4] = "ELSA MicroLink 56k",
            [7] = "ELSA AG, Aachen"
        }
    }
    // clang-format on
};

/* S-register power-on values.  Rockwell defaults; the cabinet overwrites S7,
   S8 and S10 in its own init string and leaves the rest alone. */
static const uint8_t modem_s_defaults[100] = {
    [0] = 0,   /* rings to auto-answer on -- 0 is "never"     */
    [2] = 43,  /* escape character, '+'                        */
    [3] = 13,  /* carriage return                              */
    [4] = 10,  /* line feed                                    */
    [5] = 8,   /* backspace                                    */
    [6] = 2,   /* wait before blind dial, seconds              */
    [7] = 50,  /* wait for carrier after dial, seconds         */
    [8] = 2,   /* comma pause, seconds                         */
    [9] = 6,   /* carrier detect response time, tenths         */
    [10] = 14, /* carrier loss disconnect delay, tenths        */
    [11] = 95, /* DTMF duration, ms                            */
    [12] = 50  /* escape guard time, 20 ms units               */
};

typedef struct {
    void        *log;
    char_port_t *port;

    /* Snapshot of the device config.  device_get_config_*() only works inside
       init(); read()/write() run much later off the serial port's timers.  Same
       reasoning as char_fujinet.c. */
    const modem_model_t *model;
    int  line;
    char host[128];
    int  host_port;
    char ident[64];    /* the identification string, at model->ident_at    */
    char firmware[64]; /* what the cabinet files as MODEMFMW               */
    int  connect_rate;

    /* DTE-facing output ring. */
    uint8_t  out[MODEM_OUT_SIZE];
    uint32_t out_head;
    uint32_t out_tail;

    /* Command line being collected. */
    char     cmd[MODEM_CMD_SIZE];
    char     lastcmd[MODEM_CMD_SIZE];
    uint32_t cmdlen;

    /* Configuration the DTE has set. */
    uint8_t s[100];
    int     echo;
    int     verbose;
    int     quiet;
    int     xresult;  /* ATX -- how much of the result set is in use      */
    int     wresult;  /* ATW -- whose speed CONNECT reports               */
    int     dcd_mode; /* AT&C                                             */
    int     dsr_mode; /* AT&S                                             */
    int     dtr_mode; /* AT&D                                             */
    int     flow;     /* AT&K                                             */
    int     country;  /* AT*NC / AT+GCI                                   */
    int     online;   /* data mode rather than command mode               */

    /* Line side. */
    int      state;
    SOCKET   sock;
    uint32_t deadline; /* plat_get_ticks() value the current wait ends at  */
    int      said_connect; /* ANSWERING: the CONNECT line has been queued  */
    int      dtr;

    /* +++ escape detection. */
    uint32_t last_data;
    int      pluses;
} modem_t;

/* ------------------------------------------------------------------ output */

static void
modem_out_byte(modem_t *dev, uint8_t val)
{
    uint32_t next = (dev->out_head + 1) % MODEM_OUT_SIZE;

    if (next == dev->out_tail)
        return; /* ring full: a real modem would have asserted flow control */
    dev->out[dev->out_head] = val;
    dev->out_head           = next;
}

static void
modem_out_str(modem_t *dev, const char *s)
{
    while (*s != '\0')
        modem_out_byte(dev, (uint8_t) *s++);
}

/* One information line, the way a verbose modem frames it. */
static void
modem_out_line(modem_t *dev, const char *s)
{
    modem_out_byte(dev, dev->s[3]);
    modem_out_byte(dev, dev->s[4]);
    modem_out_str(dev, s);
    modem_out_byte(dev, dev->s[3]);
    modem_out_byte(dev, dev->s[4]);
}

static void
modem_result(modem_t *dev, int code)
{
    char buf[64];

    if ((code == RES_NONE) || dev->quiet)
        return;

    /* X0 collapses everything the extended set added back onto the basic four. */
    if (dev->xresult == 0) {
        switch (code) {
            case RES_NO_DIALTONE:
            case RES_BUSY:
            case RES_NO_ANSWER:
                code = RES_NO_CARRIER;
                break;
            default:
                break;
        }
    } else {
        /* Dial tone detection is only in X2 and X4; busy detection in X3 and X4. */
        if ((code == RES_NO_DIALTONE) && (dev->xresult != 2) && (dev->xresult != 4))
            code = RES_NO_CARRIER;
        if ((code == RES_BUSY) && (dev->xresult < 3))
            code = RES_NO_CARRIER;
    }

    if (code == RES_CONNECT) {
        /* X0 says CONNECT and nothing else; anything above reports a speed,
           and W2 makes that the carrier's rather than the port's. */
        if (dev->xresult == 0)
            snprintf(buf, sizeof(buf), "CONNECT");
        else if (dev->wresult == 2)
            snprintf(buf, sizeof(buf), "CONNECT %d/V90", dev->connect_rate);
        else
            snprintf(buf, sizeof(buf), "CONNECT %d", dev->connect_rate);
    } else
        snprintf(buf, sizeof(buf), "%s", modem_res_text[code]);

    if (dev->verbose)
        modem_out_line(dev, buf);
    else {
        char num[8];

        snprintf(num, sizeof(num), "%d", code);
        modem_out_str(dev, num);
        modem_out_byte(dev, dev->s[3]);
    }

    char_modem_log(dev->log, "-> %s\n", buf);
}

/* ------------------------------------------------------------------- line */

static void
modem_hangup(modem_t *dev)
{
    if (CHAR_FD_VALID(dev->sock)) {
        plat_netsocket_close(dev->sock);
        dev->sock = (SOCKET) -1;
    }
    dev->state  = MODEM_ST_IDLE;
    dev->online = 0;
    dev->pluses = 0;
    char_update_status(dev->port);
}

/* The carrier is up: start the CONNECT sequence described above. */
static void
modem_answered(modem_t *dev)
{
    dev->state        = MODEM_ST_ANSWERING;
    dev->said_connect = 0;
    dev->deadline     = plat_get_ticks() + MODEM_TRAIN_MS;
}

/* CONNECT has been read: raise DCD, go on line. */
static void
modem_online(modem_t *dev)
{
    dev->state  = MODEM_ST_ONLINE;
    dev->online = 1;
    char_update_status(dev->port);
}

/* Everything a dialled string can contain that is not a digit: the dial
   modifiers.  The number itself is never used -- there is one host. */
static void
modem_dial(modem_t *dev, const char *number)
{
    uint32_t now = plat_get_ticks();

    (void) number; /* there is one line: the digits are only worth logging */
    char_modem_log(dev->log, "dial \"%s\"\n", number);

    if (dev->line == MODEM_LINE_TCP) {
        dev->sock = plat_netsocket_create(NET_SOCKET_TCP);
        if (!CHAR_FD_VALID(dev->sock) ||
            (plat_netsocket_connect(dev->sock, dev->host, (unsigned short) dev->host_port) != 0)) {
            if (CHAR_FD_VALID(dev->sock)) {
                plat_netsocket_close(dev->sock);
                dev->sock = (SOCKET) -1;
            }
            dev->state    = MODEM_ST_DIALING;
            dev->deadline = now + 1500;
            return; /* fail after a plausible pause rather than instantly */
        }
        dev->state    = MODEM_ST_CONNECTING;
        dev->deadline = now + (dev->s[7] * 1000u);
        return;
    }

    /* No line.  A modem that can hear no dial tone says so within a second or
       two; one that cannot listen for it (X0/X1/X3) dials blind and waits out
       S7 for a carrier that never comes. */
    dev->state    = MODEM_ST_DIALING;
    dev->deadline = now + (((dev->xresult == 2) || (dev->xresult == 4))
                               ? 1500u
                               : (dev->s[7] * 1000u));
}

static void
modem_poll_line(modem_t *dev)
{
    uint32_t now = plat_get_ticks();

    switch (dev->state) {
        case MODEM_ST_DIALING:
            if ((int32_t) (now - dev->deadline) < 0)
                break;
            modem_hangup(dev);
            modem_result(dev, ((dev->line == MODEM_LINE_DEAD) &&
                               ((dev->xresult == 2) || (dev->xresult == 4)))
                                  ? RES_NO_DIALTONE
                                  : RES_NO_CARRIER);
            break;

        case MODEM_ST_CONNECTING: {
            const int connected = plat_netsocket_connected(dev->sock);

            if (connected == 1)
                modem_answered(dev);
            else if ((connected == -1) || ((int32_t) (now - dev->deadline) >= 0)) {
                modem_hangup(dev);
                modem_result(dev, RES_NO_CARRIER);
            }
            break;
        }

        case MODEM_ST_ANSWERING:
            if ((int32_t) (now - dev->deadline) < 0)
                break;
            if (!dev->said_connect) {
                modem_result(dev, RES_CONNECT);
                dev->said_connect = 1;
                dev->deadline     = now + MODEM_SETTLE_MS;
            } else if (dev->out_tail == dev->out_head)
                modem_online(dev); /* the DTE has read the line */
            break;

        default:
            break;
    }
}

/* ---------------------------------------------------------------- commands */

/* Read a decimal argument, defaulting to 0 the way every AT command does. */
static int
modem_arg(const char **p)
{
    int val = 0;

    while (**p == ' ')
        (*p)++;
    if (!isdigit((unsigned char) **p))
        return 0;
    while (isdigit((unsigned char) **p))
        val = (val * 10) + (*(*p)++ - '0');
    return val;
}

/* The `ATIn` responses.  Every one of these is load-bearing: this is how the
   cabinet decides which of thirty modems it is looking at, so a stray substring
   would make it report the wrong one.  See the model table above. */
static void
modem_info(modem_t *dev, int n)
{
    char buf[80];

    if ((n < 0) || (n > 9))
        return;

    if (n == dev->model->ident_at)
        modem_out_line(dev, dev->ident);
    else if (n == dev->model->fmw_at)
        modem_out_line(dev, dev->firmware);
    else if (n == dev->model->country_at) {
        snprintf(buf, sizeof(buf), "Country Code: %02X", dev->country);
        modem_out_line(dev, buf);
    } else if (dev->model->info[n] != NULL)
        modem_out_line(dev, dev->model->info[n]);
}

static void
modem_load_defaults(modem_t *dev)
{
    memcpy(dev->s, modem_s_defaults, sizeof(dev->s));
    dev->echo     = 1;
    dev->verbose  = 1;
    dev->quiet    = 0;
    dev->xresult  = 4;
    dev->wresult  = 0;
    dev->dcd_mode = 1;
    dev->dsr_mode = 0;
    dev->dtr_mode = 2;
    dev->flow     = 3;
}

/* Returns the result code to report, or RES_NONE when the command has already
   answered for itself (a dial in progress, an info line). */
static int
modem_execute(modem_t *dev, const char *line)
{
    const char *p = line;

    while (*p != '\0') {
        const char c = (char) toupper((unsigned char) *p++);

        switch (c) {
            case ' ':
                break;

            case 'A': /* answer -- there is never an incoming call */
                return RES_NO_CARRIER;

            case 'D': { /* dial */
                char number[64];
                int  n = 0;

                if (dev->state != MODEM_ST_IDLE)
                    return RES_ERROR;
                while ((*p != '\0') && (n < ((int) sizeof(number) - 1))) {
                    const char d = *p++;

                    if (isdigit((unsigned char) d) || (d == '*') || (d == '#'))
                        number[n++] = d;
                }
                number[n] = '\0';
                modem_dial(dev, number);
                return RES_NONE;
            }

            case 'E':
                dev->echo = !!modem_arg(&p);
                break;

            case 'H':
                if (modem_arg(&p) == 0)
                    modem_hangup(dev);
                break;

            case 'I':
                modem_info(dev, modem_arg(&p));
                break;

            case 'O': /* back to data mode */
                if (dev->state != MODEM_ST_ONLINE)
                    return RES_ERROR;
                (void) modem_arg(&p);
                dev->online = 1;
                return RES_NONE;

            case 'Q':
                dev->quiet = !!modem_arg(&p);
                break;

            case 'S': { /* S-register read or write */
                const int reg = modem_arg(&p);

                if (reg >= 100)
                    return RES_ERROR;
                while (*p == ' ')
                    p++;
                if (*p == '=') {
                    p++;
                    dev->s[reg] = (uint8_t) modem_arg(&p);
                } else if (*p == '?') {
                    char buf[8];

                    p++;
                    snprintf(buf, sizeof(buf), "%03d", dev->s[reg]);
                    modem_out_line(dev, buf);
                } else
                    return RES_ERROR;
                break;
            }

            case 'V':
                dev->verbose = !!modem_arg(&p);
                break;

            case 'W':
                dev->wresult = modem_arg(&p);
                break;

            case 'X':
                dev->xresult = modem_arg(&p);
                break;

            case 'Z': /* reset and load a stored profile */
                (void) modem_arg(&p);
                modem_hangup(dev);
                modem_load_defaults(dev);
                break;

            /* Speaker, dial mode, modulation selection and error control:
               accepted, and nothing here has a speaker or a modulation. */
            case 'B':
            case 'L':
            case 'M':
            case 'N':
            case 'P':
            case 'T':
                (void) modem_arg(&p);
                break;

            case '&': { /* the ampersand set */
                const char a = (char) toupper((unsigned char) *p++);

                switch (a) {
                    case 'C':
                        dev->dcd_mode = modem_arg(&p);
                        char_update_status(dev->port);
                        break;
                    case 'D':
                        dev->dtr_mode = modem_arg(&p);
                        break;
                    case 'K':
                        dev->flow = modem_arg(&p);
                        break;
                    case 'S':
                        dev->dsr_mode = modem_arg(&p);
                        char_update_status(dev->port);
                        break;
                    case 'F': /* load factory profile n */
                        (void) modem_arg(&p);
                        modem_load_defaults(dev);
                        break;
                    /* Stored profiles: there is no NVRAM behind these, but the
                       cabinet writes one (`AT&F2&W`) on every country change and
                       an ERROR there would abort its init. */
                    case 'W':
                    case 'Y':
                    case 'Z':
                        (void) modem_arg(&p);
                        if (*p == '=')
                            while ((*p != '\0') && (*p != ';'))
                                p++;
                        break;
                    case 'V': /* view active profile */
                        modem_out_line(dev, dev->ident);
                        break;
                    case '\0':
                        return RES_ERROR;
                    default:
                        (void) modem_arg(&p);
                        break;
                }
                break;
            }

            case '\\': /* error control and flow control, Microcom style */
            case '%':  /* Rockwell extended parameters, e.g. AT%C3 */
            case '-':  /* -J and friends */
                if (*p == '\0')
                    return RES_ERROR;
                p++;
                (void) modem_arg(&p);
                break;

            case '*': { /* Rockwell country select: AT*NC<n> */
                const char a = (char) toupper((unsigned char) *p);

                if (a == '\0')
                    return RES_ERROR;
                p++;
                if (toupper((unsigned char) *p) == 'C')
                    p++;
                if (a == 'N')
                    dev->country = modem_arg(&p);
                else
                    (void) modem_arg(&p);
                break;
            }

            case '+': { /* the ITU extended set */
                char name[16];
                int  n = 0;

                while ((*p != '\0') && (*p != '=') && (*p != '?') &&
                       (n < ((int) sizeof(name) - 1)))
                    name[n++] = (char) toupper((unsigned char) *p++);
                name[n] = '\0';

                if (!strcmp(name, "GCI")) {
                    /* Country of installation, T.35 code.  The cabinet uses this
                       on the ELSA and Conexant parts rather than on this one. */
                    if (*p == '?') {
                        char buf[16];

                        p++;
                        snprintf(buf, sizeof(buf), "+GCI: %02X", dev->country);
                        modem_out_line(dev, buf);
                    } else if (*p == '=') {
                        char *end = NULL;

                        dev->country = (int) strtol(p + 1, &end, 16);
                        p            = end;
                    } else
                        return RES_ERROR;
                } else if (!strcmp(name, "MS") || !strcmp(name, "FCLASS") ||
                           !strcmp(name, "IFC") || !strcmp(name, "ES")) {
                    /* Modulation, fax class and flow control: accepted whole. */
                    while ((*p != '\0') && (*p != ';'))
                        p++;
                } else
                    return RES_ERROR;
                break;
            }

            default:
                return RES_ERROR;
        }
    }

    return RES_OK;
}

static void
modem_command(modem_t *dev, const char *line)
{
    int res;

    char_modem_log(dev->log, "<- AT%s\n", line);

    /* A/ re-runs the stored line, so this can be handed its own buffer -- hence
       memmove rather than strncpy. */
    {
        size_t n = strlen(line);

        if (n > (sizeof(dev->lastcmd) - 1))
            n = sizeof(dev->lastcmd) - 1;
        memmove(dev->lastcmd, line, n);
        dev->lastcmd[n] = '\0';
    }

    res = modem_execute(dev, line);
    modem_result(dev, res);
}

/* --------------------------------------------------------------- DTE input */

static void
modem_command_byte(modem_t *dev, uint8_t val)
{
    /* Backspace edits the line, and is echoed as destructive. */
    if (val == dev->s[5]) {
        if (dev->cmdlen > 0) {
            dev->cmdlen--;
            if (dev->echo) {
                modem_out_byte(dev, dev->s[5]);
                modem_out_byte(dev, ' ');
                modem_out_byte(dev, dev->s[5]);
            }
        }
        return;
    }

    if (dev->echo && (val != dev->s[4]))
        modem_out_byte(dev, val);

    if (val == dev->s[3]) {
        dev->cmd[dev->cmdlen] = '\0';

        /* A/ repeats the last line and takes no terminator; a bare CR, which is
           how the cabinet wakes the port up, is not a command at all. */
        if (dev->cmdlen >= 2) {
            if ((toupper((unsigned char) dev->cmd[0]) == 'A') &&
                (toupper((unsigned char) dev->cmd[1]) == 'T'))
                modem_command(dev, &dev->cmd[2]);
            else
                modem_result(dev, RES_ERROR);
        } else if (dev->cmdlen > 0)
            modem_result(dev, RES_ERROR);

        dev->cmdlen = 0;
        return;
    }

    if (val == dev->s[4])
        return; /* LF inside a command line is ignored */

    /* A/ has no CR: it fires the moment the slash arrives. */
    if ((dev->cmdlen == 1) && (val == '/') &&
        (toupper((unsigned char) dev->cmd[0]) == 'A')) {
        dev->cmdlen = 0;
        modem_command(dev, dev->lastcmd);
        return;
    }

    if (dev->cmdlen < (MODEM_CMD_SIZE - 1))
        dev->cmd[dev->cmdlen++] = (char) val;
}

static void
modem_data_byte(modem_t *dev, uint8_t val)
{
    const uint32_t now   = plat_get_ticks();
    const uint32_t guard = dev->s[12] * 20u;

    /* The escape sequence: three copies of S2 with a guard time either side and
       nothing else in between. */
    if (val == dev->s[2]) {
        if ((dev->pluses > 0) || ((now - dev->last_data) >= guard)) {
            dev->pluses++;
            dev->last_data = now;
            if (dev->pluses <= 3)
                return;
        }
    }

    if (dev->pluses > 0) {
        /* Broken by other traffic -- the pluses were data after all. */
        for (int i = 0; i < dev->pluses; i++) {
            int wouldblock = 0;

            if (CHAR_FD_VALID(dev->sock))
                (void) plat_netsocket_send(dev->sock, &dev->s[2], 1, &wouldblock);
        }
        dev->pluses = 0;
    }

    dev->last_data = now;

    if (CHAR_FD_VALID(dev->sock)) {
        int wouldblock = 0;

        (void) plat_netsocket_send(dev->sock, &val, 1, &wouldblock);
    }
}

/* ------------------------------------------------------- char device hooks */

static size_t
modem_read(uint8_t *buf, size_t len, void *priv)
{
    modem_t *dev = (modem_t *) priv;
    size_t   n   = 0;

    modem_poll_line(dev);

    /* The escape sequence completes on silence, not on a fourth character. */
    if (dev->online && (dev->pluses >= 3) &&
        ((plat_get_ticks() - dev->last_data) >= (dev->s[12] * 20u))) {
        dev->pluses = 0;
        dev->online = 0;
        modem_result(dev, RES_OK);
    }

    /* Anything the carrier is delivering goes in front of the ring. */
    if (dev->online && CHAR_FD_VALID(dev->sock)) {
        uint8_t   net[256];
        int       wouldblock = 0;
        const int room       = (int) sizeof(net);
        const int ret        = plat_netsocket_receive(dev->sock, net, room, &wouldblock);

        if (ret > 0) {
            for (int i = 0; i < ret; i++)
                modem_out_byte(dev, net[i]);
        } else if ((ret == 0) || ((ret < 0) && !wouldblock)) {
            modem_hangup(dev);
            modem_result(dev, RES_NO_CARRIER);
        }
    }

    while ((n < len) && (dev->out_tail != dev->out_head)) {
        buf[n++]      = dev->out[dev->out_tail];
        dev->out_tail = (dev->out_tail + 1) % MODEM_OUT_SIZE;
    }

    return n;
}

static size_t
modem_write(uint8_t *buf, size_t len, void *priv)
{
    modem_t *dev = (modem_t *) priv;

    for (size_t i = 0; i < len; i++) {
        if (dev->online)
            modem_data_byte(dev, buf[i]);
        else
            modem_command_byte(dev, buf[i]);
    }

    return len;
}

static uint32_t
modem_status(void *priv)
{
    modem_t *dev    = (modem_t *) priv;
    uint32_t status = 0;

    /* An external modem that is switched on always has CTS: nothing here ever
       needs to throttle the DTE. */
    status |= CHAR_COM_CTS;

    if ((dev->dsr_mode == 0) || (dev->state != MODEM_ST_IDLE))
        status |= CHAR_COM_DSR;

    /* &C0 nails DCD high; &C1 -- the default, and what the cabinet's PPP driver
       watches -- follows the carrier. */
    if ((dev->dcd_mode == 0) || (dev->state == MODEM_ST_ONLINE))
        status |= CHAR_COM_DCD;

    return status;
}

static void
modem_control(uint32_t flags, void *priv)
{
    modem_t  *dev  = (modem_t *) priv;
    const int dtr  = !!(flags & CHAR_COM_DTR);
    const int drop = dev->dtr && !dtr;

    dev->dtr = dtr;

    if (!drop)
        return;

    switch (dev->dtr_mode) {
        case 1: /* back to command mode, carrier kept */
            dev->online = 0;
            break;
        case 2: /* hang up -- the factory default */
            if (dev->state != MODEM_ST_IDLE) {
                modem_hangup(dev);
                modem_result(dev, RES_NO_CARRIER);
            }
            break;
        case 3: /* hang up and reset */
            modem_hangup(dev);
            modem_load_defaults(dev);
            break;
        default: /* &D0: DTR is ignored */
            break;
    }
}

static void
modem_port_config(void *priv)
{
    modem_t *dev = (modem_t *) priv;

    (void) dev; /* the log call compiles away when logging is off */
    char_modem_log(dev->log, "port %u %u%c%u\n", dev->port->com.baud,
                   dev->port->com.data_bits,
                   (dev->port->com.parity & 1) ? 'P' : 'N',
                   dev->port->com.stop_bits);
}

static void
modem_close(void *priv)
{
    modem_t *dev = (modem_t *) priv;

    modem_hangup(dev);
    log_close(dev->log);
    free(dev);
}

static void *
modem_init(const device_t *info)
{
    modem_t    *dev = (modem_t *) calloc(1, sizeof(modem_t));
    const char *s;

    dev->model = &modem_models[(info->local < (int) (sizeof(modem_models) / sizeof(modem_models[0])))
                                   ? info->local
                                   : MODEM_MODEL_SUPRA];
    dev->sock  = (SOCKET) -1;
    modem_load_defaults(dev);

    dev->line         = device_get_config_int("line");
    dev->host_port    = device_get_config_int("host_port");
    dev->connect_rate = device_get_config_int("connect_rate");

    s = device_get_config_string("host");
    snprintf(dev->host, sizeof(dev->host), "%s", (s != NULL) ? s : "");

    /* Both identity strings default out of the model table, so a blank in the
       ini reads as "whatever this part says" rather than as a modem that answers
       nothing and gets filed as Standard-Modem. */
    s = device_get_config_string("ident");
    snprintf(dev->ident, sizeof(dev->ident), "%s",
             ((s != NULL) && (s[0] != '\0')) ? s : dev->model->ident);

    s = device_get_config_string("firmware");
    snprintf(dev->firmware, sizeof(dev->firmware), "%s",
             ((s != NULL) && (s[0] != '\0')) ? s : dev->model->fmw);

    /* A host with nothing in it is a dead line however the selector is set --
       otherwise every dial would stall on a connect to port 0. */
    if ((dev->line == MODEM_LINE_TCP) && (dev->host[0] == '\0'))
        dev->line = MODEM_LINE_DEAD;

    dev->port = char_attach(0, modem_read, modem_write, modem_status,
                            modem_control, modem_port_config, dev);
    dev->log  = char_log_open(dev->port, "Modem");

    char_modem_log(dev->log, "init(): %s, ATI%d \"%s\", ATI%d \"%s\", line %s\n",
                   dev->model->name, dev->model->ident_at, dev->ident,
                   dev->model->fmw_at, dev->firmware,
                   (dev->line == MODEM_LINE_TCP) ? dev->host : "dead");

    return dev;
}


/* One config array for both parts.  What differs between them -- which ATIn
   carries what, and the text of each -- lives in the model table, so the only
   thing the dialog has to know is that there is a telephone line and it may or
   may not go anywhere. */
// clang-format off
static const device_config_t modem_config[] = {
    {
        .name           = "identity",
        .description    = "The modem the fun.net cabinets had on COM4, at 0x2E8 / IRQ 10.\n"
                          "The cabinet identifies it from its own ATI answers.",
        .type           = CONFIG_LABEL,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "line",
        .description    = "Telephone line",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = MODEM_LINE_DEAD,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Not connected",             .value = MODEM_LINE_DEAD },
            { .description = "Dial out to a TCP/IP host", .value = MODEM_LINE_TCP  },
            { .description = ""                                                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "host",
        .description    = "Host",
        .type           = CONFIG_STRING,
        .default_string = "",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "host_port",
        .description    = "Port",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 23,
        .file_filter    = NULL,
        .spinner        = {
            /* 32767 rather than 65535 because device_config_spinner_t is
               int16_t: a wider maximum wraps negative and the box then refuses
               to take a value at all.  Upstream's own modem stops here too. */
            .min = 1,
            .max = 32767
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "connect_rate",
        .description    = "Reported connection speed",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 57600,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "57600", .value = 57600 },
            { .description = "50666", .value = 50666 },
            { .description = "46666", .value = 46666 },
            { .description = "33600", .value = 33600 },
            { .description = "28800", .value = 28800 },
            { .description = "14400", .value = 14400 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        /* Hidden, because the only values that are right are the ones the model
           table already holds and getting them wrong makes the cabinet report
           the wrong modem.  They stay settings so that a rig with the real part
           beside it can be made to agree with it byte for byte: put the captured
           strings in the ini and they win.  Blank means "whatever this part
           says". */
        .name           = "ident",
        .description    = "Identification string, overriding the part's own",
        .type           = CONFIG_STRING | CONFIG_HIDDEN,
        .default_string = "",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "firmware",
        .description    = "Firmware string, overriding the part's own",
        .type           = CONFIG_STRING | CONFIG_HIDDEN,
        .default_string = "",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

/* The names are MD_NAME.CSV's, verbatim, because that is what the cabinet calls
   these parts and what it will print back at the operator. */
const device_t char_modem_supra_com_device = {
    .name          = "Diamond SupraExpress 56e PRO",
    .internal_name = "modem_supra",
    .flags         = DEVICE_COM,
    .local         = MODEM_MODEL_SUPRA,
    .init          = modem_init,
    .close         = modem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = modem_config
};

const device_t char_modem_elsa_com_device = {
    .name          = "ELSA MicroLink 56k",
    .internal_name = "modem_elsa",
    .flags         = DEVICE_COM,
    .local         = MODEM_MODEL_ELSA,
    .init          = modem_init,
    .close         = modem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = modem_config
};
