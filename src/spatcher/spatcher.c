#include "patch.h"

#include "selector.h"

#include "util.h"
#include <stdio.h>
#include <locale.h>

int main(int argc, char *argv[]) {
    const char *src_path = NULL, *input_path = NULL, *output_path = NULL;
#if defined(_WIN32)
    char browsed_selected_path[1024];
#endif
    int is_dir = 0;
    struct vfs_file_handle *input_file = NULL;
    int ret;
    int64_t bytes_left = 0;
    setlocale(LC_NUMERIC, "");
    switch (argc) {
    case 2:
#if defined(_WIN32)
    {
        if (browse_for_directory(NULL, browsed_selected_path, 1024) != 0) {
            ret = -1;
            goto end;
        }
        output_path = browsed_selected_path;
        input_path = argv[1];
        is_dir = 1;
        break;
    }
#endif
    case 0:
    case 1:
        fprintf(stdout, "Usage: spatcher [source dir/file] <patch file> <target_dir>\n");
        ret = -1;
        goto end;
    case 3: {
        input_path = argv[1];
        output_path = argv[2];
        is_dir = 1;
        break;
    }
    default:
        {
            src_path = argv[1];
            if ((src_path[0] != '-' || src_path[1] != 0) && vfs.stat(src_path, NULL) & VFS_STAT_IS_DIRECTORY) {
                is_dir = 1;
            }
            input_path = argv[2];
            output_path = argv[3];
            break;
        }
    }
    input_file = vfs.open(input_path, VFS_FILE_ACCESS_READ, 0);
    if (!input_file) {
        fprintf(stderr, "Unable to open input file!\n");
        goto end;
    }
    bytes_left = vfs.size(input_file);
    if (is_dir) {
        ret = do_multi_patch(src_path, input_file, bytes_left, output_path);
    } else {
        ret = do_single_patch(input_file, src_path, output_path, is_dir);
    }

end:
    if (input_file) vfs.close(input_file);

    return ret;
}
