add_executable(spatcher spatcher.c)
target_link_libraries(spatcher xdelta3_dec lzma_dec common)
set_target_properties(spatcher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
