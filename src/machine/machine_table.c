/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Handling of the emulated machines.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2016-2026 Miran Grca.
 *          Copyright 2017-2025 Fred N. van Kempen.
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/keyboard.h>
#include <86box/nvr.h>
#include <86box/sio.h>
#include <86box/sound.h>
#include <86box/snd_ac97.h>
#include <86box/video.h>
#include <86box/vid_cga.h>
#include <86box/vid_mcga.h>
#include <86box/plat_unused.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/machine.h>

const machine_filter_t machine_types[] = {
    { "None",                             MACHINE_TYPE_NONE        },
    { "[1979] 8088",                      MACHINE_TYPE_8088        },
    { "[1978] 8086",                      MACHINE_TYPE_8086        },
    { "[1982] 80286",                     MACHINE_TYPE_286         },
    { "[1988] i386SX",                    MACHINE_TYPE_386SX       },
    { "[1997] ALi M6117",                 MACHINE_TYPE_M6117       },
    { "[1992] 486SLC",                    MACHINE_TYPE_486SLC      },
    { "[1985] i386DX",                    MACHINE_TYPE_386DX       },
    { "[1989] i386DX/i486",               MACHINE_TYPE_386DX_486   },
    { "[1989] Socket 168/1",              MACHINE_TYPE_SOCKET1     },
    { "[1992] Socket 2",                  MACHINE_TYPE_SOCKET2     },
    { "[1993] Socket 3",                  MACHINE_TYPE_SOCKET3     },
    { "[1994] Socket 3 (PCI)",            MACHINE_TYPE_SOCKET3_PCI },
    { "[1993] Socket 3/4",                MACHINE_TYPE_SOCKET3_4   },
    { "[1999] STMicroelectronics STPC",   MACHINE_TYPE_STPC        },
    { "[1993] Socket 4",                  MACHINE_TYPE_SOCKET4     },
    { "[1994] Socket 4/5",                MACHINE_TYPE_SOCKET4_5   },
    { "[1994] Socket 5",                  MACHINE_TYPE_SOCKET5     },
    { "[1995] Socket 7 (Single Voltage)", MACHINE_TYPE_SOCKET7_3V  },
    { "[1996] Socket 7 (Dual Voltage)",   MACHINE_TYPE_SOCKET7     },
    { "[1998] Super Socket 7",            MACHINE_TYPE_SOCKETS7    },
    { "[1995] Socket 8",                  MACHINE_TYPE_SOCKET8     },
    { "[1997] Slot 1",                    MACHINE_TYPE_SLOT1       },
    { "[1998] Slot 1/2",                  MACHINE_TYPE_SLOT1_2     },
    { "[1998] Slot 1/Socket 370",         MACHINE_TYPE_SLOT1_370   },
    { "[1998] Slot 2",                    MACHINE_TYPE_SLOT2       },
    { "[1998] Socket 370",                MACHINE_TYPE_SOCKET370   },
    { "Miscellaneous",                    MACHINE_TYPE_MISC        }
};

