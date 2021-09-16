add_library(common STATIC
    util.c util.h
    memstream.c memstream.h
    vfs_unix.c vfs_win32.c vfs.h
    patch_config.h)
if(WIN32)
    target_compile_definitions(common PRIVATE VFS_WIN32)
    target_link_libraries(common shlwapi)
else()
    target_compile_definitions(common PRIVATE VFS_UNIX)
endif()
target_include_directories(common PUBLIC .)
