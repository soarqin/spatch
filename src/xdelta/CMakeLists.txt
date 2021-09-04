include(CheckTypeSize)
check_type_size("size_t" SIZEOF_SIZE_T)
check_type_size("unsigned long long" SIZEOF_UNSIGNED_LONG_LONG)
set(EXTRA_DEFS
    static_assert=_Static_assert
    SIZEOF_SIZE_T=${SIZEOF_SIZE_T}
    SIZEOF_UNSIGNED_LONG_LONG=${SIZEOF_UNSIGNED_LONG_LONG}
    WINVER=0x0502)
add_library(xdelta3 STATIC xdelta3.c)
target_compile_definitions(xdelta3 PUBLIC ${EXTRA_DEFS})
target_include_directories(xdelta3 PUBLIC .)
add_library(xdelta3_dec STATIC xdelta3.c)
target_compile_definitions(xdelta3_dec PUBLIC XD3_ENCODER=0 ${EXTRA_DEFS})
target_include_directories(xdelta3_dec PUBLIC .)
