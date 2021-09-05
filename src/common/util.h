#pragma once

#include <stdint.h>
#include <stddef.h>

#if defined(VFS_WIN32)
int util_mkdir_unicode(const wchar_t *wpath, int recursive);
#endif

int util_mkdir(const char *path, int recursive);
int util_file_exists(const char *path);
