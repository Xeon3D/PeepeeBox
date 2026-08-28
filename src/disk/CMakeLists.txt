#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script.
#
# Authors: David Hrdlička, <hrdlickadavid@outlook.com>
#          Jasmine Iwanek, <jriwanek@gmail.com>
#
#          Copyright 2020-2021 David Hrdlička.
#          Copyright 2024      Jasmine Iwanek.
#

add_library(hdd OBJECT
    hdd.c
    hdd_image.c
    hdd_table.c
    hdc.c
    hdc_ide.c
    hdc_ide_sff8038i.c
    hdd_audio.c
)

add_subdirectory(minivhd)
target_link_libraries(86Box minivhd)
