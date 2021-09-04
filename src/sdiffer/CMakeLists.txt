add_executable(sdiffer sdiffer.c)
target_link_libraries(sdiffer xdelta3 lzma_enc common)
set_target_properties(sdiffer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
