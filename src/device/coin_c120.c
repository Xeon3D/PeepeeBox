/*
 * PeepeeBox   A fork of 86Box that emulates the funworld Photo Play / I.G.O.
 *             arcade kiosk hardware, including its protection token.
 *
 *             The Coin Controls C120 validator, on COM2.
 *
 *             The C120 is a *parallel* part.  Its manual gives six accept
 *             outputs on a 10-way IDC, each an open-collector NPN pulled low
 *             for 100 ms +/- 20% on a good coin, and there is no protocol on
 *             any of them -- nothing in it can speak serial.
 *
 *             So a C120 landing on COM2 is not talking to the UART; it is wired
 *             to the only inputs a UART has.  The modem status register carries
 *             four of them -- CTS, DSR, RI and DCD -- and the host polls them,
 *             which is exactly how a machine reads level-signalling coin lines
 *             through a serial port it had spare.  Four inputs is also the
 *             number of coins this cabinet is set up for: 0.10, 0.50, 1 and 2
 *             EUR.  The 5 EUR row in the operator setup is a banknote and comes
 *             from a bill validator, which is a different device again.
 *
 *             Which line is which coin is not known, so the walk covers all
 *             four the same way it covers the I/O card's lines.
 *
 *             The 100 ms hold is the part that is not a guess: the manual is
 *             explicit that the host must see the line held and must not merely
 *             detect edges, so this asserts and releases on a timer rather than
 *             poking a flag.
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
#include <86box/timer.h>
#include <86box/serial.h>
#include <86box/plat.h>
#include <86box/funworld_io.h>

#define C120_PORT_COM2 1        /* zero-based: COM2 */
#define C120_HOLD_MS   100.0    /* what the manual specifies */

typedef struct c120_t {
    serial_t  *serial;
    pc_timer_t release[C120_LINES];
    uint8_t    held;
} c120_t;

static c120_t *c120_inst = NULL;

static void
c120_drive(c120_t *dev, int line, int on)
{
    if (dev->serial == NULL)
        return;

    /* Asserted is low at the connector, and a UART's status inputs are inverted
       on the way to the register, so "coin" is the enabled state here.  Which of
       the four carries which coin is what the walk is for. */
    switch (line) {
        case C120_LINE_CTS:
            serial_set_cts(dev->serial, on);
            break;
        case C120_LINE_DSR:
            serial_set_dsr(dev->serial, on);
            break;
        case C120_LINE_DCD:
            serial_set_dcd(dev->serial, on);
            break;
        case C120_LINE_RI:
            serial_set_ri(dev->serial, on);
            break;
        default:
            break;
    }
}

static void
c120_release(void *priv)
{
    c120_t   *dev  = c120_inst;
    const int line = (int) (intptr_t) priv;

    if (dev == NULL)
        return;

    c120_drive(dev, line, 0);
    dev->held &= (uint8_t) ~(1 << line);
    pclog("C120: line %d released\n", line);
}

void
coin_c120_pulse(int line)
{
    c120_t *dev = c120_inst;

    if ((dev == NULL) || (line < 0) || (line >= C120_LINES))
        return;

    c120_drive(dev, line, 1);
    dev->held |= (uint8_t) (1 << line);
    timer_on_auto(&dev->release[line], C120_HOLD_MS * 1000.0);
    pclog("C120: line %d (%s) asserted for %g ms\n", line,
          coin_c120_line_name(line), C120_HOLD_MS);
}

const char *
coin_c120_line_name(int line)
{
    static const char *names[C120_LINES] = { "CTS", "DSR", "DCD", "RI" };

    return ((line >= 0) && (line < C120_LINES)) ? names[line] : "?";
}

int
coin_c120_present(void)
{
    return (c120_inst != NULL) && (c120_inst->serial != NULL);
}

static void
c120_write(UNUSED(serial_t *serial), UNUSED(void *priv), UNUSED(uint8_t val))
{
    /* The host has nothing to say to a C120 over the wire; the only thing it can
       tell it is inhibit, and that is a level on the loom, not a byte. */
}

static void *
c120_init(UNUSED(const device_t *info))
{
    c120_t *dev = (c120_t *) calloc(1, sizeof(c120_t));

    if (dev == NULL)
        return NULL;

    dev->serial = serial_attach(C120_PORT_COM2, NULL, c120_write, dev);
    if (dev->serial == NULL) {
        pclog("C120: COM2 is not there; no validator attached\n");
        free(dev);
        return NULL;
    }

    /* Idle: no coin on any line. */
    for (int i = 0; i < C120_LINES; i++) {
        timer_add(&dev->release[i], c120_release, (void *) (intptr_t) i, 0);
        c120_drive(dev, i, 0);
    }

    c120_inst = dev;
    pclog("C120: Coin Controls C120 on COM2, four accept lines on CTS/DSR/DCD/RI\n");
    return dev;
}

static void
c120_close(void *priv)
{
    c120_t *dev = (c120_t *) priv;

    if (dev == NULL)
        return;

    free(dev);
    c120_inst = NULL;
}

const device_t coin_c120_device = {
    .name          = "Coin Controls C120 (COM2)",
    .internal_name = "coin_c120",
    .flags         = DEVICE_COM,
    .local         = 0,
    .init          = c120_init,
    .close         = c120_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
