add_library(common STATIC
    util.c util.h
    memstream.c memstream.h)
target_include_directories(common PUBLIC .)
