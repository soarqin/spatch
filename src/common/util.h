#pragma once

#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32)
extern int util_ucs_to_utf8(const wchar_t *strw, char *str, size_t size);
extern int util_utf8_to_ucs(const char *str, wchar_t *strw, size_t size);

extern int util_mkdir_unicode(const wchar_t *wpath, int recursive);
#endif

extern int util_mkdir(const char *path, int recursive);
extern int util_file_exists(const char *path);
