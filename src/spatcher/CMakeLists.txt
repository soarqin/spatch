if(WIN32)
    set(RES_FILES spatcher.rc spatcher.ico)
endif()
add_executable(spatcher spatcher.c
    selector.c selector.h
    ${RES_FILES})
target_link_libraries(spatcher xdelta3_dec lzma_dec common)
set_target_properties(spatcher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
