#
# PeepeeBox for Android: the platform layer of the emulator.
#
#              Built inside PeepeeBox's own CMake tree: on Android,
#              src/CMakeLists.txt makes the emulator a shared library and adds
#              this directory in place of the Qt front end; the result is
#              libPeepeeBox.so.  android/scripts/build.sh sets it all up.
#

# An object library: its functions and the core call each other both ways.
add_library(android_plat OBJECT
    android_main.cpp
    android_plat.c
    android_plat_unix.c
    android_ui.c
    android_dynld.c
    android_globals.c
    android_joystick.c
    ${CMAKE_SOURCE_DIR}/src/unix/unix_netsocket.c
    ${CMAKE_SOURCE_DIR}/src/unix/dummy_cdrom_ioctl.c
    ${CMAKE_SOURCE_DIR}/src/unix/dummy_floppy_ioctl.c
)

target_link_libraries(86Box android_plat android log ${CMAKE_DL_LIBS})
