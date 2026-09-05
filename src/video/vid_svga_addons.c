/*
 * PeepeeBox   A fork of 86Box that emulates the funworld Photo Play / I.G.O.
 *             arcade kiosk hardware, including its protection token.
 *
 *             Inert 8514/A and XGA hooks for the SVGA core.
 *
 *             86Box's SVGA core can host an 8514/A or an XGA alongside the VGA,
 *             and it reaches them from roughly sixty call sites threaded through
 *             its memory, timing and rendering paths.  Every one of those sites
 *             is gated on ibm8514_active or xga_active, and the two entry points
 *             that are called unguarded -- xga_read_test() and xga_write_test()
 *             on the Cirrus memory path -- test xga_active as their first act.
 *
 *             PeepeeBox has exactly one video card, the Cirrus Logic CL-GD5480,
 *             which is neither of those things and cannot host either of them.
 *             Both flags are therefore permanently zero and none of this code
 *             can run.
 *
 *             So the four card implementations (vid_8514a.c, vid_xga.c, and the
 *             ATi mach8 and EEPROM support they drag in behind them) are gone,
 *             and what remains is this: the flags, still zero, and no-op bodies
 *             for the entry points the SVGA core links against.  Deleting the
 *             call sites instead would have meant sixty edits scattered through
 *             the hottest code in the emulator, to remove branches that never
 *             taken -- a real risk of breaking video output in exchange for
 *             nothing.
 *
 * Authors:    The HUEG PP team.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/plat_unused.h>

/* Never set: nothing in this build can add an 8514/A or an XGA. */
int ibm8514_active = 0;
int xga_active     = 0;

void
ibm8514_set_poll(UNUSED(svga_t *svga))
{
    /* unreachable: every call site tests ibm8514_active first */
}

void
ibm8514_recalctimings(UNUSED(svga_t *svga))
{
    /* unreachable: every call site tests ibm8514_active first */
}

void
xga_set_poll(UNUSED(svga_t *svga))
{
    /* unreachable: every call site tests xga_active first */
}

void
xga_recalctimings(UNUSED(svga_t *svga))
{
    /* unreachable: every call site tests xga_active first */
}

/* These two are called unguarded from the Cirrus memory path.  Upstream's
   versions open with `if (xga_active && xga)`, so with no XGA present they read
   back nothing and write nothing -- which is what these do. */
uint8_t
xga_read_test(UNUSED(uint32_t addr), UNUSED(void *priv))
{
    return 0x00;
}

void
xga_write_test(UNUSED(uint32_t addr), UNUSED(uint8_t val), UNUSED(void *priv))
{
}
