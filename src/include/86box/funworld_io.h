/*
 * PeepeeBox   A fork of 86Box that emulates the funworld Photo Play / I.G.O.
 *             arcade kiosk hardware, including its protection token.
 *
 *             The funworld I/O card -- the 8255 the coin acceptor and the two
 *             service buttons hang off.  See funworld_io.c.
 *
 * Authors:    The HUEG PP team.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#ifndef EMU_FUNWORLD_IO_H
#define EMU_FUNWORLD_IO_H

#ifdef __cplusplus
extern "C" {
#endif

/* The lines that arrive at the card.  Six coins because the Coin Controls C120
   has six separate accept outputs, one per programmed coin, rather than
   encoding the value in a pulse count. */
#define FWIO_LINE_COIN1 0
#define FWIO_LINE_COIN2 1
#define FWIO_LINE_COIN3 2
#define FWIO_LINE_COIN4 3
#define FWIO_LINE_COIN5 4
#define FWIO_LINE_COIN6 5
#define FWIO_LINE_SETUP 6
#define FWIO_LINE_CALIB 7
#define FWIO_IN_LINES   8

/* Assert one line for as long as the real part would hold it -- 100 ms for a
   C120 coin, which is what the software debounces against.  Called from the UI;
   does nothing when the card is not fitted. */
extern void funworld_io_pulse(int line);

/* Whether the card is in the machine, so the UI can grey its buttons. */
extern int  funworld_io_present(void);

/* Diagnostic: answer the card-detection sweep at every candidate address, to
   find out which one the disk expects.  Enabled by PEEPEEBOX_IO_PROBE. */
extern void funworld_io_probe_init(void);

extern const device_t funworld_io_device;

#ifdef __cplusplus
}
#endif

#endif /*EMU_FUNWORLD_IO_H*/
