/*
 * PeepeeBox   A fork of 86Box that emulates the funworld Photo Play / I.G.O.
 *             arcade kiosk hardware, including its two protection tokens.
 *
 *             The fixed Photo Play machine profile.
 *
 *             86Box is a general-purpose PC emulator: the user picks a
 *             motherboard, a CPU, a video card, a sound card and a set of
 *             drives, and the config file records that choice.  PeepeeBox is
 *             not general-purpose.  It emulates two cabinets, and the disk
 *             image in the folder says which -- nothing is chosen by hand.
 *
 *             Photo Play / I.G.O.:
 *                 Zida Tomato 4DPS (SiS 496), Intel iDX4 at 100 MHz, 16 MB
 *                 3M MicroTouch TouchPen on COM3, IRQ 4
 *                 the Photo Play protection dongle on LPT1
 *
 *             Funny's Interactive Playworld:
 *                 PC Partner MB540N (i430TX), Pentium MMX at 200 MHz, 64 MB
 *                 Elo SmartSet on COM3, IRQ 3
 *                 the Funny token on LPT1
 *
 *             Both share the rest: Cirrus Logic CL-GD5480, ESS ES1688
 *             AudioDrive, and one IDE disk -- HardDisk.img, next to the
 *             executable.
 *
 *             Every one of those is what the real cabinets shipped, and any
 *             deviation is a bug rather than a preference -- a different video
 *             card gets the wrong VESA modes, a different sound card gets no
 *             sound at all, and moving the touchscreen off COM3 makes touch
 *             input silently stop working while everything else still appears
 *             to run.  That last failure in particular cost real debugging
 *             time, which is a good argument for not letting it be
 *             configurable.
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
#include <86box/ui.h>
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

/* Is this Funny's Interactive Playworld rather than a Photo Play release?  Four things
   hang off the answer -- the token, COM3's IRQ, the default touchscreen and the board
   itself -- so it is worth asking in one place.

   The image is what normally decides, and the identification behind that is cached, so
   asking costs nothing.  A forced answer in the ini wins over it: identification needs
   an image that is present and recognised, and when it is not, everything silently
   falls back to Photo Play's hardware, which a Funny disk will not run on. */
int
photoplay_is_funny(void)
{
    const char *forced = photoplay_product();
    char        banner[64] = { 0 };

    if (!strcmp(forced, PHOTOPLAY_PRODUCT_FUNNY))
        return 1;
    if (!strcmp(forced, PHOTOPLAY_PRODUCT_PP))
        return 0;

    photoplay_image_ident(banner, sizeof(banner), NULL, 0);
    return !strcmp(banner, PHOTOPLAY_FUNNY_BANNER);
}

/* Pin the motherboard, CPU and RAM.

   Two cabinets, two sets of hardware.  Photo Play is a 486 on a Zida Tomato 4DPS;
   Funny's Interactive Playworld is a Pentium MMX on a PC Partner MB540N with four
   times the RAM.  The image says which, so neither one has to be chosen by hand. */