const machine_filter_t machine_chipsets[] = {
    { "None",                       MACHINE_CHIPSET_NONE                },
    { "Discrete",                   MACHINE_CHIPSET_DISCRETE            },
    { "Proprietary",                MACHINE_CHIPSET_PROPRIETARY         },
    { "Headland GC100A",            MACHINE_CHIPSET_GC100A              },
    { "Headland GC103",             MACHINE_CHIPSET_GC103               },
    { "Headland HT18",              MACHINE_CHIPSET_HT18                },
    { "ACC 2036",                   MACHINE_CHIPSET_ACC_2036            },
    { "ACC 2168",                   MACHINE_CHIPSET_ACC_2168            },
    { "ALi M1217",                  MACHINE_CHIPSET_ALI_M1217           },
    { "ALi M6117",                  MACHINE_CHIPSET_ALI_M6117           },
    { "ALi M1409",                  MACHINE_CHIPSET_ALI_M1409           },
    { "ALi M1429",                  MACHINE_CHIPSET_ALI_M1429           },
    { "ALi M1429G",                 MACHINE_CHIPSET_ALI_M1429G          },
    { "ALi M1489",                  MACHINE_CHIPSET_ALI_M1489           },
    { "ALi ALADDiN IV+",            MACHINE_CHIPSET_ALI_ALADDIN_IV_PLUS },
    { "ALi ALADDiN V",              MACHINE_CHIPSET_ALI_ALADDIN_V       },
    { "ALi ALADDiN-PRO II",         MACHINE_CHIPSET_ALI_ALADDIN_PRO_II  },
    { "C&T PC/AT",                  MACHINE_CHIPSET_CT_AT               },
    { "C&T 386/AT",                 MACHINE_CHIPSET_CT_386              },
    { "C&T 82C235 SCAT",            MACHINE_CHIPSET_SCAT                },
    { "C&T 82C236 SCATsx",          MACHINE_CHIPSET_SCAT_SX             },
    { "C&T CS8221 NEAT",            MACHINE_CHIPSET_NEAT                },
    { "C&T CS8281 NEATsx",          MACHINE_CHIPSET_NEAT_SX             },
    { "C&T CS4031",                 MACHINE_CHIPSET_CT_CS4031           },
    { "Contaq 82C596",              MACHINE_CHIPSET_CONTAQ_82C596       },
    { "Contaq 82C597",              MACHINE_CHIPSET_CONTAQ_82C597       },
    { "IMS 8848",                   MACHINE_CHIPSET_IMS_8848            },
    { "Intel 82335",                MACHINE_CHIPSET_INTEL_82335         },
    { "Intel 420TX",                MACHINE_CHIPSET_INTEL_420TX         },
    { "Intel 420ZX",                MACHINE_CHIPSET_INTEL_420ZX         },
    { "Intel 420EX",                MACHINE_CHIPSET_INTEL_420EX         },
    { "Intel 430LX",                MACHINE_CHIPSET_INTEL_430LX         },
    { "Intel 430NX",                MACHINE_CHIPSET_INTEL_430NX         },
    { "Intel 430FX",                MACHINE_CHIPSET_INTEL_430FX         },
    { "Intel 430HX",                MACHINE_CHIPSET_INTEL_430HX         },
    { "Intel 430VX",                MACHINE_CHIPSET_INTEL_430VX         },
    { "Intel 430TX",                MACHINE_CHIPSET_INTEL_430TX         },
    { "Intel 450KX",                MACHINE_CHIPSET_INTEL_450KX         },
    { "Intel 440FX",                MACHINE_CHIPSET_INTEL_440FX         },
    { "Intel 440LX",                MACHINE_CHIPSET_INTEL_440LX         },
    { "Intel 440EX",                MACHINE_CHIPSET_INTEL_440EX         },
    { "Intel 440BX",                MACHINE_CHIPSET_INTEL_440BX         },
    { "Intel 440ZX",                MACHINE_CHIPSET_INTEL_440ZX         },
    { "Intel 440GX",                MACHINE_CHIPSET_INTEL_440GX         },
    { "OPTi 283",                   MACHINE_CHIPSET_OPTI_283            },
    { "OPTi 291",                   MACHINE_CHIPSET_OPTI_291            },
    { "OPTi 381",                   MACHINE_CHIPSET_OPTI_381            },
    { "OPTi 391",                   MACHINE_CHIPSET_OPTI_391            },
    { "OPTi 481",                   MACHINE_CHIPSET_OPTI_481            },
    { "OPTi 493",                   MACHINE_CHIPSET_OPTI_493            },
    { "OPTi 495SLC",                MACHINE_CHIPSET_OPTI_495SLC         },
    { "OPTi 495SX",                 MACHINE_CHIPSET_OPTI_495SX          },
    { "OPTi 496",                   MACHINE_CHIPSET_OPTI_496            },
    { "OPTi 498",                   MACHINE_CHIPSET_OPTI_498            },
    { "OPTi 499",                   MACHINE_CHIPSET_OPTI_499            },
    { "OPTi 895/802G",              MACHINE_CHIPSET_OPTI_895_802G       },
    { "OPTi 547",                   MACHINE_CHIPSET_OPTI_547            },
    { "OPTi 571",                   MACHINE_CHIPSET_OPTI_571            },
    { "OPTi 597",                   MACHINE_CHIPSET_OPTI_597            },
    { "OPTi Viper",                 MACHINE_CHIPSET_OPTI_VIPER          },
    { "SARC RC2016A",               MACHINE_CHIPSET_SARC_RC2016A        },
    { "SiS 310",                    MACHINE_CHIPSET_SIS_310             },
    { "SiS 401",                    MACHINE_CHIPSET_SIS_401             },
    { "SiS 460",                    MACHINE_CHIPSET_SIS_460             },
    { "SiS 461",                    MACHINE_CHIPSET_SIS_461             },
    { "SiS 471",                    MACHINE_CHIPSET_SIS_471             },
    { "SiS 496",                    MACHINE_CHIPSET_SIS_496             },
    { "SiS 501",                    MACHINE_CHIPSET_SIS_501             },
    { "SiS 5501",                   MACHINE_CHIPSET_SIS_5501            },
    { "SiS 5511",                   MACHINE_CHIPSET_SIS_5511            },
    { "SiS 5571",                   MACHINE_CHIPSET_SIS_5571            },
    { "SiS 5581",                   MACHINE_CHIPSET_SIS_5581            },
    { "SiS 5591",                   MACHINE_CHIPSET_SIS_5591            },
    { "SiS (5)600",                 MACHINE_CHIPSET_SIS_5600            },
    { "SMSC VictoryBX-66",          MACHINE_CHIPSET_SMSC_VICTORYBX_66   },
    { "STPC Client",                MACHINE_CHIPSET_STPC_CLIENT         },
    { "STPC Consumer-II",           MACHINE_CHIPSET_STPC_CONSUMER_II    },
    { "STPC Elite",                 MACHINE_CHIPSET_STPC_ELITE          },
    { "STPC Atlas",                 MACHINE_CHIPSET_STPC_ATLAS          },
    { "Symphony SL82C460 Haydn II", MACHINE_CHIPSET_SYMPHONY_SL82C460   },
    { "UMC UM82C480",               MACHINE_CHIPSET_UMC_UM82C480        },
    { "UMC UM82C491",               MACHINE_CHIPSET_UMC_UM82C491        },
    { "UMC UM8881",                 MACHINE_CHIPSET_UMC_UM8881          },
    { "UMC UM8890BF",               MACHINE_CHIPSET_UMC_UM8890BF        },
    { "VIA VT82C495",               MACHINE_CHIPSET_VIA_VT82C495        },
    { "VIA VT82C496G",              MACHINE_CHIPSET_VIA_VT82C496G       },
    { "VIA Apollo VPX",             MACHINE_CHIPSET_VIA_APOLLO_VPX      },
    { "VIA Apollo VP3",             MACHINE_CHIPSET_VIA_APOLLO_VP3      },
    { "VIA Apollo MVP3",            MACHINE_CHIPSET_VIA_APOLLO_MVP3     },
    { "VIA Apollo Pro",             MACHINE_CHIPSET_VIA_APOLLO_PRO      },
    { "VIA Apollo Pro 133",         MACHINE_CHIPSET_VIA_APOLLO_PRO_133  },
    { "VIA Apollo Pro 133A",        MACHINE_CHIPSET_VIA_APOLLO_PRO_133A },
    { "VLSI SCAMP",                 MACHINE_CHIPSET_VLSI_SCAMP          },
    { "VLSI VL82C480",              MACHINE_CHIPSET_VLSI_VL82C480       },
    { "VLSI VL82C481",              MACHINE_CHIPSET_VLSI_VL82C481       },
    { "VLSI VL82C486",              MACHINE_CHIPSET_VLSI_VL82C486       },
    { "VLSI SuperCore",             MACHINE_CHIPSET_VLSI_SUPERCORE      },
    { "VLSI Wildcat",               MACHINE_CHIPSET_VLSI_WILDCAT        },
    { "WD76C10",                    MACHINE_CHIPSET_WD76C10             }
};

