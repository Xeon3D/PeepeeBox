/*
 * PeepeeBox   A fork of 86Box that emulates the funworld Photo Play / I.G.O.
 *             arcade kiosk hardware, including its two protection tokens.
 *
 *             The fixed Photo Play machine profile.
 *
 *             86Box is a general-purpose PC emulator: the user picks a
 *             motherboard, a CPU, a video card, a sound card and a set of
 *             drives, and the config file records that choice.  PeepeeBox is
 *             not general-purpose.  It emulates one cabinet:
 *
 *                 Zida Tomato 4DPS (SiS 496), Intel iDX4 at 100 MHz, 16 MB
 *                 Cirrus Logic CL-GD5480, ESS ES1688 AudioDrive
 *                 3M MicroTouch TouchPen on COM3
 *                 the Photo Play protection dongle on LPT1
 *                 one IDE disk: HardDisk.img, next to the executable
 *
 *             so there is nothing to choose.  Every one of those is what the
 *             real cabinets shipped, and any deviation is a bug rather than a
 *             preference -- a different video card gets the wrong VESA modes, a
 *             different sound card gets no sound at all, and moving the
 *             touchscreen off COM3 makes touch input silently stop working
 *             while everything else still appears to run.  That last failure in
 *             particular cost real debugging time, which is a good argument for
 *             not letting it be configurable.
 *
 *             This is applied at the end of config_load() rather than by
 *             changing the load_*() defaults, because defaults only apply when
 *             a key is absent.  Stamping the profile after the whole file has
 *             been parsed means a stale, hand-edited or copied-in 86box.cfg
 *             cannot produce a machine that is not a Photo Play cabinet.
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
#include <86box/ini.h>
#include <86box/config.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/video.h>
#include <86box/sound.h>
#include <86box/midi.h>
#include <86box/snd_mpu401.h>
#include <86box/mouse.h>
#include <86box/keyboard.h>
#include <86box/gameport.h>
#include <86box/serial.h>
#include <86box/lpt.h>
#include <86box/char.h>
#include <86box/hdd.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/fdd.h>
#include <86box/photoplay.h>
#include "cpu.h"

/* The real images all use 63 sectors per track and 16 heads; their MBRs and
   FAT16 boot records were written under that geometry, and PTS-DOS still does
   CHS addressing, so it is not a free choice.  Cylinders are whatever the image
   size gives.  86Box's own hdd_image_calc_chs() arrives at 63/16 for every size
   in this range anyway, but it rounds the image down to whole megabytes first,
   which would leave the tail of a 3.0 GB image unaddressable. */
#define PP_SPT 63
#define PP_HPC 16

static void
pp_profile_log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    pclog_ex(fmt, ap);
    va_end(ap);
}

/* Pin the motherboard, CPU and RAM. */
static void
pp_apply_machine(void)
{
    machine = machine_get_machine_from_internal_name(PHOTOPLAY_MACHINE);
    if (machine < 0)
        fatal("PeepeeBox: the " PHOTOPLAY_MACHINE " machine is missing from this build\n");

    cpu_f = cpu_get_family(PHOTOPLAY_CPU_FAMILY);
    if (cpu_f == NULL)
        fatal("PeepeeBox: the " PHOTOPLAY_CPU_FAMILY " CPU family is missing from this build\n");

    /* Pick the 100 MHz part out of the family.  Matching on the speed rather
       than on a hardcoded index keeps this correct if the table gains entries. */
    cpu = 0;
    while (cpu_f->cpus[cpu].cpu_type && (cpu_f->cpus[cpu].rspeed != PHOTOPLAY_CPU_SPEED))
        cpu++;
    if (!cpu_f->cpus[cpu].cpu_type)
        fatal("PeepeeBox: no %d Hz part in the " PHOTOPLAY_CPU_FAMILY " family\n",
              PHOTOPLAY_CPU_SPEED);
    cpu_s = (CPU *) &cpu_f->cpus[cpu];

    /* The DX4 has an on-die FPU; the games use it. */
    fpu_type      = fpu_get_type(cpu_f, cpu, "internal");
    fpu_softfloat = 0;

    cpu_waitstates           = 0;
    cpu_use_dynarec          = 1;
    cpu_override             = 0;
    cpu_override_interpreter = 0;

    mem_size = PHOTOPLAY_MEM_SIZE;

    /* The cabinets have no battery-backed clock worth honouring and the games
       do not care what year it is, so follow the host clock. */
    time_sync = TIME_SYNC_ENABLED;
}

