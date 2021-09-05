#include "xdelta3.h"
#include "LzmaDec.h"

#include "util.h"
#include "vfs.h"

#include <stdint.h>

static void *SzAlloc(ISzAllocPtr p, size_t size) { (void*)p; return malloc(size); }
static void SzFree(ISzAllocPtr p, void *address) { (void*)p; free(address); }

static int sp_getblk(xd3_stream *stream, xd3_source *source, xoff_t blkno) {
    uint8_t *blkdata = *(uint8_t**)stream->opaque;
    int64_t bytes;
    if (!blkdata) {
        *(void**)stream->opaque = malloc(source->blksize);
        blkdata = *(uint8_t**)stream->opaque;
    }
    vfs.seek(source->ioh, source->blksize * blkno, VFS_SEEK_POSITION_START);
    bytes = vfs.read(source->ioh, blkdata, source->blksize);
    source->curblkno = blkno;
    source->onblk = bytes;
    source->curblk = blkdata;
    return 0;
}

static int do_patch(struct vfs_file_handle *input_file, const char *src_path, const char *output_path, int is_dir) {
    int ret = 0;
    void *data = NULL;
    void *blkdata = NULL;
    size_t data_size = 0;
    uint8_t *inp = NULL;
    size_t src_size = 0, inp_size = 0;
    usize_t n = 0, ipos = 0;
    xd3_source source = {};
    xd3_stream stream = {};
    xd3_config config = {};
    struct vfs_file_handle *fsrc = NULL, *fout = NULL;
    uint16_t namelen = 0;
    uint8_t type = 0;
    char name[1024];
    vfs.read(input_file, &namelen, 2);
    vfs.read(input_file, name, namelen);
    name[namelen] = 0;
    fsrc = vfs.open(src_path ? src_path : name, VFS_FILE_ACCESS_READ, 0);
    if (!fsrc) {
        fprintf(stderr, "Unable to open source file!\n");
        goto end;
    }
    if (is_dir) {
        char path[1024];
        vfs.mkdir(output_path);
        snprintf(path, 1024, "%s/%s", output_path, name);
        fout = vfs.open(path, VFS_FILE_ACCESS_WRITE, 0);
    } else {
        fout = vfs.open(output_path, VFS_FILE_ACCESS_WRITE, 0);
    }
    vfs.read(input_file, &type, 1);
    vfs.read(input_file, &inp_size, sizeof(uint32_t));
    inp = malloc(inp_size);
    if (!inp) {
        fprintf(stderr, "Out of memory!\n");
        goto end;
    }
    vfs.read(input_file, inp, inp_size);

    src_size = vfs.size(fsrc);

    xd3_init_config(&config, 0);
    config.winsize = 256 * 1024;
    config.getblk = sp_getblk;
    config.opaque = &blkdata;
    ret = xd3_config_stream(&stream, &config);
    if (ret != 0) {
        fprintf(stderr, "Error create stream!\n");
        return -1;
    }
    source.blksize  = 256 * 1024;
    source.ioh = fsrc;
    ret = xd3_set_source(&stream, &source);
    if (ret != 0) {
        fprintf(stderr, "Error set source and size!\n");
        goto end;
    }

    fprintf(stdout, "Source file size: %lu\n", src_size);
    fprintf(stdout, "Input file size:  %lu\n", inp_size);

    if (type == 1) {
        ELzmaStatus status;
        ISzAlloc my_alloc = { SzAlloc, SzFree };
        size_t orig_size = *(uint32_t*)(inp);
        size_t comp_size = inp_size - LZMA_PROPS_SIZE - sizeof(uint32_t);
        uint8_t *dest = malloc(orig_size);
        if (LzmaDecode(dest, &orig_size,
                       inp + LZMA_PROPS_SIZE + sizeof(uint32_t), &comp_size,
                       inp + sizeof(uint32_t), LZMA_PROPS_SIZE,
                       LZMA_FINISH_END, &status, &my_alloc) != SZ_OK) {
            free(dest);
            fprintf(stderr, "Error decompress patch data!\n");
            goto end;
        }
        free(inp);
        inp = dest;
        inp_size = orig_size;
    }
    ipos = 0;
    n = xd3_min(stream.winsize, inp_size - ipos);
    stream.flags |= XD3_FLUSH;
    xd3_avail_input(&stream, inp + ipos, n);
    ipos += n;

    data_size = stream.winsize;
    data = malloc(data_size);
    while(1) {
        ret = xd3_decode_input(&stream);
        switch (ret) {
        case XD3_INPUT: {
            n = xd3_min(stream.winsize, inp_size - ipos);
            if (n == 0) { ret = 0; goto end; }
            xd3_avail_input(&stream, inp + ipos, n);
            ipos += n;
            break;
        }
        case XD3_OUTPUT:
            vfs.write(fout, stream.next_out, stream.avail_out);
            xd3_consume_output(&stream);
            break;
        case XD3_GOTHEADER:
        case XD3_WINSTART:
        case XD3_WINFINISH:
            /* no action necessary */
            break;
        default:
            fprintf(stderr, "Error decode stream: %d\n", ret);
            goto end;
        }
    }

end:
    xd3_close_stream(&stream);
    if (fout) vfs.close(fout);
    if (fsrc) vfs.close(fsrc);
    if (inp) free(inp);
    if (blkdata) free(blkdata);
    if (data) free(data);

    return ret;
}

int main(int argc, char *argv[]) {
    const char *src_path = NULL, *input_path = NULL, *output_path = NULL;
    int is_dir = 0;
    struct vfs_file_handle *input_file = NULL;
    int ret;
    if (argc > 3) {
        src_path = argv[1];
        input_path = argv[2];
        output_path = argv[3];
    } else {
        input_path = argv[1];
        output_path = argv[2];
        is_dir = 1;
    }
    input_file = vfs.open(input_path, VFS_FILE_ACCESS_READ, 0);
    if (!input_file) {
        fprintf(stderr, "Unable to open input file!\n");
        goto end;
    }
    ret = do_patch(input_file, src_path, output_path, is_dir);

end:
    if (input_file) vfs.close(input_file);

    return ret;
}
