/*
 * Created by Soar on 2021/9/9.
 * Copyright (c) 2021 Northen Kingdom, X.D. Network. All rights reserved.
 */

#include "patch.h"
#include "selector.h"
#include "vfs.h"
#include "util.h"

#include "gui_win32.h"

#if defined(_WIN32)
#include <windows.h>
#endif
#include <locale.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    run_gui();
    int ret = 0;
    char browsed_selected_path[1024];
    uint32_t patch_offset = 0;
    uint64_t tag = 0;
    struct vfs_file_handle *input_file = NULL;
    int64_t bytes_left = 0;
    WCHAR currpath[MAX_PATH];
    char currpatha[MAX_PATH * 3 + 1];
    setlocale(LC_NUMERIC, "");
    GetModuleFileNameW(NULL, currpath, MAX_PATH);
    util_ucs_to_utf8(currpath, currpatha, MAX_PATH * 3 + 1);
    input_file = vfs.open(currpatha, VFS_FILE_ACCESS_READ, 0);
    vfs.seek(input_file, -(int)(sizeof(uint32_t) + sizeof(uint64_t)), VFS_SEEK_POSITION_END);
    vfs.read(input_file, &patch_offset, sizeof(uint32_t));
    vfs.read(input_file, &tag, sizeof(uint64_t));
    if (tag == 0xBADC0DEDEADBEEFULL) {
        bytes_left = vfs.size(input_file) - patch_offset - sizeof(uint32_t) - sizeof(uint64_t);
        if (browse_for_directory(NULL, browsed_selected_path, 1024) != 0) {
            ret = -1;
            goto end;
        }
        vfs.seek(input_file, patch_offset, VFS_SEEK_POSITION_START);
        ret = do_multi_patch(NULL, input_file, bytes_left, browsed_selected_path);
    }

end:
    if (input_file) vfs.close(input_file);

    return ret;
}
