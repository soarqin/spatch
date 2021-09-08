#pragma once

#include "vfs.h"
#include <stdint.h>

int do_single_patch(struct vfs_file_handle *input_file, const char *src_path, const char *output_path, int is_dir);
int do_multi_patch(const char *src_path, struct vfs_file_handle *input_file, int64_t bytes_left, const char *output_path);
