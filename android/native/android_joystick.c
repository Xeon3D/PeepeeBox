/*
 * PeepeeBox for Android - the funworld Photo Play cabinet (PeepeeBox) on Android.
 *
 *          Android: no host joystick.  A cabinet has none, and the desktop
 *          builds' SDL backend (sdl_joystick.c) is not built for Android;
 *          these are its entry points with nothing behind them.
 *
 * Authors: PeepeeBox contributors
 */
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/plat_unused.h>

int                   joysticks_present = 0;
joystick_state_t      joystick_state[GAMEPORT_MAX][MAX_JOYSTICKS];
plat_joystick_state_t plat_joystick_state[MAX_PLAT_JOYSTICKS];

void
joystick_init(void)
{
    joysticks_present = 0;
    memset(plat_joystick_state, 0, sizeof(plat_joystick_state));
}

void
joystick_close(void)
{
}

void
joystick_process(UNUSED(uint8_t gp))
{
}

void
win_joystick_handle(UNUSED(void *raw))
{
}
