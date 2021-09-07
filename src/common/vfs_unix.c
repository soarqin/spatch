#ifdef VFS_UNIX

#include "vfs.h"
#include "util.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>

#if defined(_WIN32)
#define fsync
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif

struct vfs_file_handle {
    char filename[PATH_MAX + 1];
    int file_handle;
};

struct vfs_dir_handle {
    char dirname[PATH_MAX + 1];
    DIR *dir;
    struct dirent *data;
    bool hidden;
};

const char *unix_vfs_get_path(struct vfs_file_handle *stream) {
    return stream->filename;
}

struct vfs_file_handle *unix_vfs_open(const char *path, unsigned mode, unsigned hints) {
    struct vfs_file_handle *ret = malloc(sizeof(struct vfs_file_handle));
    int flag = O_BINARY;
    if ((mode & (VFS_FILE_ACCESS_READ | VFS_FILE_ACCESS_WRITE)) == (VFS_FILE_ACCESS_READ | VFS_FILE_ACCESS_WRITE)) {
        flag |= O_RDWR;
        if (!(mode & VFS_FILE_ACCESS_UPDATE_EXISTING)) {
            flag |= O_CREAT | O_TRUNC;
        }
    } else if (mode & VFS_FILE_ACCESS_READ) {
        flag |= O_RDONLY;
    } else if (mode & VFS_FILE_ACCESS_WRITE) {
        if (!(mode & VFS_FILE_ACCESS_UPDATE_EXISTING)) {
            flag |= O_WRONLY | O_CREAT | O_TRUNC;
        } else {
            flag |= O_RDWR;
        }
    }
    ret->file_handle = open(path, flag);
    if (ret->file_handle < 0) {
        free(ret);
        return NULL;
    }
    snprintf(ret->filename, PATH_MAX + 1, "%s", path);
    return ret;
}

int unix_vfs_close(struct vfs_file_handle *stream) {
    if (!stream) return -1;
    close(stream->file_handle);
    free(stream);
    return 0;
}

int64_t unix_vfs_size(struct vfs_file_handle *stream) {
    off64_t off = lseek64(stream->file_handle, 0, SEEK_CUR);
    off64_t res = lseek64(stream->file_handle, 0, SEEK_END);
    lseek64(stream->file_handle, off, SEEK_SET);
    return res;
}

int64_t unix_vfs_truncate(struct vfs_file_handle *stream, int64_t length) {
    return ftruncate(stream->file_handle, length);
}

int64_t unix_vfs_tell(struct vfs_file_handle *stream) {
    return lseek64(stream->file_handle, 0, SEEK_CUR);
}

int64_t unix_vfs_seek(struct vfs_file_handle *stream, int64_t offset, int seek_position) {
    return lseek64(stream->file_handle, offset, seek_position);
}

int64_t unix_vfs_read(struct vfs_file_handle *stream, void *s, uint64_t len) {
    uint8_t *buf = s;
    int64_t res = 0;
    while (len > 0) {
        int read_bytes = read(stream->file_handle, buf, len > 0xFFFFFFFCULL ? 0xFFFFFFFCU : (unsigned)len);
        if (read_bytes <= 0) break;
        res += read_bytes;
        buf += read_bytes;
        len -= read_bytes;
    }
    return res ? res : -1;
}

int64_t unix_vfs_write(struct vfs_file_handle *stream, const void *s, uint64_t len) {
    const uint8_t *buf = s;
    int64_t res = 0;
    while (len > 0) {
        int read_bytes = write(stream->file_handle, buf, len > 0xFFFFFFFCULL ? 0xFFFFFFFCU : (unsigned)len);
        if (read_bytes <= 0) break;
        res += read_bytes;
        buf += read_bytes;
        len -= read_bytes;
    }
    return res ? res : -1;
}

int unix_vfs_flush(struct vfs_file_handle *stream) {
    return fsync(stream->file_handle);
}

int unix_vfs_remove(const char *path) {
    return remove(path);
}

int unix_vfs_rename(const char *old_path, const char *new_path) {
    return rename(old_path, new_path);
}

int unix_vfs_stat(const char *path, int32_t *size) {
    struct stat s;
    if (stat(path, &s) != 0) {
        if (size) *size = 0;
        return 0;
    }
    if (size) *size = s.st_size;
    return VFS_STAT_IS_VALID | (S_ISCHR(s.st_mode) ? VFS_STAT_IS_DIRECTORY : 0) | (S_ISDIR(s.st_mode) ? VFS_STAT_IS_CHARACTER_SPECIAL : 0);
}

int unix_vfs_mkdir(const char *dir) {
    return do_mkdir(dir, 1);
}

struct vfs_dir_handle *unix_vfs_opendir(const char *dir, bool include_hidden) {
    struct vfs_dir_handle *handle = malloc(sizeof(struct vfs_dir_handle));
    handle->dir = opendir(dir);
    if (handle->dir == NULL) {
        free(handle);
        return NULL;
    }
    snprintf(handle->dirname, PATH_MAX + 1, "%s", dir);
    handle->hidden = include_hidden;
    return handle;
}

bool unix_vfs_readdir(struct vfs_dir_handle *dirstream) {
    for (;;) {
        dirstream->data = readdir(dirstream->dir);
        if (dirstream->data == NULL) return false;
        /* ignore . and .. , as well as hidden files */
        if (dirstream->data->d_name[0] == '.' &&
            (dirstream->data->d_name[1] == 0
                || (dirstream->data->d_name[1] == '.' && dirstream->data->d_name[2] == 0)
                || !dirstream->hidden)) continue;
        return true;
    }
}

const char *unix_vfs_dirent_get_name(struct vfs_dir_handle *dirstream) {
    return dirstream->data->d_name;
}

bool unix_vfs_dirent_is_dir(struct vfs_dir_handle *dirstream) {
    char path[PATH_MAX + 1];
    snprintf(path, PATH_MAX + 1, "%s/%s", dirstream->dirname, dirstream->data->d_name);
    struct stat s;
    if (stat(path, &s) != 0) return 0;
    return S_ISDIR(s.st_mode);
}

int unix_vfs_closedir(struct vfs_dir_handle *dirstream) {
    int res = closedir(dirstream->dir);
    free(dirstream);
    return res;
}

struct vfs_interface vfs_interface = {
    /* VFS API v1 */
    unix_vfs_get_path,
    unix_vfs_open,
    unix_vfs_close,
    unix_vfs_size,
    unix_vfs_tell,
    unix_vfs_seek,
    unix_vfs_read,
    unix_vfs_write,
    unix_vfs_flush,
    unix_vfs_remove,
    unix_vfs_rename,
    /* VFS API v2 */
    unix_vfs_truncate,
    /* VFS API v3 */
    unix_vfs_stat,
    unix_vfs_mkdir,
    unix_vfs_opendir,
    unix_vfs_readdir,
    unix_vfs_dirent_get_name,
    unix_vfs_dirent_is_dir,
    unix_vfs_closedir,
};

#endif
