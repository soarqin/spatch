#pragma once

#include "vfs.h"
#include <stdint.h>

enum {
    DIFF_TYPE_CHANGE = 0,
    DIFF_TYPE_CHANGE_LZMA = 1,
    DIFF_TYPE_ADD_OR_REPLACE = 2,
    DIFF_TYPE_ADD_OR_REPLACE_LZMA = 3,
    DIFF_TYPE_DELETE = 4,
};

typedef void (*info_callback_t)(const char *filename, int64_t file_size, int diff_type);
typedef void (*progress_callback_t)(int64_t progress);
typedef void (*message_callback_t)(int err, const char *msg, ...);

extern void set_info_callback(info_callback_t cb);
extern void set_progress_callback(progress_callback_t cb);
extern void set_message_callback(message_callback_t cb);
extern int do_single_patch(struct vfs_file_handle *input_file, const char *src_path, const char *output_path, int is_dir);
extern int do_multi_patch(const char *src_path, struct vfs_file_handle *input_file, int64_t bytes_left, const char *output_path);
