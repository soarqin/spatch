#include "xdelta3.h"

#include "LzmaEnc.h"

#include "vfs.h"
#include "memstream.h"

#include <stdlib.h>
#include <stdint.h>

static void *SzAlloc(ISzAllocPtr p, size_t size) { (void*)p; return malloc(size); }
static void SzFree(ISzAllocPtr p, void *address) { (void*)p; free(address); }

enum {
    DIFF_TYPE_CHANGE = 0,
    DIFF_TYPE_CHANGE_LZMA = 1,
    DIFF_TYPE_ADD_OR_REPLACE = 2,
    DIFF_TYPE_ADD_OR_REPLACE_LZMA = 3,
};

typedef struct seq_in_file_s {
    ISeqInStream stream;
    struct vfs_file_handle *fin;
} seq_in_file_t;

typedef struct seq_in_stream_s {
    ISeqInStream stream;
    memstream_t *stm;
} seq_in_stream_t;

typedef struct seq_out_file_s {
    ISeqOutStream stream;
    struct vfs_file_handle *fout;
} seq_out_file_t;

static SRes file_read(const ISeqInStream *p, void *buf, size_t *size) {
    const seq_in_file_t *stm = (const seq_in_file_t*)p;
    int64_t bytes = vfs.read(stm->fin, buf, *size);
    *size = bytes > 0 ? bytes : 0;
    return SZ_OK;
}

static SRes stream_read(const ISeqInStream *p, void *buf, size_t *size) {
    const seq_in_stream_t *stm = (const seq_in_stream_t*)p;
    if (!stm || !stm->stm) { return SZ_ERROR_DATA; }
    *size = memstream_read(stm->stm, buf, *size);
    return SZ_OK;
}

static size_t stream_write(const ISeqOutStream *p, const void *buf, size_t size) {
    const seq_out_file_t *stm = (const seq_out_file_t*)p;
    return vfs.write(stm->fout, buf, size);
}

static int do_stream_compress(ISeqInStream *stm_in, size_t input_size, seq_out_file_t *stm_out) {
    size_t comp_size;
    uint64_t file_offset, file_offset2;
    int i;
    SRes res;
    uint8_t header[LZMA_PROPS_SIZE + 8] = {};
    size_t header_size = LZMA_PROPS_SIZE;

    CLzmaEncHandle enc;
    CLzmaEncProps props;
    ISzAlloc my_alloc = { SzAlloc, SzFree };

    enc = LzmaEnc_Create(&my_alloc);
    LzmaEncProps_Init(&props);
    props.level = 9;
    props.fb = 256;
    props.lc = 4;
    props.lp = 2;
    props.pb = 2;
    LzmaEnc_SetProps(enc, &props);

    res = LzmaEnc_WriteProperties(enc, header + sizeof(uint32_t) * 2, &header_size);
    if (res != SZ_OK) {
        return -res;
    }

    *(uint32_t*)&header[sizeof(uint32_t)] = input_size;
    file_offset = vfs.tell(stm_out->fout);
    vfs.write(stm_out->fout, header, header_size + sizeof(uint32_t) * 2);
    res = LzmaEnc_Encode(enc, &stm_out->stream, stm_in,
                         NULL, &my_alloc, &my_alloc);
    file_offset2 = vfs.tell(stm_out->fout);
    vfs.seek(stm_out->fout, file_offset, VFS_SEEK_POSITION_START);
    comp_size = file_offset2 - file_offset - sizeof(uint32_t);
    vfs.write(stm_out->fout, &comp_size, sizeof(uint32_t));
    vfs.seek(stm_out->fout, file_offset2, VFS_SEEK_POSITION_START);
    LzmaEnc_Destroy(enc, &my_alloc, &my_alloc);
    fprintf(stdout, "Compressed size:  %lu\n", comp_size);
    return -res;
}

