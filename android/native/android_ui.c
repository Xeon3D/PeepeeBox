/*
 * PeepeeBox for Android - the funworld Photo Play cabinet (PeepeeBox) on Android.
 *
 *          Android: the core's ui_* calls.  The native app draws its own bars
 *          (app/), so the status-bar calls have nothing to update; a
 *          message box goes to the log, and the emulation speed is kept for
 *          the app to show (android_speed_percent, read over JNI).
 *
 * Authors: PeepeeBox contributors
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <android/log.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>
#include <86box/ui.h>

volatile int android_speed_percent = 0;

int
ui_msgbox(int flags, char *message)
{
    return ui_msgbox_header(flags, NULL, message);
}

int
ui_msgbox_header(int flags, char *header, char *message)
{
    const int prio = (flags & MBX_FATAL) ? ANDROID_LOG_FATAL : ((flags & MBX_ERROR) ? ANDROID_LOG_ERROR : ANDROID_LOG_WARN);
    __android_log_print(prio, "PeepeeBox", "%s%s%s", header ? (const char *) header : "", header ? ": " : "",
                        message ? (const char *) message : "");
    return 0; /* "OK" / the default answer */
}

void
ui_emu_status(int speed_percent)
{
    android_speed_percent = speed_percent;
}

void
ui_hard_reset_completed(void)
{
}

void
ui_init_monitor(UNUSED(int monitor_index))
{
}

void
ui_deinit_monitor(UNUSED(int monitor_index))
{
}

void
ui_sb_set_ready(UNUSED(int ready))
{
}

void
ui_sb_update_panes(void)
{
}

void
ui_sb_update_text(void)
{
}

void
ui_sb_update_tip(UNUSED(int arg))
{
}

void
ui_sb_update_icon(UNUSED(int tag), UNUSED(int active))
{
}

void
ui_sb_update_icon_write(UNUSED(int tag), UNUSED(int active))
{
}

void
ui_sb_update_icon_state(UNUSED(int tag), UNUSED(int state))
{
}

void
ui_sb_update_icon_wp(UNUSED(int tag), UNUSED(int state))
{
}

void
ui_sb_set_text(UNUSED(char *wstr))
{
}

void
ui_sb_bugui(UNUSED(char *str))
{
}

void
ui_sb_mt32lcd(UNUSED(char *str))
{
}

void
ui_update_force_interpreter(void)
{
}

/* The video code asks for a new window size when the guest changes mode; the
   surface follows the frame size by itself (android_main.cpp). */
void
plat_resize_request(UNUSED(int w), UNUSED(int h), UNUSED(int monitor_index))
{
}

void
plat_resize(UNUSED(int w), UNUSED(int h), UNUSED(int monitor_index))
{
}

void
plat_mouse_capture(UNUSED(int on))
{
}
