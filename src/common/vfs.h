/* VFS definitions from libretro-common(https://github.com/libretro/libretro-common) */

#pragma once

#include <stdint.h>
#ifndef __cplusplus
#if defined(_MSC_VER) && _MSC_VER < 1800 && !defined(SN_TARGET_PS3)
/* Hack applied for MSVC when compiling in C89 mode
 * as it isn't C99-compliant. */
#define bool unsigned char
#define true 1
#define false 0
#else
#include <stdbool.h>
#endif
#endif

/* VFS functionality */

/* File paths:
 * File paths passed as parameters when using this API shall be well formed UNIX-style,
 * using "/" (unquoted forward slash) as directory separator regardless of the platform's native separator.
 * Paths shall also include at least one forward slash ("game.bin" is an invalid path, use "./game.bin" instead).
 * Other than the directory separator, cores shall not make assumptions about path format:
 * "C:/path/game.bin", "http://example.com/game.bin", "#game/game.bin", "./game.bin" (without quotes) are all valid paths.
 * Cores may replace the basename or remove path components from the end, and/or add new components;
 * however, cores shall not append "./", "../" or multiple consecutive forward slashes ("//") to paths they request to front end.
 * The frontend is encouraged to make such paths work as well as it can, but is allowed to give up if the core alters paths too much.
 * Frontends are encouraged, but not required, to support native file system paths (modulo replacing the directory separator, if applicable).
 * Cores are allowed to try using them, but must remain functional if the front rejects such requests.
 * Cores are encouraged to use the libretro-common filestream functions for file I/O,
 * as they seamlessly integrate with VFS, deal with directory separator replacement as appropriate
 * and provide platform-specific fallbacks in cases where front ends do not support VFS. */

/* Opaque file handle
 * Introduced in VFS API v1 */
struct vfs_file_handle;

/* Opaque directory handle
 * Introduced in VFS API v3 */
struct vfs_dir_handle;

/* File open flags
 * Introduced in VFS API v1 */
#define VFS_FILE_ACCESS_READ            (1 << 0) /* Read only mode */
#define VFS_FILE_ACCESS_WRITE           (1 << 1) /* Write only mode, discard contents and overwrites existing file unless VFS_FILE_ACCESS_UPDATE is also specified */
#define VFS_FILE_ACCESS_READ_WRITE      (VFS_FILE_ACCESS_READ | VFS_FILE_ACCESS_WRITE) /* Read-write mode, discard contents and overwrites existing file unless VFS_FILE_ACCESS_UPDATE is also specified*/
#define VFS_FILE_ACCESS_UPDATE_EXISTING (1 << 2) /* Prevents discarding content of existing files opened for writing */

/* These are only hints. The frontend may choose to ignore them. Other than RAM/CPU/etc use,
   and how they react to unlikely external interference (for example someone else writing to that file,
   or the file's server going down), behavior will not change. */
#define VFS_FILE_ACCESS_HINT_NONE              (0)
/* Indicate that the file will be accessed many times. The frontend should aggressively cache everything. */
#define VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS   (1 << 0)

/* Seek positions */
#define VFS_SEEK_POSITION_START    0
#define VFS_SEEK_POSITION_CURRENT  1
#define VFS_SEEK_POSITION_END      2

/* stat() result flags
 * Introduced in VFS API v3 */
#define VFS_STAT_IS_VALID               (1 << 0)
#define VFS_STAT_IS_DIRECTORY           (1 << 1)
#define VFS_STAT_IS_CHARACTER_SPECIAL   (1 << 2)

/* Get path from opaque handle. Returns the exact same path passed to file_open when getting the handle
 * Introduced in VFS API v1 */
typedef const char *(*vfs_get_path_t)(struct vfs_file_handle *stream);

/* Open a file for reading or writing. If path points to a directory, this will
 * fail. Returns the opaque file handle, or NULL for error.
 * Introduced in VFS API v1 */
typedef struct vfs_file_handle *(*vfs_open_t)(const char *path, unsigned mode, unsigned hints);

/* Close the file and release its resources. Must be called if open_file returns non-NULL. Returns 0 on success, -1 on failure.
 * Whether the call succeeds ot not, the handle passed as parameter becomes invalid and should no longer be used.
 * Introduced in VFS API v1 */
typedef int (*vfs_close_t)(struct vfs_file_handle *stream);

/* Return the size of the file in bytes, or -1 for error.
 * Introduced in VFS API v1 */