/* Pin the video and sound cards. */
static void
pp_apply_video_sound(void)
{
    gfxcard[0] = video_get_video_from_internal_name(PHOTOPLAY_GFXCARD);
    if (!gfxcard[0])
        fatal("PeepeeBox: the " PHOTOPLAY_GFXCARD " video card is missing from this build\n");
    for (int i = 1; i < GFXCARD_MAX; i++)
        gfxcard[i] = 0;

    sound_card_current[0] = sound_card_get_from_internal_name(PHOTOPLAY_SNDCARD);
    if (!sound_card_current[0])
        fatal("PeepeeBox: the " PHOTOPLAY_SNDCARD " sound card is missing from this build\n");
    for (int i = 1; i < SOUND_CARD_MAX; i++)
        sound_card_current[i] = 0;

    /* No MIDI anywhere in the cabinet. */
    midi_output_device_current = 0;
    midi_input_device_current  = 0;
    mpu401_standalone_enable   = 0;
}

/* Pin the input devices: a PS/2 keyboard the cabinet does not have but the BIOS
   expects, and the MicroTouch touchscreen, which is the only thing a player
   ever touches. */
static void
pp_apply_input(void)
{
    keyboard_type = KEYBOARD_TYPE_PS2;

    /* No mouse: the games drive the MicroTouch directly and a second pointing
       device only confuses the guest. */
    mouse_type  = 0;
    tablet_type = tablet_get_from_internal_name(PHOTOPLAY_TABLET);
    if (!tablet_type)
        fatal("PeepeeBox: the " PHOTOPLAY_TABLET " touchscreen is missing from this build\n");

    /* The touchscreen's serial port is a device config option rather than a
       global, so it has to be stamped into the ini the settings UI would have
       written.  COM3 is where the cabinet wiring puts it; anywhere else and
       touch input dies without an error message. */
    config_set_int(PHOTOPLAY_TABLET_NAME, "port", PHOTOPLAY_TABLET_PORT);

    for (int i = 0; i < GAMEPORT_MAX; i++)
        joystick_type[i] = 0;
}

/* Pin the ports: COM1-3 present because the touchscreen sits on COM3, and the
   protection dongle on LPT1. */
static void
pp_apply_ports(void)
{
    for (int i = 0; i < SERIAL_MAX; i++) {
        com_ports[i].enabled = (i < 3);
        com_ports[i].device  = 0;
    }

    lpt_ports[0].enabled = 1;
    lpt_ports[0].device  = char_get_from_internal_name(PHOTOPLAY_DONGLE, DEVICE_LPT);
    if (!lpt_ports[0].device)
        fatal("PeepeeBox: the " PHOTOPLAY_DONGLE " device is missing from this build\n");
    for (int i = 1; i < PARALLEL_MAX; i++) {
        lpt_ports[i].enabled = 0;
        lpt_ports[i].device  = 0;
    }
}

/* Always mount HardDisk.img from the emulator's own directory as the single
   IDE master, with the geometry implied by its size. */
static void
pp_apply_disk(void)
{
    char     fn[MAX_IMAGE_PATH_LEN];
    FILE    *fp;
    uint64_t bytes   = 0;
    uint32_t sectors;

    for (int i = 0; i < HDD_NUM; i++)
        memset(&hdd[i], 0, sizeof(hard_disk_t));

    path_append_filename(fn, exe_path, PHOTOPLAY_DISK_IMAGE);

    fp = plat_fopen64(fn, "rb");
    if (fp == NULL) {
        /* Deliberately do not fall through to 86Box's create-on-open path: it
           would silently produce a blank multi-gigabyte image and boot to a
           dead machine, which looks like a corrupt disk rather than a missing
           one.  Leave the disk disabled and say so. */
        pp_profile_log("PP: %s not found next to the executable -- no disk attached\n", fn);
        return;
    }
    if (!fseeko64(fp, 0, SEEK_END))
        bytes = (uint64_t) ftello64(fp);
    fclose(fp);

    sectors = (uint32_t) (bytes >> 9);
    if (sectors < (PP_SPT * PP_HPC)) {
        pp_profile_log("PP: %s is too small to be a disk image (%llu bytes)\n",
                       fn, (unsigned long long) bytes);
        return;
    }

    hdd[0].bus_type     = HDD_BUS_IDE;
    hdd[0].ide_channel  = 0; /* primary master */
    hdd[0].spt          = PP_SPT;
    hdd[0].hpc          = PP_HPC;
    hdd[0].tracks       = sectors / (PP_SPT * PP_HPC);
    hdd[0].wp           = 0;
    hdd[0].speed_preset = hdd_preset_get_from_internal_name("ramdisk");
    strcpy(hdd[0].fn, fn);

    pp_profile_log("PP: disk %s, %u/%u/%u\n", fn, hdd[0].tracks, hdd[0].hpc, hdd[0].spt);

    /* Name the window after what the image actually is, rather than after
       whatever the working directory happens to be called -- with -P . that
       came out as ".".  The image says so itself in \FOTO\SETTINGS\MAIN.SET. */
    char ident[64];
    if (photoplay_identify(fn, ident, sizeof(ident))) {
        strncpy(vm_name, ident, sizeof(vm_name) - 1);
        vm_name[sizeof(vm_name) - 1] = '\0';
        pp_profile_log("PP: image identified as %s\n", ident);
    }
}

