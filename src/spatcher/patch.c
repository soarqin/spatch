#include "patch.h"

#include "xdelta3.h"
#include "LzmaDec.h"

#include "vfs.h"

#include <stdint.h>

static void *cb_opaque = NULL;
static info_callback_t info_cb = NULL;
static progress_callback_t progress_cb = NULL;
static message_callback_t message_cb = NULL;

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

void set_callback_opaque(void *opaque) {
    cb_opaque = opaque;
}

void set_info_callback(info_callback_t cb) {
    info_cb = cb;
}

void set_progress_callback(progress_callback_t cb) {
    progress_cb = cb;
}

void set_message_callback(message_callback_t cb) {
    message_cb = cb;
}

int do_single_patch(struct vfs_file_handle *input_file, const char *src_path, const char *output_path, int is_dir) {
    int ret = -1;
    void *data = NULL;
    void *blkdata = NULL;
    size_t data_size = 0;
    uint8_t *inp = NULL;
    size_t src_size = 0, inp_size = 0;
    usize_t n = 0, ipos = 0;
    xd3_source source = {0};
    xd3_stream stream = {0};
    xd3_config config = {0};
    struct vfs_file_handle *fsrc = NULL, *fout = NULL;
    uint16_t namelen = 0;
    uint8_t type = 0;
    int64_t total;
    char name[1024];
    char bakpath[1024] = {0};
    char outpath[1024] = {0};
    if (vfs.read(input_file, &namelen, 2) < 2) {
        ret = -2;
        goto end;
    }
    if (vfs.read(input_file, name, namelen) < namelen) {
        ret = -2;
        goto end;
    }
    name[namelen] = 0;
    if (vfs.read(input_file, &type, 1) < 1) {
        ret = -2;
        goto end;
    }
    if (type < 2) {
        if (is_dir) {
            if (src_path && src_path[0] != 0) {
                snprintf(outpath, 1024, "%s/%s", src_path, name);
                fsrc = vfs.open(outpath, VFS_FILE_ACCESS_READ, 0);
            } else {
                int i;
                snprintf(outpath, 1024, "%s/%s", output_path, name);
                for (i = 0; i < 999; ++i) {
                    snprintf(bakpath, 1024, "%s/%s.sbk.%d", output_path, name, i);
                    if (vfs.rename(outpath, bakpath) == 0) {
                        break;
                    }
                }
                fsrc = vfs.open(bakpath, VFS_FILE_ACCESS_READ, 0);
            }
        } else {
            fsrc = vfs.open(src_path ? src_path : name, VFS_FILE_ACCESS_READ, 0);
        }
        if (!fsrc) {
            if (message_cb) message_cb(cb_opaque, -1, "Unable to open source file!");
            goto end;
        }
    }
    if (is_dir) {
        char path[1024], *rslash
#if defined(_WIN32)
        , *rslash2
#endif
        ;
        vfs.mkdir(output_path);
        snprintf(path, 1024, "%s/%s", output_path, name);
        if (type == 4) {
            if (info_cb) info_cb(cb_opaque, path, 0, 4);
            // fprintf(stdout, "Delete file: %s\n", path);
            ret = vfs.remove(path);
            goto end;
        }
        // fprintf(stdout, "Target file path: %s\n", path);
        rslash = strrchr(path, '/');
#if defined(_WIN32)
        rslash2 = strrchr(path, '\\');
        if (rslash < rslash2) rslash = rslash2;
#endif
        if (rslash) {
            *rslash = 0;
            vfs.mkdir(path);
            *rslash = '/';
        }
        fout = vfs.open(path, VFS_FILE_ACCESS_WRITE, 0);
    } else {
        char *rslash
#if defined(_WIN32)
        , *rslash2
#endif
        ;
        if (type == 4) {
            if (info_cb) info_cb(cb_opaque, output_path, 0, 4);
            // fprintf(stdout, "Delete file: %s\n", output_path);
            ret = vfs.remove(output_path);
            goto end;
        }
        // fprintf(stdout, "Target file path: %s\n", output_path);
        rslash = strrchr(output_path, '/');
#if defined(_WIN32)
        rslash2 = strrchr(output_path, '\\');
        if (rslash < rslash2) rslash = rslash2;
#endif
        if (rslash) {
            *rslash = 0;
            vfs.mkdir(output_path);
            *rslash = '/';
        }
        fout = vfs.open(output_path, VFS_FILE_ACCESS_WRITE, 0);
    }
    if (!fout) {
        if (message_cb) message_cb(cb_opaque, -1, "Unable to write output file!");
        ret = -1;
        goto end;
    }
    if (vfs.read(input_file, &inp_size, sizeof(uint32_t)) < sizeof(uint32_t)) {
        ret = -2;
        goto end;
    }
    if (type == 2 || type == 3) {
        if (type == 2) {
            int64_t left = inp_size;
            uint8_t buf[256 * 1024];
            if (info_cb) info_cb(cb_opaque, vfs.get_path(fout), inp_size, 2);
            if (progress_cb) progress_cb(cb_opaque, 0);
            while (left > 0) {
                int64_t bytes = vfs.read(input_file, buf, left < 256 * 1024 ? left : 256 * 1024);
                if (bytes <= 0) {
                    break;
                }
                vfs.write(fout, buf, bytes);
                left -= bytes;
                if (progress_cb) progress_cb(cb_opaque, inp_size - left);
            }
            if (progress_cb) progress_cb(cb_opaque, -1);
        } else {
            CLzmaDec dec;
            uint8_t props[LZMA_PROPS_SIZE];
            ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
            ISzAlloc my_alloc = { SzAlloc, SzFree };
            uint8_t buf[256 * 1024], buf_out[256 * 1024];
            uint32_t output_size;
            int64_t left = inp_size;
            total = 0;
            vfs.read(input_file, &output_size, sizeof(uint32_t));
            if (info_cb) info_cb(cb_opaque, vfs.get_path(fout), output_size, 3);
            if (progress_cb) progress_cb(cb_opaque, 0);
            // fprintf(stdout, "Original size: %'u\n", output_size);
            vfs.read(input_file, props, LZMA_PROPS_SIZE);
            left -= LZMA_PROPS_SIZE + sizeof(uint32_t);
            LzmaDec_Construct(&dec);
            LzmaDec_Allocate(&dec, props, LZMA_PROPS_SIZE, &my_alloc);
            LzmaDec_Init(&dec);
            while (left > 0) {
                int64_t offset;
                int64_t bytes = vfs.read(input_file, buf, left < 256 * 1024 ? left : 256 * 1024);
                if (bytes <= 0) {
                    break;
                }
                offset = 0;
                while (offset < bytes) {
                    SizeT sz_input = bytes - offset;
                    SizeT sz_output = 256 * 1024;
                    ret = -LzmaDec_DecodeToBuf(&dec, buf_out, &sz_output, buf + offset, &sz_input, LZMA_FINISH_ANY, &status);
                    if (ret != SZ_OK) {
                        LzmaDec_Free(&dec, &my_alloc);
                        goto end;
                    }
                    offset += sz_input;
                    if (sz_output == 0) {
                        break;
                    }
                    vfs.write(fout, buf_out, sz_output);
                    total += sz_output;
                    if (progress_cb) progress_cb(cb_opaque, total);
                }
                left -= bytes;
            }
            if (status != LZMA_STATUS_FINISHED_WITH_MARK) {
                SizeT sz_input = 0;
                SizeT sz_output = 256 * 1024;
                ret = -LzmaDec_DecodeToBuf(&dec, buf_out, &sz_output, NULL, &sz_input, LZMA_FINISH_END, &status);
                if (ret == SZ_OK && sz_output != 0) {
                    vfs.write(fout, buf_out, sz_output);
                    total += sz_output;
                    if (progress_cb) progress_cb(cb_opaque, total);
                }
            }
            LzmaDec_Free(&dec, &my_alloc);
            if (progress_cb) progress_cb(cb_opaque, -1);
        }
        ret = 0;
        goto end;
    }
    inp = malloc(inp_size);
    if (!inp) {
        if (message_cb) message_cb(cb_opaque, -1, "Out of memory!");
        goto end;
    }
    if (vfs.read(input_file, inp, inp_size) < inp_size) {
        ret = -2;
        goto end;
    }

    src_size = vfs.size(fsrc);

    xd3_init_config(&config, 0);
    config.winsize = 256 * 1024;
    config.getblk = sp_getblk;
    config.opaque = &blkdata;
    ret = xd3_config_stream(&stream, &config);
    if (ret != 0) {
        if (message_cb) message_cb(cb_opaque, -1, "Error create stream!");
        goto end;
    }
    source.blksize  = 256 * 1024;
    source.ioh = fsrc;
    ret = xd3_set_source(&stream, &source);
    if (ret != 0) {
        if (message_cb) message_cb(cb_opaque, -1, "Error set source and size!");
        goto end;
    }

/*
    fprintf(stdout, "Source file size: %'lu\n", src_size);
    fprintf(stdout, "Input file size:  %'lu\n", inp_size);
*/

    if (type == 1) {
        ELzmaStatus status;
        ISzAlloc my_alloc = { SzAlloc, SzFree };
        size_t orig_size = *(uint32_t*)(inp);
        size_t comp_size = inp_size - LZMA_PROPS_SIZE - sizeof(uint32_t);
        uint8_t *old = inp;
        inp = malloc(orig_size);
        if (!inp) {
            inp = old;
            if (message_cb) message_cb(cb_opaque, -1, "Out of memory!");
            goto end;
        }
        if (LzmaDecode(inp, &orig_size,
                       old + LZMA_PROPS_SIZE + sizeof(uint32_t), &comp_size,
                       old + sizeof(uint32_t), LZMA_PROPS_SIZE,
                       LZMA_FINISH_END, &status, &my_alloc) != SZ_OK) {
            free(inp);
            inp = old;
            if (message_cb) message_cb(cb_opaque, -1, "Error decompress patch data!");
            goto end;
        }
        inp_size = orig_size;
        free(old);
    }
    ipos = 0;
    n = xd3_min(stream.winsize, inp_size - ipos);
    stream.flags |= XD3_FLUSH;
    xd3_avail_input(&stream, inp + ipos, n);
    ipos += n;

    total = 0;
    if (info_cb) info_cb(cb_opaque, vfs.get_path(fout), -1, type);
    if (progress_cb) progress_cb(cb_opaque, 0);
    data_size = stream.winsize;
    data = malloc(data_size);
    while(1) {
        ret = xd3_decode_input(&stream);
        switch (ret) {
        case XD3_INPUT: {
            n = xd3_min(stream.winsize, inp_size - ipos);
            if (n == 0) {
                if (progress_cb) progress_cb(cb_opaque, -1);
                ret = 0; goto end;
            }
            xd3_avail_input(&stream, inp + ipos, n);
            ipos += n;
            break;
        }
        case XD3_OUTPUT:
            vfs.write(fout, stream.next_out, stream.avail_out);
            total += stream.avail_out;
            if (progress_cb) progress_cb(cb_opaque, total);
            xd3_consume_output(&stream);
            break;
        case XD3_GOTHEADER:
        case XD3_WINSTART:
        case XD3_WINFINISH:
            /* no action necessary */
            break;
        default:
            if (message_cb) message_cb(cb_opaque, -1, "Error decode stream: %d", ret);
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
    if (bakpath[0] != 0) {
        if (ret == 0 || outpath[0] == 0) {
            vfs.remove(bakpath);
        } else {
            vfs.remove(outpath);
            vfs.rename(bakpath, outpath);
        }
    }

    return ret;
}

int do_multi_patch(const char *src_path, struct vfs_file_handle *input_file, int64_t bytes_left, const char *output_path) {
    int64_t offset_end = vfs.tell(input_file) + bytes_left;
    while (vfs.tell(input_file) < offset_end) {
        int ret = do_single_patch(input_file, src_path, output_path, 1);
        if (ret != 0) {
            if (ret == -2) {
                break;
            }
            return ret;
        }
    }
    return 0;
}
