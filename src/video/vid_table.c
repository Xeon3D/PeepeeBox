/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Define all known video cards.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2017-2020 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/vid_svga.h>

#include <86box/vid_cga.h>
#include <86box/vid_ega.h>
#include <86box/vid_colorplus.h>
#include <86box/vid_mda.h>
#include <86box/vid_xga_device.h>

typedef struct video_card_t {
    const device_t *device;
    int             flags;
} VIDEO_CARD;

typedef struct video_card_migrate_t {
    const device_t *device;
    const char     *old_internal_name;
} video_card_migrate_t;

static video_timings_t timing_default = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

static int was_reset = 0;

static const VIDEO_CARD
video_cards[] = {
  // clang-format off
    { .device = &device_none,                                   .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &device_internal,                               .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = &gd5480_pci_device,                             .flags = VIDEO_FLAG_TYPE_NONE      },
    { .device = NULL,                                           .flags = VIDEO_FLAG_TYPE_NONE      }
  // clang-format on
};

static const video_card_migrate_t
video_cards_migrate[] = {
  // clang-format off
    /* PeepeeBox: nothing to migrate -- there is one video card. */
    { .device = NULL,                                           .old_internal_name = NULL                             }
  // clang-format on
};

#ifdef ENABLE_VID_TABLE_LOG
int vid_table_do_log = ENABLE_VID_TABLE_LOG;

