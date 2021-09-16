if(WIN32)
    FILE(GLOB PNG_FILES icons/*.png)
    set(RES_FILES spatcher.rc ${PNG_FILES})
endif()
add_executable(spatcher
    patch.c patch.h
    spatcher.c)
add_executable(spatcher_header_win32 WIN32
    patch.c patch.h
    gui_win32.c gui_win32.h
    spatcher_header_win32.c
    whereami.c whereami.h
    ${RES_FILES})
target_link_libraries(spatcher xdelta3_dec lzma_dec common)
target_link_libraries(spatcher_header_win32 xdelta3_dec lzma_dec common nuklear)
set_target_properties(spatcher spatcher_header_win32 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
