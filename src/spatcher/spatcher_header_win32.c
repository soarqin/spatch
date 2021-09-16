/*
 * Created by Soar on 2021/9/9.
 * Copyright (c) 2021 Northen Kingdom, X.D. Network. All rights reserved.
 */

#include "patch.h"
/*
#include "selector.h"
*/
#include "patch_config.h"
#include "vfs.h"
#include "util.h"
#include "whereami.h"

#include "gui_win32.h"

#if defined(_WIN32)
#include <windows.h>
#endif
#include <stdio.h>

struct callback_context {
    struct vfs_file_handle *input_file;
    int64_t bytes_left;
    uint32_t patch_offset;
    int64_t progress;
    int64_t total;
};

void info_cb(void *opaque, const char *filename, int64_t file_size, int diff_type) {
    struct callback_context *ctx = opaque;
    char msg[1024];
    ctx->total = file_size;
    set_apply_patch_progress(ctx->progress, ctx->total);
    snprintf(msg, 1024, "Patching %s...", filename);
    set_apply_patch_message(msg);
}

void progress_cb(void *opaque, int64_t progress) {
    struct callback_context *ctx = opaque;
    ctx->progress = progress;
    set_apply_patch_progress(ctx->progress, ctx->total);
}

void message_cb(void *opaque, int err, const char *msg, ...) {
    char out[1024];
    va_list l;
    va_start(l, msg);
    vsnprintf(out, 1024, msg, l);
    va_end(l);
    set_apply_patch_message(out);
}

void patch_cb(void *opaque, const char *path) {
    struct callback_context *ctx = opaque;
    vfs.seek(ctx->input_file, ctx->patch_offset, VFS_SEEK_POSITION_START);
    set_callback_opaque(ctx);
    set_info_callback(info_cb);
    set_progress_callback(progress_cb);
    set_message_callback(message_cb);
    do_multi_patch(NULL, ctx->input_file, ctx->bytes_left, path);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    int ret = 0;
    char browsed_selected_path[1024];
    int64_t patch_offset = 0, config_offset = 0;
    uint64_t tag = 0;
    struct vfs_file_handle *input_file = NULL;
    char exepath[1024];
    int i, dirname_length;
    struct callback_context ctx;

    wai_getExecutablePath(exepath, 1024, &dirname_length);
    i = dirname_length;
    exepath[i] = '/';
    while (--i >= 0) {
        if (exepath[i] == '\\') exepath[i] = '/';
    }
    input_file = vfs.open(exepath, VFS_FILE_ACCESS_READ, 0);
    vfs.seek(input_file, -(int)(sizeof(int64_t) * 2 + sizeof(uint64_t)), VFS_SEEK_POSITION_END);
    vfs.read(input_file, &patch_offset, sizeof(int64_t));
    vfs.read(input_file, &config_offset, sizeof(int64_t));
    vfs.read(input_file, &tag, sizeof(uint64_t));
    if (tag == 0xBADC0DEDEADBEEFULL) {
        if (config_offset > 0) {
            patch_config_t patch_config;
            vfs.seek(input_file, config_offset, VFS_SEEK_POSITION_START);
            if (vfs.read(input_file, &patch_config, sizeof(patch_config_t)) != sizeof(patch_config_t)
                || patch_config.format_version != SPATCH_FORMAT_VERSION) {
                ret = -1;
                goto end;
            }
        }
        ctx.input_file = input_file;
        ctx.bytes_left = config_offset > 0 ?
            config_offset - patch_offset :
            vfs.size(input_file) - patch_offset - (sizeof(int64_t) * 2 + sizeof(uint64_t));
        ctx.patch_offset = patch_offset;
        ctx.progress = 0;
        ctx.total = 0;
        set_apply_patch_callback(patch_cb, &ctx);
        exepath[dirname_length] = 0;
        run_gui(exepath);
/*
        if (browse_for_directory(NULL, browsed_selected_path, 1024) != 0) {
            ret = -1;
            goto end;
        }
*/
    }

end:
    if (input_file) vfs.close(input_file);

    return ret;
}
