/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Socket 7 boards.
 *
 *          PeepeeBox keeps exactly one of these: the PC Partner MB540N, an
 *          i430TX board, because Funny's Interactive Playworld runs on a
 *          Pentium MMX rather than on the 486 the Photo Play cabinets used.
 *          The function is upstream 86Box's, unchanged -- the rest of
 *          upstream's Socket 7 boards are not here because no cabinet uses
 *          them.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/keyboard.h>
#include <86box/flash.h>
#include <86box/sio.h>
#include <86box/spd.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/pci.h>

int
machine_at_mb540n_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/mb540n/Tx0720ug.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4); /* PIIX4 */

    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&um8669f_device, (void *) 0);
    device_add(&sst_flash_29ee010_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 128);

    return ret;
}