static void
pp_apply_machine(void)
{
    const int   funny       = photoplay_is_funny();
    const char *machine_nm  = funny ? PHOTOPLAY_FUNNY_MACHINE    : PHOTOPLAY_MACHINE;
    const char *cpu_family  = funny ? PHOTOPLAY_FUNNY_CPU_FAMILY : PHOTOPLAY_CPU_FAMILY;
    const int   cpu_speed   = funny ? PHOTOPLAY_FUNNY_CPU_SPEED  : PHOTOPLAY_CPU_SPEED;
    const int   memory_size = funny ? PHOTOPLAY_FUNNY_MEM_SIZE   : PHOTOPLAY_MEM_SIZE;

    machine = machine_get_machine_from_internal_name((char *) machine_nm);
    if (machine < 0)
        fatal("PeepeeBox: the %s machine is missing from this build\n", machine_nm);

    cpu_f = cpu_get_family(cpu_family);
    if (cpu_f == NULL)
        fatal("PeepeeBox: the %s CPU family is missing from this build\n", cpu_family);

    /* Pick the wanted part out of the family.  Matching on the speed rather
       than on a hardcoded index keeps this correct if the table gains entries. */
    cpu = 0;
    while (cpu_f->cpus[cpu].cpu_type && (cpu_f->cpus[cpu].rspeed != cpu_speed))
        cpu++;
    if (!cpu_f->cpus[cpu].cpu_type)
        fatal("PeepeeBox: no %d Hz part in the %s family\n", cpu_speed, cpu_family);
    cpu_s = (CPU *) &cpu_f->cpus[cpu];

    /* Both parts have an on-die FPU; the games use it. */
    fpu_type      = fpu_get_type(cpu_f, cpu, "internal");
    fpu_softfloat = 0;

    cpu_waitstates           = 0;
    cpu_use_dynarec          = 1;
    cpu_override             = 0;
    cpu_override_interpreter = 0;

    mem_size = memory_size;

    pp_profile_log("PP: board %s, %s at %d Hz, %d KB\n", machine_nm, cpu_family,
                   cpu_speed, memory_size);

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

    /* No mouse: the games drive the touchscreen directly and a second pointing
       device only confuses the guest. */
    mouse_type  = 0;
    tablet_type = tablet_get_from_internal_name((char *) photoplay_touchscreen());
    if (!tablet_type)
        fatal("PeepeeBox: the %s touchscreen is missing from this build\n",
              photoplay_touchscreen());

    /* The serial port is a device config option rather than a global, so the
       cabinet's COM3 has to be stamped into the ini that the settings UI would
       otherwise have written -- but only when nothing has chosen one yet.  Doing
       it unconditionally would mean the Touchscreen dialog could never move the
       port, and the move would look like it had simply been ignored. */
    {
        const device_t *dev = tablet_get_device(tablet_type);

        if ((dev != NULL) && (config_get_int((char *) dev->name, "port", -1) < 0))
            config_set_int((char *) dev->name, "port", PHOTOPLAY_TABLET_PORT);
    }

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

    /* Which token goes on LPT1 is the one thing about this machine the image gets to
       decide, because the two cabinets this build runs carry different ones and nothing
       else tells them apart.  Photo Play / I.G.O. is the default; an image that
       identifies as Funny's Interactive Playworld gets its own part instead.  See
       dongle_funny.c for what that is and why it is not the same device. */
    const char *dongle_name = photoplay_is_funny() ? PHOTOPLAY_FUNNY_DONGLE : PHOTOPLAY_DONGLE;

    lpt_ports[0].enabled = 1;
    lpt_ports[0].device  = char_get_from_internal_name(dongle_name, DEVICE_LPT);
    if (!lpt_ports[0].device)
        fatal("PeepeeBox: the %s device is missing from this build\n", dongle_name);
    pp_profile_log("PP: LPT1 token: %s\n", dongle_name);
    for (int i = 1; i < PARALLEL_MAX; i++) {
        lpt_ports[i].enabled = 0;
        lpt_ports[i].device  = 0;
    }
}

/* ------------------------------------------------------------------------------------
 * Photo Play 2.0: Microcosm CopyControl.
 *
 * 2.0 carries no dongle.  Its games are wrapped in CopyControl v1.66, whose key is not in
 * any file -- it is the *physical layout* of the disk.  PP2000.CCC's last cluster has to
 * end with a run of a particular byte written past the end of the file, and both
 * CCONTROL.SYS and PP2000.CCC have to start at the exact clusters CCMOVE recorded.
 * Copying an image file by file destroys all of that, which is why every 2.0 image found
 * so far stops with "Run CCMOVE to create a working copy" the moment a game is started.
 * docs/research/24 has the whole mechanism, and the licence cipher.
 *
 * Repairing an image is `tools/ppfix`'s job, not the emulator's: it rewrites directory
 * entries, FAT entries and slack, which is not something to do quietly behind the user's
 * back at every boot.  ppfix stamps LBA 1 -- a sector in the MBR track that no filesystem
 * uses -- when it is done.  All that is left here is to notice a 2.0 image that has not
 * been through it and say so, rather than letting the games fail mysteriously.
 */

#define PP_CC_MARKER "PPBOXCC1"

typedef struct {
    FILE    *f;
    uint32_t part;
    uint32_t bps;
    uint32_t spc;
    uint32_t fat_lba;
    uint32_t root_lba;
    uint32_t root_secs;
    uint32_t data_lba;
    uint32_t nfat;
    uint32_t spf;
    uint8_t *fat;
} pp_fat_t;

static uint16_t
pp_rd16(const uint8_t *p)
{
    return (uint16_t) (p[0] | (p[1] << 8));
}

