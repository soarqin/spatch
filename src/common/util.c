#include "util.h"

#include "vfs.h"

#include <stdlib.h>

#if defined(VFS_WIN32)
#include <windows.h>
#include <shlwapi.h>
#else
#include <sys/stat.h>
#include <errno.h>
#if defined(_WIN32)
#include <direct.h>
#define mkdir(p, o) _mkdir(p)
#endif
#endif

uint8_t *read_whole_file(const char *filename, size_t *size) {
    static uint8_t empty[1];
    struct vfs_file_handle *handle = vfs.open(filename, VFS_FILE_ACCESS_READ, 0);
    int64_t fsize;
    if (!handle) { *size = 0; return empty; }
    fsize = vfs.size(handle);
    uint8_t *result = malloc(fsize);
    if (!result) { vfs.close(handle); *size = 0; return empty; }
    uint8_t *buffer = result;
    int64_t left = fsize;
    while(left > 0) {
        int rsize = left > (INT_MAX >> 1) ? (INT_MAX >> 1) : left;
        rsize = vfs.read(handle, buffer, rsize);
        if (rsize <= 0) { break; }
        buffer += rsize;
        left -= rsize;
    }
    vfs.close(handle);
    *size = fsize - left;
    return result;
}

#if defined(VFS_WIN32)
static inline int mkdir_unicode(const wchar_t *wpath, int recursive) {
    if (recursive) {
        wchar_t n[MAX_PATH];
        lstrcpyW(n, wpath);
        PathRemoveFileSpecW(n);
        if (n[0] != 0 && !PathIsDirectoryW(n)) {
            int ret = mkdir_unicode(n, recursive);
            if (ret != 0) return ret;
        }
    }
    if (CreateDirectoryW(wpath, NULL)) return 0;
    if (GetLastError() == ERROR_ALREADY_EXISTS) return -2;
    return -1;
}
#endif

int do_mkdir(const char *path, int recursive) {
#if defined(VFS_WIN32)
    wchar_t wpath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH);
    return mkdir_unicode(wpath, recursive);
#endif
#if defined(VFS_UNIX)
    if (recursive) {
        const char *rslash = strrchr(path, '/');
        if (rslash) {
            char parent[4096];
            memcpy(parent, path, rslash - path);
            parent[rslash - path] = 0;
            struct stat s = {};
            if (stat(parent, &s) == -1) {
                int ret = do_mkdir(parent, 1);
                if (ret != 0) return ret;
            } else if (!S_ISDIR(s.st_mode)) {
                return -1;
            }
        }
    }
    if (mkdir(path, 0755) == 0) return 0;
    if (errno == EEXIST) return -2;
    return -1;
#endif
}

int file_exists(const char *path) {
#if defined(VFS_WIN32)
    wchar_t wpath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH);
    DWORD dwAttrib = GetFileAttributesW(wpath);
    return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
        !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
#endif
#if defined(VFS_UNIX)
    return access(path, F_OK) != -1;
#endif
}
