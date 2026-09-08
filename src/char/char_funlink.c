/*
 * PeepeeBox   A fork of 86Box that emulates the funworld Photo Play / I.G.O.
 *             arcade kiosk hardware, including its protection token.
 *
 *             fun.link: the adapter that joins cabinets together, on COM1.
 *
 * Authors:    The HUEG PP team.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 *
 * ---------------------------------------------------------------------------
 *
 * fun.link is funworld's cabinet-to-cabinet link: a box with a 25-pin D-sub to
 * the machine's I/O connector and a DIN to the next cabinet, and funworld's own
 * advert for it draws four cabinets joined in a ring.  The obvious guess is that
 * it hangs off the parallel port, because that is where the dongle lives.  It
 * does not, and the games say so:
 *
 *     LINK ERROR: No serial-port found !!!!. Please check mainboard COM-settings
 *     LINK ERROR: Unable to send on BUS. Please check LINK-adaptor and LINK-cable
 *
 * It is a multi-drop serial bus.  Every build from 1998/99 to I.G.O. 2 opens it
 * the same way -- the initialiser is called with port index 1 and a baud of
 * 0x1C200, which in funworld's own table is **COM1, 0x3F8, IRQ 4, at 115200 8N1**
 * -- and that fits the rest of the cabinet, where COM3 is the touchscreen and
 * COM4 the fun.net modem.  Sending a byte raises MCR bit 0 (DTR), waits for
 * LSR TEMT, writes THR and drops DTR again, which is what a half-duplex line
 * driver's enable wants.  Arbitration is CSMA done in software: watch the
 * receive counter, back off rand()%90+10 ticks if it moved, fifty tries before
 * giving up.  Frames are 16 bytes of 0x55, a header magic "PHDR", the payload,
 * and a "PHND" trailer.  See docs/research/34-funlink.md for all of it.
 *
 * None of that has to be understood here, and that is the point.  The cabinets
 * talk to each other, not to us: everything above -- arbitration, framing,
 * checksums, the invitation handshake -- happens inside the guests.  What the
 * wire has to do is carry bytes from each cabinet to all the others, so that is
 * all this device does.  It is a repeater, not a protocol.
 *
 * The bus is a TCP connection.  One instance listens and the others connect to
 * it; the listener also repeats what it hears from one peer to all the others,
 * which is what makes three and four cabinets work rather than just two.  In
 * Auto mode an instance tries to connect first and becomes the listener if
 * nothing is there yet, so two copies pointed at the same address and port find
 * each other whichever starts first -- on one machine over the loopback, or
 * across a network with a real address.
 *
 * Two deliberate simplifications, both harmless:
 *
 *   - **DTR is ignored.**  Gating transmission on it would drop bytes, because
 *     the byte sits in the emulated THR for a bit-time after the guest has
 *     already lowered the line.  Nothing is lost: the enable exists to stop two
 *     drivers fighting over one pair, and a TCP stream has no such contention.
 *   - **Collisions never happen.**  Real stations can talk over each other; here
 *     the stream is lossless and ordered.  The guests still run their backoff,
 *     they just always win first time, which is the good case on real hardware
 *     too.
 *
 * The one thing that is a guess is whether a station hears its own transmission.
 * On a real bus that depends on whether the box leaves its receiver enabled while
 * the driver is on, and the box has not been opened.  The default is not to echo
 * -- a cabinet hears only the others -- and `echo` switches it, because if that
 * guess is wrong it is the sort of wrong that a rig with two real machines would
 * settle in a minute.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/ini.h>
#include <86box/char.h>
#include <86box/log.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/plat_netsocket.h>
#include <86box/plat_unused.h>
#include <86box/timer.h> /* serial.h wants pc_timer_t */
#include <86box/serial.h>

/* Four cabinets is what funworld's own advert draws, so three peers plus us. */
#define FUNLINK_MAX_PEERS 3

/* The token in the adapter.  Before the menu will so much as open the bus it
   looks for a DS1982 on this same port and checks what it says; see the 1-Wire
   section below and docs/research/34-funlink.md § "a second iButton". */
#define FUNLINK_OW_MEM      128   /* the device's memory page                  */
#define FUNLINK_OW_RESET_HZ 30000 /* the reset pulse is sent this slowly       */