static uint32_t
pp_rd32(const uint8_t *p)
{
    return (uint32_t) (p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static int
pp_sec_read(pp_fat_t *v, uint32_t lba, uint32_t count, uint8_t *buf)
{
    if (fseeko64(v->f, (uint64_t) lba * v->bps, SEEK_SET) != 0)
        return 0;
    return fread(buf, v->bps, count, v->f) == count;
}

static uint16_t
pp_fat_get(const pp_fat_t *v, uint16_t cl)
{
    return pp_rd16(&v->fat[cl * 2]);
}

static uint32_t
pp_clus_lba(const pp_fat_t *v, uint16_t cl)
{
    return v->data_lba + ((uint32_t) (cl - 2) * v->spc);
}

static int
pp_fat_open(pp_fat_t *v, const char *fn)
{
    uint8_t sec[512];

    memset(v, 0, sizeof(pp_fat_t));
    v->bps = 512;
    v->f   = plat_fopen64((char *) fn, "rb");
    if (v->f == NULL)
        return 0;
    if (!pp_sec_read(v, 0, 1, sec) || (pp_rd16(&sec[510]) != 0xAA55))
        return 0;
    v->part = pp_rd32(&sec[0x1BE + 8]);
    if ((v->part == 0) || (v->part > (1U << 24)))
        return 0;
    if (!pp_sec_read(v, v->part, 1, sec))
        return 0;
    v->bps  = pp_rd16(&sec[0x0B]);
    v->spc  = sec[0x0D];
    v->nfat = sec[0x10];
    v->spf  = pp_rd16(&sec[0x16]);
    if ((v->bps != 512) || (v->spc == 0) || (v->nfat == 0) || (v->spf == 0))
        return 0;
    v->fat_lba   = v->part + pp_rd16(&sec[0x0E]);
    v->root_lba  = v->fat_lba + (v->nfat * v->spf);
    v->root_secs = ((uint32_t) pp_rd16(&sec[0x11]) * 32) / v->bps;
    v->data_lba  = v->root_lba + v->root_secs;
    v->fat       = (uint8_t *) malloc(v->spf * v->bps);
    if (v->fat == NULL)
        return 0;
    return pp_sec_read(v, v->fat_lba, v->spf, v->fat);
}

static void
pp_fat_close(pp_fat_t *v)
{
    if (v->fat != NULL)
        free(v->fat);
    if (v->f != NULL)
        fclose(v->f);
    v->fat = NULL;
    v->f   = NULL;
}

/* Find an 11-byte "NAME    EXT" entry.  dir_cl 0 means the root directory. */
static int
pp_dir_find(pp_fat_t *v, uint16_t dir_cl, const char *name11, uint16_t *cl_out,
            uint32_t *size_out)
{
    uint8_t *buf = (uint8_t *) malloc(v->spc * v->bps);
    int      hit = 0;

    if (buf == NULL)
        return 0;
    for (int guard = 0; (guard < 64) && !hit; guard++) {
        const uint32_t lba  = dir_cl ? pp_clus_lba(v, dir_cl) : v->root_lba;
        const uint32_t secs = dir_cl ? v->spc : v->root_secs;

        if (!pp_sec_read(v, lba, secs, buf))
            break;
        for (uint32_t i = 0; i < ((secs * v->bps) / 32); i++) {
            const uint8_t *e = &buf[i * 32];

            if ((e[0] == 0x00) || (e[0] == 0xE5))
                continue;
            if (!memcmp(e, name11, 11)) {
                if (cl_out != NULL)
                    *cl_out = pp_rd16(&e[0x1A]);
                if (size_out != NULL)
                    *size_out = pp_rd32(&e[0x1C]);
                hit = 1;
                break;
            }
        }
        if (!dir_cl)
            break;
        dir_cl = pp_fat_get(v, dir_cl);
        if ((dir_cl < 2) || (dir_cl >= 0xFFF0))
            break;
    }
    free(buf);
    return hit;
}

/* The NSB number -- the build the cabinet shipped as, and the thing the folders these
   images arrive in are named after.  Every dongle-era release keeps it in MENU\NSB.NR;
   Photo Play 2.0 has no MENU directory and keeps it in MAIN\KEY.DAT instead.  It is plain
   text and not always digits: "A3735", "IGO7 MK002", "C519A SP1", "8778".  Some images
   carry the file but leave it zeroed, which counts as not having one. */
static int
pp_read_small(pp_fat_t *v, uint16_t dir_cl, const char *name11, char *out, size_t sz)
{
    uint16_t cl   = 0;
    uint32_t size = 0;
    uint8_t *buf;
    size_t   n;

    if (!pp_dir_find(v, dir_cl, name11, &cl, &size) || (cl < 2) || (size == 0))
        return 0;

    buf = (uint8_t *) malloc(v->spc * v->bps);
    if (buf == NULL)
        return 0;
    if (!pp_sec_read(v, pp_clus_lba(v, cl), v->spc, buf)) {
        free(buf);
        return 0;
    }

    n = (size < (sz - 1)) ? size : (sz - 1);
    memcpy(out, buf, n);
    out[n] = '\0';
    free(buf);

    /* Trim the line ending and any padding, and reject a zeroed file. */
    for (size_t i = 0; i < n; i++)
        if ((out[i] == '\r') || (out[i] == '\n') || (out[i] == '\032')) {
            out[i] = '\0';
            break;
        }
    for (size_t i = strlen(out); i && ((out[i - 1] == ' ') || (out[i - 1] == '\0')); i--)
        out[i - 1] = '\0';
    return out[0] != '\0';
}

/* What to call this image in the window title: the release it says it is, plus its NSB
   number.  Photo Play 2.0 does not carry a MAIN.SET to name itself, so it is recognised
   by its CopyControl directory instead. */
static void
pp_image_label(const char *fn, char *out, size_t sz)
{
    pp_fat_t v;
    char     nsb[32] = "";
    char     name[64] = "";
    uint16_t dir      = 0;
    uint16_t sub      = 0;
    int      is20     = 0;

    out[0] = '\0';
    if (!pp_fat_open(&v, fn)) {
        pp_fat_close(&v);
        return;
    }

    if (pp_dir_find(&v, 0, "MENU       ", &dir, NULL))
        pp_read_small(&v, dir, "NSB     NR ", nsb, sizeof(nsb));
    if ((nsb[0] == '\0') && pp_dir_find(&v, 0, "MAIN       ", &dir, NULL))
        pp_read_small(&v, dir, "KEY     DAT", nsb, sizeof(nsb));

    if (pp_dir_find(&v, 0, "EXE        ", &dir, NULL) &&
        pp_dir_find(&v, dir, "PP2000  081", &sub, NULL) &&
        pp_dir_find(&v, sub, "PP2000  CCC", NULL, NULL))
        is20 = 1;

    pp_fat_close(&v);

    if (is20)
        snprintf(name, sizeof(name), "Photo Play 2.0");
    else if (!photoplay_identify(fn, name, sizeof(name)))
        name[0] = '\0';

    if (name[0] && nsb[0])
        snprintf(out, sz, "%s - NSB: %s", name, nsb);
    else if (name[0])
        snprintf(out, sz, "%s", name);
    else if (nsb[0])
        snprintf(out, sz, "NSB: %s", nsb);
}

/* Warn about a Photo Play 2.0 image that ppfix has not repaired.  Read-only: this looks
   and tells, it does not touch the image. */
static void
pp_check_copycontrol(const char *fn)
{
    static int told = 0;
    pp_fat_t   v;
    uint16_t dir_exe = 0;
    uint16_t dir_cc  = 0;
    uint8_t  sec[512];

    if (!pp_fat_open(&v, fn)) {
        pp_fat_close(&v);
        return;
    }
    if (!pp_dir_find(&v, 0, "EXE        ", &dir_exe, NULL) ||
        !pp_dir_find(&v, dir_exe, "PP2000  081", &dir_cc, NULL) ||
        !pp_dir_find(&v, dir_cc, "CCONTROLSYS", NULL, NULL) ||
        !pp_dir_find(&v, dir_cc, "PP2000  CCC", NULL, NULL)) {
        pp_fat_close(&v);
        return; /* not a 2.0 image */
    }

    if (pp_sec_read(&v, 1, 1, sec) && !memcmp(sec, PP_CC_MARKER, 8)) {
        pp_profile_log("PP: Photo Play 2.0 image, CopyControl layout repaired"
                       " (CCONTROL.SYS at %u, PP2000.CCC at %u, slack %02X)\n",
                       pp_rd16(&sec[8]), pp_rd16(&sec[10]), sec[12]);
        pp_fat_close(&v);
        return;
    }
    pp_fat_close(&v);

    pp_profile_log("PP: Photo Play 2.0 image has not been repaired -- games will refuse"
                   " to start\n");

    /* The profile is applied more than once during start-up; say this once. */
    if (told)
        return;
    told = 1;
    ui_msgbox_header(MBX_WARNING, (char *) "Photo Play 2.0 image needs repairing",
                     (char *) "This is a Photo Play 2.0 image, and its Microcosm "
                              "CopyControl layout has not been restored.\n\n"
                              "The machine will boot and the menu will work, but the "
                              "games will fail to start and drop straight back to "
                              "the menu.\n\n"
                              "The protection keys on where two files physically sit on "
                              "the disk and on a pattern hidden past the end of one of "
                              "them -- none of which survives copying an image file by "
                              "file.\n\n"
                              "Run ppfix.exe -- it sits next to PeepeeBox.exe -- on "
                              "the image once to put the layout back. "
                              "It changes no game file.");
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

    /* 2.0 images arrive with their CopyControl layout destroyed by imaging; put it
       back before the machine starts.  No other release is touched by this. */
    pp_check_copycontrol(fn);

    /* Name the window after what the image actually is, rather than after
       whatever the working directory happens to be called -- with -P . that
       came out as ".".  The image says so itself in \FOTO\SETTINGS\MAIN.SET. */
    char ident[96];

    pp_image_label(fn, ident, sizeof(ident));
    if (ident[0]) {
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

/* Which touchscreen is attached.

   The cabinets shipped a 3M MicroTouch on COM3, and that is still the default and
   still what every disk image expects.  Elo SmartSet parts turn up in the same
   machines though, and a cabinet with one fitted needs the emulator to speak Elo
   instead -- so this is a setting rather than a constant.  It is a device internal
   name, not an index, because indices move when the device table does.

   A name this build does not have is ignored rather than fatal: an ini file is
   easy to mistype, and dropping back to the MicroTouch always boots. */
/* Which IRQ the touchscreen's COM3 is wired to.

   The PC standard for COM3 is 4, and that is what Photo Play gets.  Funny's Interactive
   Playworld wires it to 3, and its disk says so twice: AUTOEXEC.BAT loads
   `ELODEV 2310,3,9600,3`, whose fourth field is the IRQ, and FSYSTEM.EXE is started as
   `FSYSTEM.EXE C3 I3` -- the `I` switch, which it unmasks on the PIC itself at CS:0xC3D9.

   Getting this wrong fails in the most misleading way available.  ELODEV *polls* the port
   while it interrogates and configures the controller, so detection succeeds on either
   IRQ and the cabinet cheerfully prints "Controller 2310 found. Version: 1.7".  It then
   installs its own ISR and never polls again, so the first touch packet puts one byte in
   the receive register, nothing services the interrupt, Data Ready stays set, and the
   part stalls with the rest of the packet still queued.  Touch is dead and nothing says
   so. */
int
photoplay_com3_irq(void)
{
    return photoplay_is_funny() ? 3 : COM3_IRQ;
}

const char *
photoplay_touchscreen(void)
{
    static char name[64];
    const char *dflt = PHOTOPLAY_TABLET;

    /* The image gets the first word, the same way it does for the token.  Photo Play
       shipped MicroTouch and every one of its releases expects one, so that stays the
       default.  Funny's Interactive Playworld is the exception that made this worth
       doing: its own AUTOEXEC.BAT loads Elo's driver -- `ELODEV 2310,3,9600,3`, an
       E271-2310 on COM3 at 9600 -- and FSYSTEM.EXE probes for Elo before it will look
       at anything else, so an unconfigured rig should come up on the part the disk
       actually asks for rather than on one the user has to go and find.

       A choice already in the ini still wins: this only supplies the default. */
    if (photoplay_is_funny() && tablet_get_from_internal_name((char *) PHOTOPLAY_TABLET_ELO))
        dflt = PHOTOPLAY_TABLET_ELO;

    const char *s = config_get_string(PHOTOPLAY_SECTION, "touchscreen", (char *) dflt);

    if ((s == NULL) || !tablet_get_from_internal_name((char *) s))
        return dflt;
    snprintf(name, sizeof(name), "%s", s);
    return name;
}

void
photoplay_set_touchscreen(const char *internal_name)
{
    config_set_string(PHOTOPLAY_SECTION, "touchscreen", (char *) internal_name);
}

/* Which cabinet to be, when the image should not be the one to say.  An unrecognised
   value reads as "auto" rather than being fatal, for the same reason a bad touchscreen
   name does: an ini is easy to mistype, and asking the image always works. */
const char *
photoplay_product(void)
{
    static char name[16];
    const char *s = config_get_string(PHOTOPLAY_SECTION, "product",
                                      (char *) PHOTOPLAY_PRODUCT_AUTO);

    if ((s == NULL) || (strcmp(s, PHOTOPLAY_PRODUCT_PP) &&
                        strcmp(s, PHOTOPLAY_PRODUCT_FUNNY)))
        return PHOTOPLAY_PRODUCT_AUTO;
    snprintf(name, sizeof(name), "%s", s);
    return name;
}

void
photoplay_set_product(const char *product)
{
    config_set_string(PHOTOPLAY_SECTION, "product", (char *) product);
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
