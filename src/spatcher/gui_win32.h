#pragma once

extern int run_gui(const char *init_path);
extern void set_apply_patch_callback(void (*cb)(void*), void *opaque);
