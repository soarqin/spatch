#include "util.h"

#include "vfs.h"

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

#if defined(_WIN32)
int util_ucs_to_utf8(const wchar_t *strw, char *str, size_t size) {
    return WideCharToMultiByte(CP_UTF8, 0, strw, -1, str, size, NULL, 0) > 0;
}

int util_utf8_to_ucs(const char *str, wchar_t *strw, size_t size) {
    return MultiByteToWideChar(CP_UTF8, 0, str, -1, strw, size) > 0;
}

int util_mkdir_unicode(const wchar_t *wpath, int recursive) {
    if (recursive) {
        wchar_t n[MAX_PATH];
        lstrcpyW(n, wpath);
        PathRemoveFileSpecW(n);
        if (n[0] != 0 && !PathIsDirectoryW(n)) {
            int ret = util_mkdir_unicode(n, recursive);
            if (ret != 0) return ret;
        }
    }
    if (CreateDirectoryW(wpath, NULL)) return 0;
    if (GetLastError() == ERROR_ALREADY_EXISTS) return -2;
    return -1;
}
#endif

int util_mkdir(const char *path, int recursive) {
#if defined(VFS_WIN32)
    wchar_t wpath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH);
    return util_mkdir_unicode(wpath, recursive);
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

int util_file_exists(const char *path) {
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

extern int util_copy_file(const char *source, const char *target) {
    int ret = 0;
    struct vfs_file_handle *src_file = vfs.open(source, VFS_FILE_ACCESS_READ, 0);
    struct vfs_file_handle *tgt_file;
    if (!src_file) { return -1; }
    tgt_file = vfs.open(target, VFS_FILE_ACCESS_WRITE, 0);
    if (!tgt_file) { vfs.close(src_file); return -1; }
    while (1) {
        char buf[256 * 1024];
        int64_t bytes = vfs.read(src_file, buf, 256 * 1024);
        if (bytes <= 0) {
            break;
        }
        if (vfs.write(tgt_file, buf, bytes) < bytes) {
            ret = -1;
            break;
        }
        if (bytes < 256 * 1024) {
            break;
        }
    }
    vfs.close(tgt_file);
    vfs.close(src_file);
    return ret;
}
