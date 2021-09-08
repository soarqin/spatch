add_library(nuklear INTERFACE)
target_sources(nuklear INTERFACE nuklear.h nuklear_gdip.h)
target_include_directories(nuklear INTERFACE .)
target_link_libraries(nuklear INTERFACE gdiplus shlwapi)