#define FUNLINK_RX_SIZE   8192 /* one guest's worth of inbound bytes         */
#define FUNLINK_TX_SIZE   1024 /* coalescing buffer, flushed every poll tick */

enum { /* who opens the connection */
       FUNLINK_ROLE_AUTO = 0, /* connect if someone is listening, else listen */
       FUNLINK_ROLE_HOST = 1, /* always listen                                */
       FUNLINK_ROLE_JOIN = 2  /* always connect                               */
};

enum { /* what we are doing right now */
       FUNLINK_ST_IDLE = 0, /* nothing open; a retry is due            */
       FUNLINK_ST_CONNECTING, /* client connect in flight              */
       FUNLINK_ST_CLIENT,   /* connected to a listener                 */
       FUNLINK_ST_LISTENING /* we are the listener; peers may be zero  */
};

enum { /* the 1-Wire slave's transaction state */
       FUNLINK_OW_CMD = 0, /* waiting for a ROM or memory command      */
       FUNLINK_OW_TA,      /* collecting the two address bytes         */
       FUNLINK_OW_DONE     /* answering, or finished                   */
};

#ifdef ENABLE_CHAR_FUNLINK_LOG
int char_funlink_do_log = ENABLE_CHAR_FUNLINK_LOG;

static void
char_funlink_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (char_funlink_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define char_funlink_log(priv, fmt, ...)
#endif

typedef struct { /* the DS1982 inside the adapter */
    uint8_t rom[8];
    uint8_t mem[FUNLINK_OW_MEM];

    uint8_t inbits; /* bits the host has written to us so far  */
    int     nbits;
    uint8_t out[FUNLINK_OW_MEM + 8]; /* bytes queued to shift back out */
    int     out_len;
    int     out_pos; /* bit cursor into out[]                   */

    int     state;
    uint8_t ta[2];
    int     nta;
    int      active; /* a reset has been seen; slots are 1-Wire */
    uint32_t last;   /* when the last slot went through            */
} funlink_ow_t;

typedef struct {
    void        *log;
    char_port_t *port;

    /* Snapshotted at init(): device_get_config_*() is only valid while the
       device context is live, and read()/write() run later off the serial
       port's timers.  Same trap char_fujinet.c documents. */
    char host[256];
    int  host_port;
    int  role;
    int  echo;

    int      state;
    uint32_t next_attempt;  /* throttles reconnects to CHAR_RECONNECT_MS */
    uint32_t last_poll;     /* throttles socket work to once a millisecond */

    SOCKET listener;                      /* FUNLINK_ST_LISTENING only */
    SOCKET peer[FUNLINK_MAX_PEERS];       /* the other cabinets        */
    int    peers;

    uint8_t  rx[FUNLINK_RX_SIZE];
    uint32_t rx_head;
    uint32_t rx_tail;

    uint8_t  tx[FUNLINK_TX_SIZE];
    uint32_t tx_len;

    funlink_ow_t ow;
    int          station; /* 0 = work it out from who hosts the bus */

    /* An optional trace of the wire, for working out where a linked game stalls.
       The cabinets talk to each other and not to us, so when something goes wrong
       between them this is the only place to see it. */
    FILE    *cap;
    uint32_t cap_bytes;
} char_funlink_t;

#define FUNLINK_CAP_MAX (8u * 1024u * 1024u) /* stop before it eats the disk */

static void
funlink_cap(char_funlink_t *dev, const char *what, const uint8_t *buf, int len)
{
    if ((dev->cap == NULL) || (dev->cap_bytes > FUNLINK_CAP_MAX))
        return;

    fprintf(dev->cap, "%9u %-3s %4d ", plat_get_ticks(), what, len);
    for (int i = 0; i < len; i++)
        fprintf(dev->cap, "%02X", buf[i]);
    fputc('\n', dev->cap);
    fflush(dev->cap);
    dev->cap_bytes += (uint32_t) (len * 2 + 20);
}

static void
funlink_cap_note(char_funlink_t *dev, const char *note)
{
    if (dev->cap == NULL)
        return;
    fprintf(dev->cap, "%9u ---  %s\n", plat_get_ticks(), note);
    fflush(dev->cap);
}

/* ------------------------------------------- the token: a DS1982 on the bus
 *
 * The menu will not open the link until it has found a 1-Wire device on this
 * port and liked what it said.  It resets it (0xF0 at ~10 kbaud, so the pulse is
 * long enough), then runs the slots at 115200 -- one UART byte per bit, 0xFF for
 * a one and 0x00 for a zero -- and reads two things:
 *
 *   READ ROM (0x33): 8 bytes.  Byte 0 must be 0x91, and the word at offset 5
 *   shifted right by four must be 0x5E7.  The dword at offset 1 is then taken as
 *   this cabinet's station number on the bus.
 *
 *   SKIP ROM (0xCC), READ MEMORY (0xF0) at address 0: the CRC8 of those three
 *   bytes, then a 128-byte page, then the CRC8 of the page.  Byte 0 of the page
 *   must be 0x37, the dword at offset 1 must be 0x112A, and from offset 5 it must
 *   carry a fixed 46-byte blob of funworld staff surnames.  The whole page is
 *   read twice and the two copies compared.
 *
 * Every one of those values is identical in the 1998/99, 2000 and 2001 menus, so
 * one record serves all three.  None of it is a licence in any cryptographic
 * sense -- there is no challenge and no secret, just a part that has to be there
 * and say the right thing, which is what makes the adapter a box rather than a
 * cable.
 *
 * The state machine is the one in dongle_photoplay.c's ib_*, which was validated
 * against the real software for the cabinet's own iButton at 0x268.  It is a
 * different part in a different place, so it is written out again here rather
 * than shared -- with one addition that half does not need: the trailing CRC over
 * the page, which this reader does check.
 */

static void funlink_rx_push(char_funlink_t *dev, const uint8_t *buf, int len);

/* Maxim/Dallas CRC8, reflected polynomial 0x8C. */
static uint8_t funlink_crc8_tab[256];
static int     funlink_crc8_ready = 0;

static void
funlink_crc8_init(void)
{
    if (funlink_crc8_ready)
        return;
    for (int i = 0; i < 256; i++) {
        uint8_t c = (uint8_t) i;

        for (int b = 0; b < 8; b++)
            c = (c & 1) ? (uint8_t) ((c >> 1) ^ 0x8C) : (uint8_t) (c >> 1);
        funlink_crc8_tab[i] = c;
    }
    funlink_crc8_ready = 1;
}

static uint8_t
funlink_crc8(const uint8_t *d, int n, uint8_t crc)
{
    for (int i = 0; i < n; i++)
        crc = funlink_crc8_tab[crc ^ d[i]];
    return crc;
}

/* Build the record.  The station number is the one thing about it that is ours
   rather than funworld's: the cabinets each had their own adapter and so their
   own number, and two stations answering to the same one cannot be told apart on
   the bus.  Left at 0 it follows who ended up hosting -- which is right for the
   usual pair and wrong for a third cabinet, so the option exists. */
static void
funlink_ow_load(char_funlink_t *dev)
{
    /* The blob the page has to carry, verbatim, from offset 5. */
    static const char licence[] = "Seiringer\rPichler\rHutmacher\rObermair\rLukarsch\r";
    funlink_ow_t     *ow        = &dev->ow;
    uint32_t          station   = (uint32_t) dev->station;

    if (station == 0)
        station = (dev->state == FUNLINK_ST_LISTENING) ? 1 : 2;

    memset(ow->rom, 0, sizeof(ow->rom));
    memset(ow->mem, 0, sizeof(ow->mem));

    ow->rom[0] = 0x91;                            /* checked */
    ow->rom[1] = (uint8_t) (station & 0xFF);      /* the station number, dword */
    ow->rom[2] = (uint8_t) ((station >> 8) & 0xFF);
    ow->rom[3] = (uint8_t) ((station >> 16) & 0xFF);
    ow->rom[4] = (uint8_t) ((station >> 24) & 0xFF);
    ow->rom[5] = 0x70;                            /* (rom[5] | rom[6]<<8) >> 4 */
    ow->rom[6] = 0x5E;                            /*   has to come out 0x5E7   */
    ow->rom[7] = funlink_crc8(ow->rom, 7, 0);     /* not checked here; correct anyway */

    ow->mem[0] = 0x37;
    ow->mem[1] = 0x2A; /* the dword at offset 1 is 0x0000112A */
    ow->mem[2] = 0x11;
    memcpy(ow->mem + 5, licence, sizeof(licence) - 1);
}

static void
funlink_ow_reset(char_funlink_t *dev)
{
    funlink_ow_t *ow = &dev->ow;

    ow->inbits  = 0;
    ow->nbits   = 0;
    ow->out_len = 0;
    ow->out_pos = 0;
    ow->state   = FUNLINK_OW_CMD;
    ow->nta     = 0;
}

static void
funlink_ow_queue(funlink_ow_t *ow, const uint8_t *d, int n)
{
    if (n > (int) sizeof(ow->out))
        n = (int) sizeof(ow->out);
    memcpy(ow->out, d, (size_t) n);
    ow->out_len = n;
    ow->out_pos = 0;
}

/* a whole byte has been clocked in from the host */
static void
funlink_ow_on_byte(char_funlink_t *dev, uint8_t val)
{
    funlink_ow_t *ow = &dev->ow;

    switch (ow->state) {
        case FUNLINK_OW_CMD:
            if (val == 0x33) { /* READ ROM */
                funlink_ow_queue(ow, ow->rom, 8);
                ow->state = FUNLINK_OW_DONE;
                /* Rare and worth having in an ordinary log: this is the moment
                   the cabinet learns it has an adapter, and its number. */
                funlink_cap_note(dev, "guest read the token (READ ROM)");
                pclog("fun.link: the cabinet read the adapter's token, station %u\n",
                      (unsigned) (ow->rom[1] | (ow->rom[2] << 8) |
                                  (ow->rom[3] << 16) | (ow->rom[4] << 24)));
            } else if (val == 0xCC) { /* SKIP ROM -- a command follows */
                ow->state = FUNLINK_OW_CMD;
            } else if (val == 0xF0) { /* READ MEMORY */
                ow->nta   = 0;
                ow->state = FUNLINK_OW_TA;
            } else {
                char_funlink_log(dev->log, "1-Wire: unhandled command %02X\n", val);
                ow->state = FUNLINK_OW_DONE;
            }
            break;

        case FUNLINK_OW_TA:
            ow->ta[ow->nta++] = val;
            if (ow->nta == 2) {
                const int ta     = ow->ta[0] | (ow->ta[1] << 8);
                uint8_t   hdr[3] = { 0xF0, ow->ta[0], ow->ta[1] };
                uint8_t   buf[FUNLINK_OW_MEM + 2];
                int       n = 0;

                /* the CRC of command and address, the page from there on, and
                   then the CRC of what was streamed */
                buf[n++] = funlink_crc8(hdr, 3, 0);
                if (ta < FUNLINK_OW_MEM) {
                    const int len = FUNLINK_OW_MEM - ta;

                    memcpy(buf + n, ow->mem + ta, (size_t) len);
                    n += len;
                    buf[n++] = funlink_crc8(ow->mem + ta, len, 0);
                }
                funlink_ow_queue(ow, buf, n);
                ow->state = FUNLINK_OW_DONE;
                char_funlink_log(dev->log, "1-Wire: READ MEMORY @%04X, %d bytes\n", ta, n);
            }
            break;

        default:
            break;
    }
}

/* One UART byte is one bit slot.  Returns what the wire reads back. */
static uint8_t
funlink_ow_slot(char_funlink_t *dev, uint8_t host)
{
    funlink_ow_t *ow = &dev->ow;

    if (ow->out_pos < (ow->out_len * 8)) {
        /* we are driving: present the next queued bit, LSB first */
        const int bit = (ow->out[ow->out_pos >> 3] >> (ow->out_pos & 7)) & 1;

        ow->out_pos++;
        return bit ? 0xFF : 0x00;
    }

    if (host)
        ow->inbits |= (uint8_t) (1 << ow->nbits);
    if (++ow->nbits == 8) {
        const uint8_t v = ow->inbits;

        ow->inbits = 0;
        ow->nbits  = 0;
        funlink_ow_on_byte(dev, v);
    }
    return host ? 0xFF : 0x00;
}

/* Decide whether a byte the guest just wrote belongs to the token or to the bus,
   and answer it if it is the token's.  Returns 1 when it was consumed.

   The two cannot be told apart by baud rate -- the reset pulse is slow but the
   slots run at the bus's own 115200 -- so the rule is what the protocols
   actually put on the wire.  A reset is 0xF0 sent slowly, and nothing else the
   cabinet does looks like that.  After one, every slot is 0x00 or 0xFF and
   nothing else; the first byte that is neither is the link driver starting to
   talk, and the token steps out of the way. */
static int
funlink_ow_consume(char_funlink_t *dev, uint8_t val)
{
    funlink_ow_t *ow = &dev->ow;

    if ((val == 0xF0) && (dev->port != NULL) && (dev->port->com.baud <= FUNLINK_OW_RESET_HZ)) {
        funlink_ow_load(dev); /* the station number may have settled since */
        funlink_ow_reset(dev);
        ow->active = 1;
        ow->last   = plat_get_ticks();
        /* a present slave corrupts the echo of the reset byte */
        funlink_rx_push(dev, (const uint8_t[]) { 0xE0 }, 1);
        char_funlink_log(dev->log, "1-Wire: reset at %u baud\n", dev->port->com.baud);
        return 1;
    }

    /* A transaction is a dense burst of slots.  If one has gone quiet, the
       cabinet has finished with the token and anything arriving now is the bus
       -- which matters because a slot and a payload byte of 0x00 or 0xFF look
       exactly alike, and swallowing the first byte of a frame would be a hard
       fault to find. */
    if (ow->active && ((plat_get_ticks() - ow->last) > 1000))
        ow->active = 0;

    if (!ow->active)
        return 0;

    if ((val != 0x00) && (val != 0xFF)) {
        char_funlink_log(dev->log, "1-Wire: %02X is not a slot -- back to the bus\n", val);
        ow->active = 0;
        return 0;
    }

    {
        const uint8_t reply = funlink_ow_slot(dev, val);

        funlink_rx_push(dev, &reply, 1);
    }
    ow->last = plat_get_ticks();
    return 1;
}

/* --------------------------------------------------------------- the ring */

static void
funlink_rx_push(char_funlink_t *dev, const uint8_t *buf, int len)
{
    for (int i = 0; i < len; i++) {
        const uint32_t next = (dev->rx_head + 1) % FUNLINK_RX_SIZE;

        /* A full ring means the guest has stopped draining the port, which on
           this bus means it is not in a linked game.  Dropping is the only
           option and is what a real station does with traffic it is not
           listening to. */
        if (next == dev->rx_tail)
            return;
        dev->rx[dev->rx_head] = buf[i];
        dev->rx_head          = next;
    }
}

/* ------------------------------------------------------------- the sockets */

static void
funlink_drop_peer(char_funlink_t *dev, int idx)
{
    if (CHAR_FD_VALID(dev->peer[idx]))
        plat_netsocket_close(dev->peer[idx]);
    for (int i = idx; i < (dev->peers - 1); i++)
        dev->peer[i] = dev->peer[i + 1];
    dev->peer[--dev->peers] = (SOCKET) -1;

    /* A connect that never completed is not a cabinet leaving, and saying so
       every half second while nothing is listening is noise. */
    if ((dev->state == FUNLINK_ST_LISTENING) || (dev->state == FUNLINK_ST_CLIENT))
        pclog("fun.link: cabinet left the bus (%d still on it)\n", dev->peers);
}

static void
funlink_close_all(char_funlink_t *dev)
{
    while (dev->peers > 0)
        funlink_drop_peer(dev, dev->peers - 1);
    if (CHAR_FD_VALID(dev->listener)) {
        plat_netsocket_close(dev->listener);
        dev->listener = (SOCKET) -1;
    }
    dev->state  = FUNLINK_ST_IDLE;
    dev->tx_len = 0;
}

/* Push `buf` at every peer except `except` (-1 for all of them).  A peer that
   errors is dropped, which is why this walks the array backwards. */
static void
funlink_send_to_peers(char_funlink_t *dev, const uint8_t *buf, int len, SOCKET except)
{
    if (len <= 0)
        return;

    for (int i = dev->peers - 1; i >= 0; i--) {
        int wouldblock = 0;
        int sent;

        if (dev->peer[i] == except)
            continue;

        sent = plat_netsocket_send(dev->peer[i], buf, (unsigned int) len, &wouldblock);
        if ((sent < 0) && !wouldblock) {
            char_funlink_log(dev->log, "send() failed, dropping peer %d\n", i);
            funlink_drop_peer(dev, i);
        }
    }
}

static void
funlink_flush_tx(char_funlink_t *dev)
{
    if (dev->tx_len == 0)
        return;

    /* Only a settled bus takes bytes.  While a connect is still in flight the
       socket in peer[0] is not a bus member yet and writing to it would look
       like a peer dying; with no bus at all there is nobody to hear.  Either way
       the cabinet is talking into an unterminated wire, which is exactly what a
       real one does with no cable in the adapter. */
    if ((dev->state == FUNLINK_ST_CLIENT) || (dev->state == FUNLINK_ST_LISTENING)) {
        funlink_cap(dev, "TX", dev->tx, (int) dev->tx_len);
        funlink_send_to_peers(dev, dev->tx, (int) dev->tx_len, (SOCKET) -1);
    } else
        funlink_cap(dev, "TX?", dev->tx, (int) dev->tx_len); /* nowhere to go */

    dev->tx_len = 0;
}

/* Become the listener.  Returns 1 when the port is ours. */
static int
funlink_start_listening(char_funlink_t *dev)
{
    dev->listener = plat_netsocket_create_server(NET_SOCKET_TCP,
                                                 (unsigned short) dev->host_port);
    if (!CHAR_FD_VALID(dev->listener)) {
        dev->listener = (SOCKET) -1;
        return 0;
    }

    dev->state = FUNLINK_ST_LISTENING;
    pclog("fun.link: waiting for cabinets on port %d\n", dev->host_port);
    return 1;
}

static void
funlink_start_connecting(char_funlink_t *dev)
{
    SOCKET sock = plat_netsocket_create(NET_SOCKET_TCP);

    if (!CHAR_FD_VALID(sock))
        return;

    if (plat_netsocket_connect(sock, dev->host, (unsigned short) dev->host_port) != 0) {
        plat_netsocket_close(sock);
        return;
    }

    dev->peer[0] = sock;
    dev->peers   = 1;
    dev->state   = FUNLINK_ST_CONNECTING;
}

/* Everything that touches a socket, throttled to once a millisecond.  read() is
   called once per bit time -- ~115000 times a second at this baud rate -- so it
   is the heartbeat for the whole device, but it must not be the rate at which
   the network is asked anything. */
static void
funlink_poll(char_funlink_t *dev)
{
    const uint32_t now = plat_get_ticks();
    uint8_t        buf[512];

    if (now == dev->last_poll)
        return;
    dev->last_poll = now;

    switch (dev->state) {
        case FUNLINK_ST_IDLE:
            if ((dev->next_attempt != 0) && ((now - dev->next_attempt) < CHAR_RECONNECT_MS))
                break;
            dev->next_attempt = now ? now : 1;

            if (dev->role == FUNLINK_ROLE_HOST)
                (void) funlink_start_listening(dev);
            else
                funlink_start_connecting(dev);
            break;

        case FUNLINK_ST_CONNECTING: {
            const int connected = plat_netsocket_connected(dev->peer[0]);

            if (connected == 1) {
                dev->state = FUNLINK_ST_CLIENT;
                pclog("fun.link: joined the bus at %s:%d\n", dev->host, dev->host_port);
                break;
            }
            if (connected == 0) {
                /* Still in flight.  Give it CHAR_RECONNECT_MS before deciding
                   nobody is there -- a connect to a listening socket on the
                   loopback completes long inside that. */
                if ((now - dev->next_attempt) < CHAR_RECONNECT_MS)
                    break;
            }

            funlink_close_all(dev);
            dev->next_attempt = now ? now : 1;

            /* Auto: nothing was listening, so listen ourselves.  If the bind
               loses a race with another instance doing the same thing, fall
               back to connecting on the next attempt. */
            if (dev->role == FUNLINK_ROLE_AUTO)
                (void) funlink_start_listening(dev);
            break;
        }

        case FUNLINK_ST_LISTENING:
            while (dev->peers < FUNLINK_MAX_PEERS) {
                SOCKET sock = plat_netsocket_accept(dev->listener);

                if (!CHAR_FD_VALID(sock))
                    break;
                dev->peer[dev->peers++] = sock;
                pclog("fun.link: a cabinet joined the bus (%d on it)\n", dev->peers);
            }
            break;

        default:
            break;
    }

    /* A connect still in flight has a socket in peer[0] that is not a bus member
       yet, so there is nothing to drain and nothing to say. */
    if (dev->state == FUNLINK_ST_CONNECTING)
        return;

    /* Anything a peer has to say goes to this cabinet, and -- when we are the
       listener -- on to every other cabinet, because on a real bus they all
       hear it.  Backwards, because a dead peer is removed in place. */
    for (int i = dev->peers - 1; i >= 0; i--) {
        int wouldblock = 0;
        int ret;

        ret = plat_netsocket_receive(dev->peer[i], buf, (unsigned int) sizeof(buf), &wouldblock);
        if (ret > 0) {
            funlink_cap(dev, "RX", buf, ret);
            funlink_rx_push(dev, buf, ret);
            if (dev->state == FUNLINK_ST_LISTENING)
                funlink_send_to_peers(dev, buf, ret, dev->peer[i]);
        } else if ((ret == 0) || ((ret < 0) && !wouldblock)) {
            funlink_drop_peer(dev, i);

            /* A client that lost the listener starts over; a listener that lost
               a peer keeps waiting for it to come back. */
            if (dev->state == FUNLINK_ST_CLIENT) {
                funlink_close_all(dev);
                dev->next_attempt = now ? now : 1;
                break;
            }
        }
    }

    funlink_flush_tx(dev);
}

/* ------------------------------------------------------- char device hooks */

static size_t
funlink_read(uint8_t *buf, size_t len, void *priv)
{
    char_funlink_t *dev = (char_funlink_t *) priv;
    size_t          n   = 0;

    funlink_poll(dev);

    while ((n < len) && (dev->rx_tail != dev->rx_head)) {
        buf[n++]     = dev->rx[dev->rx_tail];
        dev->rx_tail = (dev->rx_tail + 1) % FUNLINK_RX_SIZE;
    }

    return n;
}

static size_t
funlink_write(uint8_t *buf, size_t len, void *priv)
{
    char_funlink_t *dev = (char_funlink_t *) priv;

    for (size_t i = 0; i < len; i++) {
        /* The token lives on this same wire, and while the cabinet is talking to
           it none of that belongs on the bus -- the other cabinets have tokens
           of their own. */
        if (funlink_ow_consume(dev, buf[i]))
            continue;

        /* Held until the next poll rather than sent a byte at a time: the serial
           port hands them over one by one, and one TCP write per byte at 115200
           is a lot of syscalls for a bus whose backoff is in DOS ticks. */
        if (dev->tx_len >= FUNLINK_TX_SIZE)
            funlink_flush_tx(dev);
        dev->tx[dev->tx_len++] = buf[i];

        /* Whether a station hears itself is the one thing about the real box
           that is not known.  Off by default; see the file comment. */
        if (dev->echo)
            funlink_rx_push(dev, &buf[i], 1);
    }

    return len;
}

static void
funlink_control(uint32_t flags, void *priv)
{
    char_funlink_t *dev = (char_funlink_t *) priv;

    (void) dev; /* the log call compiles away when logging is off */

    /* DTR is the transmit enable on the real adapter.  Nothing to do with it
       here -- see the file comment for why gating on it would lose bytes. */
    char_funlink_log(dev->log, "control(%08X) dtr=%d rts=%d\n", flags,
                     !!(flags & CHAR_COM_DTR), !!(flags & CHAR_COM_RTS));
}

static void
funlink_port_config(void *priv)
{
    char_funlink_t *dev = (char_funlink_t *) priv;

    (void) dev;
    char_funlink_log(dev->log, "port %u %u%c%u\n", dev->port->com.baud,
                     dev->port->com.data_bits,
                     (dev->port->com.parity & 1) ? 'P' : 'N',
                     dev->port->com.stop_bits);
}

static void
funlink_close(void *priv)
{
    char_funlink_t *dev = (char_funlink_t *) priv;

    if (dev->cap != NULL) {
        funlink_cap_note(dev, "capture end");
        fclose(dev->cap);
        dev->cap = NULL;
    }
    funlink_close_all(dev);
    log_close(dev->log);
    free(dev);
}

static void *
funlink_init(UNUSED(const device_t *info))
{
    char_funlink_t *dev = (char_funlink_t *) calloc(1, sizeof(char_funlink_t));
    const char     *s;

    dev->listener = (SOCKET) -1;
    for (int i = 0; i < FUNLINK_MAX_PEERS; i++)
        dev->peer[i] = (SOCKET) -1;

    dev->role      = device_get_config_int("role");
    dev->host_port = device_get_config_int("host_port");
    dev->echo      = device_get_config_int("echo");
    dev->station   = device_get_config_int("station");

    if (device_get_config_int("capture")) {
        char fn[1024];

        path_append_filename(fn, exe_path, "funlink-capture.txt");
        dev->cap = fopen(fn, "w");
        if (dev->cap != NULL) {
            pclog("fun.link: tracing the wire to %s\n", fn);
            funlink_cap_note(dev, "capture start");
        }
    }

    funlink_crc8_init();
    funlink_ow_load(dev);
    funlink_ow_reset(dev);

    s = device_get_config_string("host");
    snprintf(dev->host, sizeof(dev->host), "%s",
             ((s != NULL) && (s[0] != '\0')) ? s : "127.0.0.1");

    /* No status callback, deliberately.  The adapter is a line driver: it does
       not assert CTS, DSR or DCD, and the link driver reads none of them -- it
       works off LSR and drives MCR.  Reporting anything at all is worse than
       useless here, because serial_receive_timer() latches MSR delta bits and
       re-raises the modem-status interrupt on every tick until the guest reads
       MSR, which nothing on this port ever does.  COM1 shares IRQ 4 with COM3,
       so that storm lands on the touchscreen's interrupt and kills touch. */
    dev->port = char_attach(0, funlink_read, funlink_write, NULL,
                            funlink_control, funlink_port_config, dev);
    dev->log  = char_log_open(dev->port, "fun.link");

    if (dev->role == FUNLINK_ROLE_HOST)
        pclog("fun.link: COM1 0x%04X IRQ %d, hosting the bus on port %d\n",
              COM1_ADDR, COM1_IRQ, dev->host_port);
    else
        pclog("fun.link: COM1 0x%04X IRQ %d, %s bus at %s:%d\n", COM1_ADDR, COM1_IRQ,
              (dev->role == FUNLINK_ROLE_JOIN) ? "joining the" : "joining or hosting the",
              dev->host, dev->host_port);

    return dev;
}

// clang-format off
static const device_config_t funlink_config[] = {
    {
        .name           = "identity",
        .description    = "The fun.link adapter, on COM1 at 0x3F8 / IRQ 4, 115200 8N1.\n"
                          "Each cabinet is one instance; they all share one bus.",
        .type           = CONFIG_LABEL,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "role",
        .description    = "This cabinet",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = FUNLINK_ROLE_AUTO,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Joins if the bus exists, else hosts it", .value = FUNLINK_ROLE_AUTO },
            { .description = "Hosts the bus",                          .value = FUNLINK_ROLE_HOST },
            { .description = "Joins a bus",                            .value = FUNLINK_ROLE_JOIN },
            { .description = ""                                                                   }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "host",
        .description    = "Bus address",
        .type           = CONFIG_STRING,
        .default_string = "127.0.0.1",
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
        /* funworld's own telephone number on the fun.link advert, which is as
           good a way to pick a free port as any. */
        .default_int    = 7662,
        .file_filter    = NULL,
        .spinner        = {
            /* 32767, not 65535: device_config_spinner_t is int16_t and a wider
               maximum wraps negative.  The modem stops here for the same
               reason. */
            .min = 1,
            .max = 32767
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        /* Each cabinet had its own adapter and so its own number, and two
           stations answering to the same one cannot be told apart on the bus.
           Automatic follows who hosts, which is right for a pair; a third and
           fourth cabinet need this set by hand. */
        .name           = "station",
        .description    = "Station number",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = {
            .min = 0,
            .max = 255
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        /* For working out where a linked game stalls.  The cabinets talk to each
           other and not to us, so when something goes wrong between them the wire
           is the only place it shows. */
        .name           = "capture",
        .description    = "Write a trace of the bus to funlink-capture.txt",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "No",  .value = 0 },
            { .description = "Yes", .value = 1 },
            { .description = ""                }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "echo",
        .description    = "Cabinet hears its own transmissions",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "No",  .value = 0 },
            { .description = "Yes", .value = 1 },
            { .description = ""                }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t char_funlink_com_device = {
    .name          = "funworld fun.link",
    .internal_name = "funlink",
    .flags         = DEVICE_COM,
    .local         = 0,
    .init          = funlink_init,
    .close         = funlink_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = funlink_config
};
