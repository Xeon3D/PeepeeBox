#
# PeepeeBox    A hard fork of 86Box for the funworld Photo Play cabinets.
#
#              ppfix -- repair a Photo Play 2.0 image's CopyControl layout.
#
#              Standalone on purpose: it rewrites directory entries, FAT entries
#              and cluster slack, which is not something the emulator should do
#              behind the user's back at every boot.  It lands next to the
#              emulator so the warning PeepeeBox shows can point at it.
#

if(NOT WIN32)
    # The tool is a Win32 window.  The repair itself is portable, but nothing
    # outside Windows has anywhere to run PeepeeBox's cabinets from.
    return()
endif()

add_executable(ppfix WIN32 ppfix.c ppfix.rc)

target_link_libraries(ppfix PRIVATE comdlg32 comctl32)

set_target_properties(ppfix PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src")

# ppfix.rc pulls in ppfix.manifest and ppfix.ico from beside it.
set_source_files_properties(ppfix.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ppfix.manifest;${CMAKE_CURRENT_SOURCE_DIR}/ppfix.ico")

# It ships with the emulator: the warning PeepeeBox shows for an unrepaired 2.0
# image tells the user to run the tool sitting next to it, so a release archive
# without ppfix.exe in it is a release that cannot follow its own instructions.
install(TARGETS ppfix DESTINATION ".")
