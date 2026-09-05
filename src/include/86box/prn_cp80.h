/*
 * PeepeeBox   A fork of 86Box that emulates the funworld Photo Play / I.G.O.
 *             arcade kiosk hardware, including its protection token.
 *
 *             The cabinet's receipt printer, on a serial port.  See prn_cp80.c.
 *
 * Authors:    The HUEG PP team.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#ifndef EMU_PRN_CP80_H
#define EMU_PRN_CP80_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The two things there are to look at: what would be on the paper, and what the
   guest actually sent to get it there.  The second is the useful one until the
   command set is known, and it is the reason this device exists at all. */
#define PRN_CP80_PAPER 0
#define PRN_CP80_TRACE 1

/* Is the printer attached to a serial port at all?  It is not, if something
   else claimed the port first. */
extern int prn_cp80_present(void);

/* Plugged in or not, and it starts **unplugged**.  With the unit visible the
   DATAPRINT menu drops straight into the print dialog, so a cabinet that boots
   connected is one whose operator menu cannot be reached.

   Unplugging stops the ENQ keepalive and drops CTS, DSR and DCD, so the guest
   sees no cable rather than a device that has merely gone quiet, and the
   software goes back to offering "Connect the interfaces of the Dataprint". */
extern int  prn_cp80_connected(void);
extern void prn_cp80_set_connected(int on);

/* Where it is listening, as text.  Always COM2 -- MENU.EXE programs 0x2F8 by
   hand and holds no other port as an immediate -- unless PEEPEEBOX_PRN_PORT
   moved it for this run. */
extern void prn_cp80_where(char *out, size_t len);

/* Has the guest ever sent it a byte?  For deciding whether to put the window on
   screen without being asked. */
extern int prn_cp80_dirty(void);

/* Copy out whatever has been appended since *pos, advancing it, and return how
   many bytes that was.  *reset comes back non-zero when the buffer was emptied
   underneath the caller, which means throw away what you are holding and start
   again.  Safe to call from the UI thread while the emulator thread writes. */
extern size_t prn_cp80_take(int which, size_t *pos, int *reset,
                            char *out, size_t max);

/* Tear the paper off. */
extern void prn_cp80_clear(void);

/* The file the raw byte stream is going to, or NULL if nothing has been
   printed yet.  This is the ground truth for identifying the command set. */
extern const char *prn_cp80_raw_path(void);

extern const device_t prn_cp80_device;

#ifdef __cplusplus
}
#endif

#endif /*EMU_PRN_CP80_H*/
