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

/* Overwrite the loaded configuration with the fixed Photo Play machine profile.
   Called at the end of config_load(); see the file comment in photoplay.c for
   why this is enforced at load time rather than left to the config file. */
extern void photoplay_apply_profile(void);

#ifdef __cplusplus
}
#endif

#endif /*EMU_PHOTOPLAY_H*/
