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
#          Copyright 2024-2026 Jasmine Iwanek.
#

add_library(snd OBJECT
    sound.c
    snd_opl.c
    snd_opl2_nuked.c
    snd_opl3_nuked.c
    snd_cqm_nuked.c
    snd_opl_ymfm.cpp
    midi.c
    snd_speaker.c
    # snd_sb.c is one file covering Sound Blaster, ESS and Aztech.  Only the
    # ESS ES1688 is reachable, but the file links against all three families'
    # mixers and codecs, so these come with it.
    snd_ad1848.c
    snd_cms.c
    snd_azt2316a.c
    snd_azt2320.c
    snd_sb.c
    snd_sb_dsp.c
    snd_emu8k.c
    snd_mpu401.c
    snd_opl_esfm.c
    sound_util.c
)

# TODO: Should platform-specific audio driver be here?
if(AUDIO4)
    target_sources(snd PRIVATE audio4.c)
elseif(SNDIO)
    target_sources(snd PRIVATE sndio.c)
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(SNDIO IMPORTED_TARGET sndio)
    if(SNDIO_FOUND)
        target_link_libraries(86Box PkgConfig::SNDIO)
    else()
        find_path(SNDIO_INCLUDE_DIR NAMES "sndio.h")
        find_library(SNDIO_LIBRARY sndio)

        target_link_libraries(86Box ${SNDIO_LIBRARY})
    endif()

    include_directories(${SNDIO_INCLUDE_DIRS})
elseif(OPENAL)
    if(VCPKG_TOOLCHAIN)
        find_package(OpenAL CONFIG REQUIRED)
    elseif(MINGW)
        find_package(OpenAL MODULE REQUIRED)
    else()
        find_package(OpenAL REQUIRED)
    endif()

    if(TARGET OpenAL::OpenAL)
        target_link_libraries(86Box OpenAL::OpenAL)
        if(WIN32 AND STATIC_BUILD)
            target_link_libraries(OpenAL::OpenAL INTERFACE avrt)
        endif()
    else()
        target_link_libraries(86Box ${OPENAL_LIBRARY})
        if(WIN32 AND STATIC_BUILD)
            target_link_libraries(${OPENAL_LIBRARY} INTERFACE avrt)
        endif()
    endif()

    include_directories(${OPENAL_INCLUDE_DIR})

    target_sources(snd PRIVATE openal.c)
else()
    if(WIN32)
        option(FAUDIO "Use FAudio instead of XAudio2" OFF)
    endif()

    target_sources(snd PRIVATE xaudio2.c)

    if(NOT WIN32 OR FAUDIO)
        find_package(PkgConfig REQUIRED)

        # Use FAudio, a reimplementation of XAudio2
        pkg_check_modules(FAUDIO IMPORTED_TARGET FAudio)
        if(FAUDIO_FOUND)
            target_link_libraries(86Box PkgConfig::FAUDIO)
        else()
            find_path(FAUDIO_INCLUDE_DIR NAMES "FAudio.h")
            find_library(FAUDIO_LIBRARY FAudio)

            target_link_libraries(86Box ${FAUDIO_LIBRARY})
        endif()

        include_directories(${FAUDIO_INCLUDE_DIRS})

        set_property(SOURCE xaudio2.c PROPERTY COMPILE_DEFINITIONS USE_FAUDIO)
    endif()
endif()

# PeepeeBox: no MIDI hardware in the cabinet -- RtMidi, FluidSynth, MUNT,

# the Sound Canvas CLAP host and the OPL4-ML daughterboard are all gone.

add_subdirectory(esfmu)
target_link_libraries(86Box esfmu)

add_subdirectory(ymfm)
target_link_libraries(86Box ymfm)

if(SOFTMODEM)
    target_compile_definitions(snd PRIVATE USE_SOFTMODEM)
endif()

find_package(PkgConfig)
if(APPLE)
    pkg_check_modules(SERIALPORT IMPORTED_TARGET libserialport)
else()
    pkg_check_modules(SERIALPORT libserialport)
endif()

if(SERIALPORT_FOUND OR DEFINED LIBSERIALPORT_ROOT)
    add_compile_definitions(USE_LIBSERIALPORT=1)

    if(APPLE)
        if(DEFINED LIBSERIALPORT_ROOT)
            include_directories(${LIBSERIALPORT_ROOT}/include)
            target_link_libraries(86Box "${LIBSERIALPORT_ROOT}/lib/libserialport.dylib")
        else()
            target_link_libraries(snd PRIVATE PkgConfig::SERIALPORT)
            target_link_libraries(86Box PkgConfig::SERIALPORT)
        endif()
    elseif(WIN32)
        include_directories(${SERIALPORT_INCLUDE_DIRS})
        target_link_libraries(86Box ${SERIALPORT_LIBRARIES} SetupAPI)
    else()
        include_directories(${SERIALPORT_INCLUDE_DIRS})
        target_link_libraries(86Box ${SERIALPORT_LIBRARIES})
    endif()
endif()