static int make_diff(const char *relpath,
                     struct vfs_file_handle *source_file,
                     struct vfs_file_handle *input_file,
                     struct vfs_file_handle *output_file,
                     int compress) {
    int ret = 0;
    uint8_t *src = NULL, *inp = NULL;
    size_t src_size = 0, inp_size = 0;
    usize_t n = 0, ipos = 0;
    xd3_source source = {};
    xd3_stream stream = {};
    xd3_config config = {};

    src_size = vfs.size(source_file);
    src = malloc(src_size);
    if (!src) {
        ret = -1;
        fprintf(stderr, "Out of memory!\n");
        goto end;
    }
    vfs.seek(source_file, 0, VFS_SEEK_POSITION_START);
    vfs.read(source_file, src, src_size);

    inp_size = vfs.size(input_file);
    inp = malloc(inp_size);
    if (!inp) {
        ret = -1;
        fprintf(stderr, "Out of memory!\n");
        goto end;
    }
    vfs.seek(input_file, 0, VFS_SEEK_POSITION_START);
    vfs.read(input_file, inp, inp_size);

    xd3_init_config(&config, 0);
    config.winsize = inp_size;
    ret = xd3_config_stream(&stream, &config);
    if (ret != 0) {
        fprintf(stderr, "Error create stream!\n");
        return -1;
    }
    source.blksize  = src_size;
    source.onblk    = src_size;
    source.curblk   = src;
    source.curblkno = 0;
    source.max_winsize = src_size;
    ret = xd3_set_source_and_size(&stream, &source, src_size);
    if (ret != 0) {
        fprintf(stderr, "Error set source and size!\n");
        goto end;
    }

    fprintf(stdout, "Source file size: %lu\n", src_size);
    fprintf(stdout, "Dest file size:   %lu\n", inp_size);
    n = xd3_min(stream.winsize, inp_size);
    stream.flags |= XD3_FLUSH;
    xd3_avail_input(&stream, inp + ipos, n);
    ipos += n;
    memstream_t *stm = memstream_create();
    while(1) {
        ret = xd3_encode_input(&stream);
        switch (ret) {
        case XD3_INPUT:
            n = xd3_min(stream.winsize, inp_size - ipos);
            if (n == 0) { ret = 0; goto end; }
            xd3_avail_input(&stream, inp + ipos, n);
            ipos += n;
            break;
        case XD3_OUTPUT:
            memstream_write(stm, stream.next_out, stream.avail_out);
            xd3_consume_output(&stream);
            break;
        case XD3_GOTHEADER:
        case XD3_WINSTART:
        case XD3_WINFINISH:
            /* no action necessary */
            break;
        default:
            fprintf(stderr, "Error encode stream: %d\n", ret);
            goto end;
        }
    }

end:
    xd3_close_stream(&stream);
    if (src) free(src);
    if (inp) free(inp);

    if (ret != 0) { return ret; }

    {
        uint16_t namelen = strlen(relpath);
        vfs.write(output_file, &namelen, 2);
        vfs.write(output_file, relpath, namelen);
    }
    fprintf(stdout, "Patch file size:  %lu\n", memstream_size(stm));
    if (compress) {
        seq_in_stream_t stm_in;
        seq_out_file_t stm_out;

        uint32_t size = memstream_size(stm);
        uint8_t type = 1;
        vfs.write(output_file, &type, 1);

        stm_in.stream.Read = stream_read;
        stm_in.stm = stm;
        stm_out.stream.Write = stream_write;
        stm_out.fout = output_file;
        do_stream_compress(&stm_in.stream, size, &stm_out);
    } else {
        uint32_t size = memstream_size(stm);

        uint8_t type = 0;
        vfs.write(output_file, &type, 1);
        vfs.write(output_file, &size, sizeof(uint32_t));
        while (1) {
            uint8_t buf[256 * 1024];
            size_t rd = memstream_read(stm, buf, 256 * 1024);
            vfs.write(output_file, buf, rd);
            if (rd < 256 * 1024) {
                break;
            }
        }
    }
    memstream_destroy(stm);

    return 0;
}

int make_add_file(const char *relpath,
                  struct vfs_file_handle *input_file,
                  struct vfs_file_handle *output_file,
                  int compress) {
    {
        uint16_t namelen = strlen(relpath);
        vfs.write(output_file, &namelen, 2);
        vfs.write(output_file, relpath, namelen);
    }
    if (compress) {
        seq_in_file_t stm_in;
        seq_out_file_t stm_out;

        uint32_t size = vfs.size(input_file);
        uint8_t type = 3;
        vfs.write(output_file, &type, 1);

        stm_in.stream.Read = file_read;
        stm_in.fin = input_file;
        stm_out.stream.Write = stream_write;
        stm_out.fout = output_file;
        do_stream_compress(&stm_in.stream, size, &stm_out);
    } else {
        uint32_t size = vfs.size(input_file);

        uint8_t type = 2;
        vfs.write(output_file, &type, 1);
        vfs.write(output_file, &size, sizeof(uint32_t));
        while (1) {
            uint8_t buf[256 * 1024];
            int64_t rd = vfs.read(input_file, buf, 256 * 1024);
            if (rd > 0) {
                vfs.write(output_file, buf, rd);
            }
            if (rd < 256 * 1024) {
                break;
            }
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    struct vfs_file_handle *source_file = NULL, *input_file = NULL, *output_file = NULL;
    int ret;
    source_file = (argv[1][0] == '-' && argv[1][1] == 0) ? NULL : vfs.open(argv[1], VFS_FILE_ACCESS_READ, 0);
    input_file = vfs.open(argv[2], VFS_FILE_ACCESS_READ, 0);
    if (!input_file) {
        fprintf(stderr, "Unable to read from input file!\n");
        goto end;
    }
    output_file = vfs.open(argv[3], VFS_FILE_ACCESS_WRITE, 0);
    if (!output_file) {
        fprintf(stderr, "Unable to write output file!\n");
        goto end;
    }
    if (source_file) {
        ret = make_diff(argv[1], source_file, input_file, output_file, argc > 4 && argv[4][0] != '0');
    } else {
        ret = make_add_file(argv[2], input_file, output_file, argc > 4 && argv[4][0] != '0');
    }

end:
    if (output_file) vfs.close(output_file);
    if (input_file) vfs.close(input_file);
    if (source_file) vfs.close(source_file);

    return ret;
}
