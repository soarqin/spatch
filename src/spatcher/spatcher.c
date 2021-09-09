#include "patch.h"

#include "util.h"
#include <stdio.h>
#include <stdarg.h>
#include <locale.h>

static char output_prefix[1024] = {0};
static int64_t total_file_size = -1;

void info_callback(const char *filename, int64_t file_size, int diff_type) {
    const char *diff_type_text[] = {"Patching", "Patching", "Adding  ", "Adding  ", "Deleting"};
    snprintf(output_prefix, 1024, "%s %s", diff_type_text[diff_type], filename);
    total_file_size = file_size;
}

void progress_callback(int64_t progress) {
    if (progress < 0) {
        fprintf(stdout, "\n");
        return;
    }
    if (total_file_size > 0) {
        fprintf(stdout, "\r%s: %llu/%llu", output_prefix, progress, total_file_size);
    } else {
        fprintf(stdout, "\r%s: %llu", output_prefix, progress);
    }
}

void message_callback(int err, const char *msg, ...) {
    va_list l;
    FILE *output = err != 0 ? stderr : stdout;
    va_start(l, msg);
    vfprintf(output, msg, l);
    va_end(l);
    fprintf(output, "\n");
}

int main(int argc, char *argv[]) {
    const char *src_path = NULL, *input_path = NULL, *output_path = NULL;
    int is_dir = 0;
    struct vfs_file_handle *input_file = NULL;
    int ret;
    int64_t bytes_left = 0;
    setlocale(LC_NUMERIC, "");
    switch (argc) {
    case 0:
    case 1:
    case 2:
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
        ret = -1;
        goto end;
    }
    bytes_left = vfs.size(input_file);
    set_info_callback(info_callback);
    set_progress_callback(progress_callback);
    set_message_callback(message_callback);
    if (is_dir) {
        ret = do_multi_patch(src_path, input_file, bytes_left, output_path);
    } else {
        ret = do_single_patch(input_file, src_path, output_path, is_dir);
    }

end:
    if (input_file) vfs.close(input_file);

    return ret;
}
