/*
 * PeepeeBox   A fork of 86Box that emulates the funworld Photo Play / I.G.O.
 *             arcade kiosk hardware, including its protection token.
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
#define PHOTOPLAY_TABLET_ELO  "elo_touchscreen"  /* Elo TouchSystems SmartSet       */
#define PHOTOPLAY_TABLET_PORT 2                  /* COM3                            */
#define PHOTOPLAY_DONGLE      "dongle_photoplay"
#define PHOTOPLAY_MODEM_SUPRA "modem_supra"      /* Diamond SupraExpress 56e PRO */
#define PHOTOPLAY_MODEM_ELSA  "modem_elsa"       /* ELSA MicroLink 56k           */
#define PHOTOPLAY_MODEM_PORT  3                  /* COM4, 0x02E8               */
#define PHOTOPLAY_MODEM_IRQ   10                 /* what the cabinet's NET.CFG says */
#define PHOTOPLAY_FUNLINK     "funlink"          /* the fun.link adapter        */
#define PHOTOPLAY_FUNLINK_PORT 0                 /* COM1, 0x03F8, IRQ 4         */

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

/* The IRQ the cabinet wires COM3 to: the PC-standard 4.  See photoplay.c.  Called
   from serial_init() when the standalone COM3 is created. */
extern int photoplay_com3_irq(void);

/* Which modem is fitted to COM4, by device internal name, and the IRQ that port
   runs on.  The cabinets that were on fun.net had one of two parts there -- a
   Diamond SupraExpress 56e PRO or an ELSA MicroLink 56k -- and the rest had
   nothing, which is the default and what an empty string means.  Chosen from the
   Tools menu, persisted in [Photo Play].  See photoplay.c for why the IRQ is 10
   and not 3.

   photoplay_modem_list() enumerates the parts the dialog offers, returning NULL
   past the end; the list lives here rather than in the UI because "which parts
   these cabinets had" is this file's question. */
extern const char *photoplay_modem(void);
extern void        photoplay_set_modem(const char *internal_name);
extern const char *photoplay_modem_list(int index);
extern int         photoplay_com4_irq(void);

/* Whether the fun.link adapter is fitted.  fun.link joins cabinets together so
   they can play each other, over a multi-drop serial bus on COM1 at 115200 --
   not the parallel port, whatever the 25-pin plug on the box suggests.  See
   docs/research/34-funlink.md.

   A part that is fitted rather than a part that is: most cabinets never had one,
   and the games only reach for it when the menu launches them with a non-zero
   /IPX=.  Everything past "is it there" -- who hosts the bus, at what address --
   belongs to the device's own options.  Persisted in [Photo Play]. */
extern int  photoplay_funlink_enabled(void);
extern void photoplay_set_funlink_enabled(int enabled);

/* Which touchscreen is wired to the cabinet, by device internal name.  The
   machines shipped a 3M MicroTouch and that stays the default, but an Elo
   SmartSet is the other part these cabinets are found with, so it is a choice
   rather than a constant.  Persisted in [Photo Play]; chosen from the toolbar. */
extern const char *photoplay_touchscreen(void);
extern void        photoplay_set_touchscreen(const char *internal_name);

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
