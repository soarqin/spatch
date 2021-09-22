#pragma once

#include <stdint.h>

extern int run_gui(const char *init_path);
extern void set_apply_patch_callback(void (*cb)(void*, const char*), void *opaque);
extern void set_apply_patch_progress(int64_t prog, int64_t total);
extern void set_apply_patch_message(const char *message);
extern void set_apply_patching_file(const char *filename);
