/*
 * PeepeeBox   A fork of 86Box that emulates the funworld Photo Play / I.G.O.
 *             arcade kiosk hardware, including its two protection tokens.
 *
 *             The fixed Photo Play machine profile.
 *
 * Authors:    The HUEG PP team.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#ifndef EMU_PHOTOPLAY_H
#define EMU_PHOTOPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Internal names of the fixed hardware.  Referenced by the settings UI so that
   the one machine PeepeeBox emulates is named in exactly one place. */
#define PHOTOPLAY_MACHINE     "4dps"             /* Zida Tomato 4DPS, SiS 496       */
#define PHOTOPLAY_CPU_FAMILY  "idx4"             /* Intel iDX4                      */
#define PHOTOPLAY_CPU_SPEED   100000000          /* 100 MHz (3x 33 MHz bus)         */
#define PHOTOPLAY_MEM_SIZE    16384              /* 16 MB, in KB                    */
#define PHOTOPLAY_GFXCARD     "cl_gd5480_pci"    /* Cirrus Logic CL-GD5480          */
#define PHOTOPLAY_SNDCARD     "ess_es1688"       /* ESS ES1688 AudioDrive           */
#define PHOTOPLAY_TABLET      "microtouch_touchpen"
#define PHOTOPLAY_TABLET_NAME "3M MicroTouch (Serial)"
#define PHOTOPLAY_TABLET_PORT 2                  /* COM3                            */
#define PHOTOPLAY_DONGLE      "dongle_photoplay"
#define PHOTOPLAY_DISK_IMAGE  "HardDisk.img"

/* The cabinets shipped without an optical drive, but service and installation
   media exist, so one can be switched on.  When it is, it is always the same
   drive: a generic 52x ATAPI CD-ROM as secondary master.  See photoplay.c. */
#define PHOTOPLAY_CDROM_TYPE  "86cd"
#define PHOTOPLAY_CDROM_SPEED 52
#define PHOTOPLAY_CDROM_CHAN  2                  /* secondary master           */
#define PHOTOPLAY_FDD_TYPE    "35_2hd"           /* 3.5" 1.44M, the IBM drive  */
#define PHOTOPLAY_SECTION     "Photo Play"       /* PeepeeBox's own ini section */

/* Overwrite the loaded configuration with the fixed Photo Play machine profile.
   Called at the end of config_load(); see the file comment in photoplay.c for
   why this is enforced at load time rather than left to the config file. */
extern void photoplay_apply_profile(void);

/* Whether the optional CD-ROM drive is attached.  Toggled from the Tools menu;
   persisted in the [Photo Play] section of the config file. */
extern int  photoplay_cdrom_enabled(void);
extern void photoplay_set_cdrom_enabled(int enabled);

/* Whether the optional 3.5" floppy drive is attached.  Same deal as the
   CD-ROM: toggled from the Tools menu, persisted in [Photo Play]. */
extern int  photoplay_fdd_enabled(void);
extern void photoplay_set_fdd_enabled(int enabled);

/* Work out which release and territory a disk image is, from its own
   \FOTO\SETTINGS\MAIN.SET.  Returns 1 and fills `out` with something like
   "IGO 5 PT" on success, 0 if the image is missing, foreign or unreadable. */
extern int  photoplay_identify(const char *img_path, char *out, size_t outsz);

/* The same, but also handing back the two raw fields the dongle needs: the
   MAIN.SET "Version" string verbatim (e.g. "Version 2000 (DE)"), which is what
   the guest compares its dongle record against, and the bare territory code.
   Either out-pointer may be NULL. */
extern int  photoplay_identify_ex(const char *img_path, char *out, size_t outsz,
                                  char *banner_out, size_t bsz,
                                  char *terr_out, size_t tsz);

/* What the disk image sitting next to the executable says it is.  Answers from a
   cache, so the dongle can ask at every hard reset without re-reading the FAT.
   Returns 1 when the image identified itself. */
extern int  photoplay_image_ident(char *banner_out, size_t bsz,
                                  char *terr_out, size_t tsz);

#ifdef __cplusplus
}
#endif

#endif /*EMU_PHOTOPLAY_H*/