static void
vid_table_log(const char *fmt, ...)
{
    va_list ap;

    if (vid_table_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define vid_table_log(fmt, ...)
#endif

static pc_timer_t framerate_timer;

void* lightpen_priv = NULL;

void (*lightpen_hsync_callback)(void*) = NULL;
void (*lightpen_vsync_callback)(void*) = NULL;
void (*lightpen_check_trigger_strobe)(void* priv, int x_offset, int y, int x_offset_from_hsync, int firstline, double hpix_clock, int monitor_used) = NULL;

void
video_update_framerates(void* priv)
{
    (void)priv;
    int i = 0;

    for (i = 0; i < GFXCARD_MAX; i++) {
        monitors[i].mon_actualrenderedframes = monitors[i].mon_renderedframes;
        monitors[i].mon_renderedframes = 0;
    }

    timer_on_auto(&framerate_timer, 1000 * 1000);
}

void
video_reset_close(void)
{
    for (int i = 1; i < MONITORS_NUM; i++)
        video_monitor_close(i);

    monitor_index_global = 0;
    video_inform(VIDEO_FLAG_TYPE_NONE, &timing_default);
    was_reset = 0;
}

void
video_lightpen_set_callbacks(void* priv, void (*lightpen_hsync)(void*), void (*lightpen_vsync)(void*), void (*lightpen_trigger_strobe)(void* priv, int x, int y, int x_offset_from_hsync, int firstline, double hpix_clock, int monitor_used))
{
    lightpen_priv = priv;
    lightpen_hsync_callback = lightpen_hsync;
    lightpen_vsync_callback = lightpen_vsync;
    lightpen_check_trigger_strobe = lightpen_trigger_strobe;
}

void
video_lightpen_hsync(void)
{
    if (lightpen_hsync_callback)
        lightpen_hsync_callback(lightpen_priv);
}

void
video_lightpen_vsync(void)
{
    if (lightpen_vsync_callback)
        lightpen_vsync_callback(lightpen_priv);
}

void
video_lightpen_check_trigger_strobe(int x_offset, int y, int x_offset_from_hsync, int firstline, double pix_clock, int monitor_used)
{
    if (lightpen_check_trigger_strobe)
        lightpen_check_trigger_strobe(lightpen_priv, x_offset, y, x_offset_from_hsync, firstline, pix_clock, monitor_used);
}

static void
video_prepare(void)
{
    /* Reset (deallocate) the video font arrays. */
    if (fontdatksc5601) {
        free(fontdatksc5601);
        fontdatksc5601 = NULL;
    }

    /* Reset the blend. */
    herc_blend = 0;

    for (int i = 0; i < MONITORS_NUM; i++) {
        /* Reset the CGA palette. */
#if 0
        if (monitors[i].mon_cga_palette)
            *monitors[i].mon_cga_palette = 0;
#endif
        cgapal_rebuild_monitor(i);

        /* Do an inform on the default values, so that that there's some sane values initialized
           even if the device init function does not do an inform of its own. */
        video_inform_monitor(VIDEO_FLAG_TYPE_SPECIAL, &timing_default, i);

        monitors[i].mon_interlace = 0;
        monitors[i].mon_composite = 0;
    }
}

void
video_pre_reset(int card)
{
    if ((card == VID_NONE) || (card == VID_INTERNAL) || machine_has_flags(machine, MACHINE_VIDEO_ONLY))
        video_prepare();
}

void
video_reset(int card)
{
    /* This is needed to avoid duplicate resets. */
    if ((video_get_type() != VIDEO_FLAG_TYPE_NONE) && was_reset)
        return;

    vid_table_log("VIDEO: reset (gfxcard[0]=%d, internal=%d)\n",
                  card, machine_has_flags(machine, MACHINE_VIDEO) ? 1 : 0);

    monitor_index_global = 0;
    video_load_font(FONT_IBM_MDA_437_PATH, FONT_FORMAT_MDA, LOAD_FONT_NO_OFFSET);

    for (uint8_t i = 1; i < GFXCARD_MAX; i ++) {
        if ((card != VID_NONE) && !machine_has_flags(machine, MACHINE_VIDEO_ONLY) &&
            (gfxcard[i] > VID_INTERNAL) && device_is_valid(video_card_getdevice(gfxcard[i]), machine)) {
            video_monitor_init(i);
            monitor_index_global = 1;
            device_add_inst(video_cards[gfxcard[i]].device, i + 1);
            monitor_index_global = 0;
        }
    }

    /* Do not initialize internal cards here. */
    if ((card > VID_INTERNAL) && !machine_has_flags(machine, MACHINE_VIDEO_ONLY)) {
        vid_table_log("VIDEO: initializing '%s'\n", video_cards[card].device->name);

        video_prepare();

        /* Initialize the video card. */
        device_add_inst(video_cards[card].device, 1);
    }

    timer_add(&framerate_timer, video_update_framerates, NULL, 1);
    was_reset = 1;
}

void
video_post_reset(void)
{
    /* Reset the graphics card (or do nothing if it was already done
       by the machine's init function). */
    video_reset(gfxcard[0]);

    int ibm8514_has_vga = 0;
    if (gfxcard[0] == VID_INTERNAL)
        ibm8514_has_vga = (video_get_type_monitor(0) == VIDEO_FLAG_TYPE_8514);
    else if (gfxcard[0] != VID_NONE)
        ibm8514_has_vga = (video_card_get_flags(gfxcard[0]) == VIDEO_FLAG_TYPE_8514);
    else
        ibm8514_has_vga = 0;

    if (ibm8514_has_vga)
        ibm8514_active = 1;

    /* PeepeeBox: upstream could add a standalone 8514/A, XGA or PS/55 DA2 here.
       None of them exists in this build. */
}

void
video_voodoo_init(void)
{
    /* PeepeeBox: no Voodoo in this build. */
}

int
video_card_available(int card)
{
    if (video_cards[card].device)
        return (device_available(video_cards[card].device));

    return 1;
}

int
video_card_get_flags(int card)
{
    return video_cards[card].flags;
}

const device_t *
video_card_getdevice(int card)
{
    return (video_cards[card].device);
}

int
video_card_has_config(int card)
{
    if (video_cards[card].device == NULL)
        return 0;

    return (device_has_config(video_cards[card].device) ? 1 : 0);
}

const char *
video_get_internal_name(int card)
{
    return device_get_internal_name(video_cards[card].device);
}

int
video_get_video_from_internal_name(char *s)
{
    int c = 0;

    while (video_cards[c].device != NULL) {
        if (!strcmp(video_cards[c].device->internal_name, s))
            return c;
        c++;
    }

    return 0;
}

const device_t *
video_get_video_from_old_internal_name(char *s)
{
    int c = 0;

    while (video_cards_migrate[c].device != NULL) {
        if (!strcmp(video_cards_migrate[c].old_internal_name, s))
            return video_cards_migrate[c].device;
        c++;
    }

    return NULL;
}

int
video_is_mda(void)
{
    return (video_get_type() == VIDEO_FLAG_TYPE_MDA);
}

int
video_is_cga(void)
{
    return (video_get_type() == VIDEO_FLAG_TYPE_CGA);
}

static unsigned
video_default_cga_wait_states(uint32_t address, uint64_t cpu_cycle)
{
    /* Existing Marty-derived IBM CGA READY-slot model.  This fallback is
     * limited to ISA CGA timing descriptors; integrated VIDEO_BUS devices
     * such as the PC1512 must publish their own model. */
    static const uint8_t waits[16] = {
        5, 5, 4, 4, 4, 3, 8, 8,
        8, 7, 7, 7, 6, 6, 6, 5
    };

    if ((address < 0xb8000u) || (address > 0xbffffu))
        return 0;

    return waits[(unsigned)((cpu_cycle * 3u + 1u) & 0x0fu)];
}

unsigned
video_get_wait_states(uint32_t address, int write, unsigned size,
                      uint64_t cpu_cycle)
{
    const video_timings_t *timings = monitors[0].mon_vid_timings;

    if (timings && timings->wait_states)
        return timings->wait_states(address, write, size, cpu_cycle,
                                    timings->wait_states_priv);

    /* Preserve the previous exact-808x behavior for ordinary ISA CGA while
     * avoiding the IBM table for machine-integrated video buses. */
    if (timings && (timings->type == VIDEO_ISA) &&
        (video_get_type_monitor(0) == VIDEO_FLAG_TYPE_CGA))
        return video_default_cga_wait_states(address, cpu_cycle);

    return 0;
}
