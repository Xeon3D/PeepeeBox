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
#
#          Copyright 2020-2021 David Hrdlička.
#          Copyright 2025 starfrost
#

add_library(vid OBJECT
    agpgart.c
    # Video core
    video.c
    vid_table.c

    # DDC / monitor identification
    vid_ddc.c
    vid_ddc_edid_custom.c

    # Super VGA core
    vid_svga.c
    vid_svga_render.c

    # Inert 8514/A and XGA hooks -- see the file comment for why the cards
    # themselves are gone but these entry points are not.
    vid_svga_addons.c

    # Timer helper the emulator core uses for frame pacing.
    nv/nv_rivatimer.c

    # Cirrus Logic -- the CL-GD5480 is the cabinet's video card and the only
    # one PeepeeBox builds.
    vid_cl54xx.c
)

if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    target_include_directories(vid PRIVATE /usr/local/include)
endif()