/*
   NOTE: The AMI MegaKey tests were done on a real Intel Advanced/ATX
     (thanks, MrKsoft for running my AMIKEY.COM on it), but the
     technical specifications of the other Intel machines confirm
     that the other boards also have the MegaKey.

   NOTE: The later (ie. not AMI Color) Intel AMI BIOS'es execute a
     sequence of commands (B8, BA, BB) during one of the very first
     phases of POST, in a way that is only valid on the AMIKey-3
     KBC firmware, that includes the Classic PCI/ED (Ninja) BIOS
     which otherwise does not execute any AMI KBC commands, which
     indicates that the sequence is a leftover of whatever AMI
     BIOS (likely a laptop one since the AMIKey-3 is a laptop KBC
     firmware!) Intel forked.

   NOTE: The AMI MegaKey commands blanked in the technical reference
     are CC and and C4, which are Set P14 High and Set P14 Low,
     respectively. Also, AMI KBC command C1, mysteriously missing
     from the technical references of AMI MegaKey and earlier, is
     Write Input Port, same as on AMIKey-3.
*/

const machine_t machines[] = {
    // clang-format off
    /* According to MrKsoft, his real 4DPS has an AMIKey-2, which is an updated version
       of type 'H'. There are other variants of the board with Holtek HT6542B KBCs. */
    {
        .name              = "[SiS 496] Zida Tomato 4DPS",
        .internal_name     = "4dps",
        .type              = MACHINE_TYPE_SOCKET3_PCI,
        .chipset           = MACHINE_CHIPSET_SIS_496,
        .init              = machine_at_4dps_init,
        .p1_handler        = machine_generic_p1_handler,
        .gpio_handler      = NULL,
        .available_flag    = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu               = {
            .package     = CPU_PKG_SOCKET3,
            .block       = CPU_BLOCK_NONE,
            .min_bus     = 25000000,
            .max_bus     = 50000000,
            .min_voltage = 3300,
            .max_voltage = 5000,
            .min_multi   = 0,
            .max_multi   = 0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags     = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_GAMEPORT,
        .ram       = {
            .min  = 2048,
            .max  = 261120,
            .step = 1024
        },
        .nvrmask                  = 255,
        .jumpered_ecp_dma         = MACHINE_DMA_DISABLED | MACHINE_DMA_1 | MACHINE_DMA_3,
        .default_jumpered_ecp_dma = 3,
        .kbc_device               = &kbc_at_device,
        .kbc_params               = KBC_VEN_AMI | 0x00004800,
        .nvr_device               = NULL,
        .nvr_params               = 0x00000000,
        .sio_device               = NULL,
        .sio_params               = 0x00000000,
        .kbc_p1                   = 0x00000cf0,
        .gpio                     = 0xffffffff,
        .gpio_acpi                = 0xffffffff,
        .device                   = NULL,
        .kbd_device               = NULL,
        .fdc_device               = NULL,
        .vid_device               = NULL,
        .snd_device               = NULL,
        .net_device               = NULL,
        .aliases                  = { "" }
    },

    {
        .name              = NULL,
        .internal_name     = NULL,
        .type              = MACHINE_TYPE_NONE,
        .chipset           = MACHINE_CHIPSET_NONE,
        .init              = NULL,
        .p1_handler        = NULL,
        .gpio_handler      = NULL,
        .available_flag    = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu               = {
            .package     = 0,
            .block       = CPU_BLOCK_NONE,
            .min_bus     = 0,
            .max_bus     = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi   = 0,
            .max_multi   = 0
        },
        .bus_flags = MACHINE_BUS_NONE,
        .flags     = MACHINE_FLAGS_NONE,
        .ram       = {
            .min  = 0,
            .max  = 0,
            .step = 0
        },
        .nvrmask                  = 0,
        .jumpered_ecp_dma         = 0,
        .default_jumpered_ecp_dma = -1,
        .kbc_device               = NULL,
        .kbc_params               = 0x00000000,
        .nvr_device               = NULL,
        .nvr_params               = 0x00000000,
        .sio_device               = NULL,
        .sio_params               = 0x00000000,
        .kbc_p1                   = 0x00000000,
        .gpio                     = 0xffffffff,
        .gpio_acpi                = 0xffffffff,
        .device                   = NULL,
        .kbd_device               = NULL,
        .fdc_device               = NULL,
        .vid_device               = NULL,
        .snd_device               = NULL,
        .net_device               = NULL,
        .aliases                  = { "" }
    }
    // clang-format on
};

/* This is so Disabled comes first. */
static const int   dma_mapping[9] = { DMA_NONE, DMA_DISABLED, 0, 1, 2, 3, 5, 6, 7 };
static const char *dma_names[9]   = { "None", "Disabled", "0", "1", "2", "3", "5", "6", "7" };

/* Saved copies - jumpers get applied to these.
   We use also machine_gpio to store IBM PC/XT jumpers as they need more than one byte. */
static uint32_t machine_p1_default;
static uint32_t machine_p1;

static uint32_t machine_gpio_default;
static uint32_t machine_gpio;

static uint32_t machine_gpio_acpi_default;
static uint32_t machine_gpio_acpi;

static int machine_is_ps2 = 0;

void *machine_snd = NULL;

uint8_t
machine_get_p1_default(void)
{
    return machine_p1_default;
}

void
machine_set_p1_default(uint8_t val)
{
    machine_p1 = machine_p1_default = val;
}

void
machine_set_p1(uint8_t val)
{
    machine_p1 = val;
}

void
machine_and_p1(uint8_t val)
{
    machine_p1 = machine_p1_default & val;
}

uint8_t
machine_generic_p1_handler(void)
{
    return video_is_mda() ? 0xf0 : 0xb0;
}

uint8_t
machine_get_p1(uint8_t kbc_p1)
{
    uint8_t low_bits = ((machine_p1 >> 8) + 1) & 0x03;
    uint8_t ret      = 0xff;

    if (machines[machine].p1_handler)
        ret = machines[machine].p1_handler();

    ret &= (machine_p1 & 0xff);

    ret |= ((machine_p1 >> 8) & 0xff);

    ret ^= ((machine_p1 >> 16) & 0xff);

    ret &= kbc_p1;

    machine_p1 = (machine_p1 & 0xfffffcff) | (low_bits << 8);

    return ret;
}

void
machine_init_p1(void)
{
    machine_p1 = machine_p1_default = machines[machine].kbc_p1;
}

uint32_t
machine_get_gpio_default(void)
{
    return machine_gpio_default;
}

uint32_t
machine_get_gpio(void)
{
    return machine_gpio;
}

void
machine_set_gpio_default(uint32_t val)
{
    machine_gpio = machine_gpio_default = val;
}

void
machine_set_gpio(uint32_t val)
{
    machine_gpio = val;
}

void
machine_and_gpio(uint32_t val)
{
    machine_gpio = machine_gpio_default & val;
}

uint32_t
machine_handle_gpio(uint8_t write, uint32_t val)
{
    uint32_t ret = 0xffffffff;

    if (machines[machine].gpio_handler)
        ret = machines[machine].gpio_handler(write, val);
    else {
        if (write)
            machine_gpio = machine_gpio_default & val;
        else
            ret = machine_gpio;
    }

    return ret;
}

void
machine_init_gpio(void)
{
    machine_gpio = machine_gpio_default = machines[machine].gpio;
}

uint32_t
machine_get_gpio_acpi_default(void)
{
    return machine_gpio_acpi_default;
}

uint32_t
machine_get_gpio_acpi(void)
{
    return machine_gpio_acpi;
}

void
machine_set_gpio_acpi_default(uint32_t val)
{
    machine_gpio_acpi = machine_gpio_acpi_default = val;
}

void
machine_set_gpio_acpi(uint32_t val)
{
    machine_gpio_acpi = val;
}

void
machine_and_gpio_acpi(uint32_t val)
{
    machine_gpio_acpi = machine_gpio_acpi_default & val;
}

uint32_t
machine_handle_gpio_acpi(uint8_t write, uint32_t val)
{
    uint32_t ret = 0xffffffff;

    if (machines[machine].gpio_acpi_handler)
        ret = machines[machine].gpio_acpi_handler(write, val);
    else {
        if (write)
            machine_gpio_acpi = machine_gpio_acpi_default & val;
        else
            ret = machine_gpio_acpi;
    }

    return ret;
}

void
machine_init_gpio_acpi(void)
{
    machine_gpio_acpi = machine_gpio_acpi_default = machines[machine].gpio_acpi;
}

int
machine_count(void)
{
    return ((sizeof(machines) / sizeof(machine_t)) - 1);
}

const char *
machine_getname(int m)
{
    return (machines[m].name);
}

const device_t *
machine_get_kbc_device(int m)
{
    if (machines[m].kbc_device)
        return (machines[m].kbc_device);

    return (NULL);
}

const device_t *
machine_get_nvr_device(int m)
{
    if (machines[m].nvr_device)
        return (machines[m].nvr_device);

    return (NULL);
}

const device_t *
machine_get_sio_device(int m)
{
    if (machines[m].sio_device)
        return (machines[m].sio_device);

    return (NULL);
}

const device_t *
machine_get_device(int m)
{
    if (machines[m].device)
        return (machines[m].device);

    return (NULL);
}

const device_t *
machine_get_fdc_device(int m)
{
    if (machines[m].fdc_device)
        return (machines[m].fdc_device);

    return (NULL);
}

const device_t *
machine_get_vid_device(int m)
{
    if (machines[m].vid_device)
        return (machines[m].vid_device);

    return (NULL);
}

const device_t *
machine_get_snd_device(int m)
{
    if (machines[m].snd_device)
        return (machines[m].snd_device);

    return (NULL);
}

const device_t *
machine_get_net_device(int m)
{
    if (machines[m].net_device)
        return (machines[m].net_device);

    return (NULL);
}

const char *
machine_get_internal_name(void)
{
    return (machines[machine].internal_name);
}

const char *
machine_get_internal_name_ex(int m)
{
    return (machines[m].internal_name);
}

int
machine_get_nvrmask(int m)
{
    return (machines[m].nvrmask);
}

int
machine_has_flags(int m, uintptr_t flags)
{
    int ret = machines[m].flags & flags;

    /* Can't have PS/2 ports with an AT KBC. */
    if ((flags & MACHINE_PS2_KBC) &&
        (machines[m].bus_flags & MACHINE_BUS_PS2_PORTS))
        ret |= MACHINE_PS2_KBC;

    return ret;
}

void
machine_set_ps2(void)
{
    if (machines[machine].bus_flags & MACHINE_BUS_PS2_PORTS)
        machine_is_ps2 = 1;
    else
        machine_is_ps2 = 0;
}

void
machine_force_ps2(int is_ps2)
{
    machine_is_ps2 = is_ps2;
}

int
machine_has_flags_ex(uintptr_t flags)
{
    int ret = machine_has_flags(machine, flags);

    /* PeepeeBox: upstream carved out the Amstrad PC 5286, whose KBC is not
       really PS/2.  That machine no longer exists here. */
    if ((flags & MACHINE_PS2_KBC) && machine_is_ps2)
        ret |= MACHINE_PS2_KBC;

    return ret;
}

int
machine_has_bus(int m, uintptr_t bus_flags)
{
    int ret = machines[m].bus_flags & bus_flags;

    /* TODO: Move the KBD flags to the machine table! */
    /* PeepeeBox: the PC 5086 exception is gone with the machine. */
    if ((bus_flags & MACHINE_BUS_XT_KBD) &&
        !(machines[m].bus_flags & MACHINE_BUS_ISA16) &&
        !(machines[m].bus_flags & MACHINE_BUS_PS2_PORTS))
        ret |= MACHINE_BUS_XT_KBD;

#ifdef ONLY_AT_KBD_ON_AT_KBC
    if ((bus_flags & MACHINE_BUS_AT_KBD) &&
        (IS_AT(m)) &&
        !(machines[m].bus_flags & MACHINE_BUS_PS2_PORTS))
        ret |= MACHINE_BUS_AT_KBD;
#else
    if ((bus_flags & MACHINE_BUS_AT_KBD) && (IS_AT(m)))
        ret |= MACHINE_BUS_AT_KBD;
#endif

    return ret;
}

int
machine_has_cartridge(int m)
{
    return (machine_has_flags(m, MACHINE_CARTRIDGE) ? 1 : 0);
}

int
machine_has_jumpered_ecp_dma(int m, int dma)
{
    if (dma == DMA_ANY)
        return !!(machines[m].jumpered_ecp_dma & MACHINE_DMA_JUMPERS_MASK);
    else
        return !!(machines[m].jumpered_ecp_dma & (1 << dma));
}

int
machine_get_default_jumpered_ecp_dma(int m)
{
    return machines[m].default_jumpered_ecp_dma;
}

int
machine_map_jumpered_ecp_dma(int dma)
{
    return dma_mapping[dma];
}

const char *
machine_get_jumpered_ecp_dma_name(int dma)
{
    return dma_names[dma];
}

int
machine_get_min_ram(int m)
{
    return (machines[m].ram.min);
}

int
machine_get_max_ram(int m)
{
    return MIN(((int) machines[m].ram.max), 3145728);
}

int
machine_get_ram_granularity(int m)
{
    return (machines[m].ram.step);
}

int
machine_get_type(int m)
{
    return (machines[m].type);
}

int
machine_get_chipset(int m)
{
    return (machines[m].chipset);
}

int
machine_get_machine_from_internal_name(const char *s)
{
    int c = 0;

    while (machines[c].init != NULL) {
        if (!strcmp(machines[c].internal_name, s))
            return c;
        c++;
    }

    return -1;
}

int
machine_has_mouse(void)
{
    return (machines[machine].flags & MACHINE_MOUSE);
}

const char *
machine_get_nvr_name_ex(int m)
{
    const char     *ret = machines[m].internal_name;
    const device_t *dev = machine_get_device(m);

    if (dev != NULL) {
        device_context(dev);
        const char *bios = device_get_config_bios("bios");
        if ((bios != NULL) && (strcmp(bios, "") != 0))
            ret = bios;
        device_context_restore();
    }

    return ret;
}

const char *
machine_get_nvr_name(void)
{
    return machine_get_nvr_name_ex(machine);
}