typedef int64_t (*vfs_size_t)(struct vfs_file_handle *stream);

/* Truncate file to specified size. Returns 0 on success or -1 on error
 * Introduced in VFS API v2 */
typedef int64_t (*vfs_truncate_t)(struct vfs_file_handle *stream, int64_t length);

/* Get the current read / write position for the file. Returns -1 for error.
 * Introduced in VFS API v1 */
typedef int64_t (*vfs_tell_t)(struct vfs_file_handle *stream);

/* Set the current read/write position for the file. Returns the new position, -1 for error.
 * Introduced in VFS API v1 */
typedef int64_t (*vfs_seek_t)(struct vfs_file_handle *stream, int64_t offset, int seek_position);

/* Read data from a file. Returns the number of bytes read, or -1 for error.
 * Introduced in VFS API v1 */
typedef int64_t (*vfs_read_t)(struct vfs_file_handle *stream, void *s, uint64_t len);

/* Write data to a file. Returns the number of bytes written, or -1 for error.
 * Introduced in VFS API v1 */
typedef int64_t (*vfs_write_t)(struct vfs_file_handle *stream, const void *s, uint64_t len);

/* Flush pending writes to file, if using buffered IO. Returns 0 on sucess, or -1 on failure.
 * Introduced in VFS API v1 */
typedef int (*vfs_flush_t)(struct vfs_file_handle *stream);

/* Delete the specified file. Returns 0 on success, -1 on failure
 * Introduced in VFS API v1 */
typedef int (*vfs_remove_t)(const char *path);

/* Rename the specified file. Returns 0 on success, -1 on failure
 * Introduced in VFS API v1 */
typedef int (*vfs_rename_t)(const char *old_path, const char *new_path);

/* Stat the specified file. Retruns a bitmask of VFS_STAT_* flags, none are set if path was not valid.
 * Additionally stores file size in given variable, unless NULL is given.
 * Introduced in VFS API v3 */
typedef int (*vfs_stat_t)(const char *path, int32_t *size);

/* Create the specified directory. Returns 0 on success, -1 on unknown failure, -2 if already exists.
 * Introduced in VFS API v3 */
typedef int (*vfs_mkdir_t)(const char *dir);

/* Open the specified directory for listing. Returns the opaque dir handle, or NULL for error.
 * Support for the include_hidden argument may vary depending on the platform.
 * Introduced in VFS API v3 */
typedef struct vfs_dir_handle *(*vfs_opendir_t)(const char *dir, bool include_hidden);

/* Read the directory entry at the current position, and move the read pointer to the next position.
 * Returns true on success, false if already on the last entry.
 * Introduced in VFS API v3 */
typedef bool (*vfs_readdir_t)(struct vfs_dir_handle *dirstream);

/* Get the name of the last entry read. Returns a string on success, or NULL for error.
 * The returned string pointer is valid until the next call to readdir or closedir.
 * Introduced in VFS API v3 */
typedef const char *(*vfs_dirent_get_name_t)(struct vfs_dir_handle *dirstream);

/* Check if the last entry read was a directory. Returns true if it was, false otherwise (or on error).
 * Introduced in VFS API v3 */
typedef bool (*vfs_dirent_is_dir_t)(struct vfs_dir_handle *dirstream);

/* Close the directory and release its resources. Must be called if opendir returns non-NULL. Returns 0 on success, -1 on failure.
 * Whether the call succeeds ot not, the handle passed as parameter becomes invalid and should no longer be used.
 * Introduced in VFS API v3 */
typedef int (*vfs_closedir_t)(struct vfs_dir_handle *dirstream);

struct vfs_interface
{
    /* VFS API v1 */
    vfs_get_path_t get_path;
    vfs_open_t open;
    vfs_close_t close;
    vfs_size_t size;
    vfs_tell_t tell;
    vfs_seek_t seek;
    vfs_read_t read;
    vfs_write_t write;
    vfs_flush_t flush;
    vfs_remove_t remove;
    vfs_rename_t rename;
    /* VFS API v2 */
    vfs_truncate_t truncate;
    /* VFS API v3 */
    vfs_stat_t stat;
    vfs_mkdir_t mkdir;
    vfs_opendir_t opendir;
    vfs_readdir_t readdir;
    vfs_dirent_get_name_t dirent_get_name;
    vfs_dirent_is_dir_t dirent_is_dir;
    vfs_closedir_t closedir;
};

extern struct vfs_interface vfs;