/* What the disk image says it is, cached.  The dongle asks again on every hard
   reset, and the answer cannot change without restarting the emulator, so the
   FAT is walked once. */
static int  pp_ident_done = 0;
static int  pp_ident_ok   = 0;
static char pp_ident_banner[64];
static char pp_ident_terr[16];

int
photoplay_image_ident(char *banner_out, size_t bsz, char *terr_out, size_t tsz)
{
    if (!pp_ident_done) {
        char fn[MAX_IMAGE_PATH_LEN];
        char disp[64];

        pp_ident_done = 1;
        path_append_filename(fn, exe_path, PHOTOPLAY_DISK_IMAGE);
        pp_ident_ok   = photoplay_identify_ex(fn, disp, sizeof(disp),
                                              pp_ident_banner, sizeof(pp_ident_banner),
                                              pp_ident_terr, sizeof(pp_ident_terr));
    }

    if (banner_out != NULL)
        snprintf(banner_out, bsz, "%s", pp_ident_banner);
    if (terr_out != NULL)
        snprintf(terr_out, tsz, "%s", pp_ident_terr);

    return pp_ident_ok && (pp_ident_banner[0] != 0);
}

/* The optional CD-ROM drive.

   The cabinets shipped without one -- everything ran from the hard disk -- but
   installation and service media exist, so it is worth being able to attach a
   drive.  What it is not worth is making it configurable: there is exactly one
   sensible answer, a generic 52x ATAPI drive as secondary master, which leaves
   the primary channel to HardDisk.img and is fast enough that nothing waits on
   it.  So the only choice is whether it is there at all. */
int
photoplay_cdrom_enabled(void)
{
    return !!config_get_int(PHOTOPLAY_SECTION, "cdrom", 0);
}

void
photoplay_set_cdrom_enabled(int enabled)
{
    config_set_int(PHOTOPLAY_SECTION, "cdrom", !!enabled);
}

/* The optional 3.5" floppy drive.

   Like the CD-ROM, the cabinets did not have one and the software never asks
   for one, but service and installation media exist on 1.44M disks.  The drive
   is a 3.5" 1.44M as drive A: -- the only kind worth having here -- so again
   the only choice is whether it is attached. */
int
photoplay_fdd_enabled(void)
{
    return !!config_get_int(PHOTOPLAY_SECTION, "floppy", 0);
}

void
photoplay_set_fdd_enabled(int enabled)
{
    config_set_int(PHOTOPLAY_SECTION, "floppy", !!enabled);
}

static void
pp_apply_floppy(void)
{
    const int enabled = photoplay_fdd_enabled();

    for (int i = 0; i < FDD_NUM; i++) {
        fdd_set_type(i, 0);
        fdd_set_turbo(i, 0);
        fdd_set_check_bpb(i, 1);
    }

    if (!enabled)
        return;

    fdd_set_type(0, fdd_get_from_internal_name(PHOTOPLAY_FDD_TYPE));
    pp_profile_log("PP: floppy drive A: attached, 3.5\" 1.44M\n");
}

static void
pp_apply_cdrom(void)
{
    const int enabled = photoplay_cdrom_enabled();

    for (int i = 1; i < CDROM_NUM; i++)
        cdrom[i].bus_type = CDROM_BUS_DISABLED;

    if (!enabled) {
        cdrom[0].bus_type = CDROM_BUS_DISABLED;
        return;
    }

    cdrom[0].bus_type    = CDROM_BUS_ATAPI;
    cdrom[0].ide_channel = PHOTOPLAY_CDROM_CHAN;
    cdrom[0].type        = cdrom_get_from_internal_name(PHOTOPLAY_CDROM_TYPE);
    cdrom[0].speed       = PHOTOPLAY_CDROM_SPEED;
    cdrom[0].cur_speed   = PHOTOPLAY_CDROM_SPEED;
    cdrom[0].sound_on    = 1;

    pp_profile_log("PP: CD-ROM attached, generic %dx ATAPI on secondary master\n",
                   PHOTOPLAY_CDROM_SPEED);
}

void
photoplay_apply_profile(void)
{
    pp_apply_machine();
    pp_apply_video_sound();
    pp_apply_input();
    pp_apply_ports();
    pp_apply_disk();
    pp_apply_cdrom();
    pp_apply_floppy();

    pp_profile_log("PP: Photo Play profile applied (%s, %s @ %d MHz, %d MB)\n",
                   PHOTOPLAY_MACHINE, PHOTOPLAY_CPU_FAMILY,
                   PHOTOPLAY_CPU_SPEED / 1000000, PHOTOPLAY_MEM_SIZE / 1024);
}
