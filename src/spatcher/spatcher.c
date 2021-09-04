#include "xdelta3.h"
#include "LzmaDec.h"

#include "util.h"

#include <stdint.h>

static uint8_t *blkdata = NULL;

static void *SzAlloc(ISzAllocPtr p, size_t size) { (void*)p; return malloc(size); }
static void SzFree(ISzAllocPtr p, void *address) { (void*)p; free(address); }

static int sp_getblk(xd3_stream *stream, xd3_source *source, xoff_t blkno) {
    if (!blkdata) {
        blkdata = malloc(source->blksize);
    }
    fseek(source->ioh, source->blksize * blkno, SEEK_SET);
    source->curblkno = blkno;
    source->onblk = fread(blkdata, 1, source->blksize, source->ioh);
    source->curblk = blkdata;
    return 0;
}

int main(int argc, char *argv[]) {
    int ret = 0;
    void *data = NULL;
    size_t data_size = 0;
    uint8_t *inp = NULL;
    size_t src_size = 0, inp_size = 0;
    usize_t n = 0, ipos = 0;
    xd3_source source = {};
    xd3_stream stream = {};
    xd3_config config = {};
    FILE *fout = NULL, *fsrc = NULL;
    fsrc = fopen(argv[1], "rb");
    if (!fsrc) {
        fprintf(stderr, "Unable to open source file!\n");
        goto end;
    }
    inp = read_whole_file(argv[2], &inp_size);
    if (!inp) {
        fprintf(stderr, "Unable to open input file!\n");
        goto end;
    }
    fout = fopen(argv[3], "wb");
    if (!fout) {
        fprintf(stderr, "Unable to write output file!\n");
        goto end;
    }
    fseek(fsrc, 0, SEEK_END);
    src_size = ftello64(fsrc);
    fseek(fsrc, 0, SEEK_SET);

    xd3_init_config(&config, 0);
    config.winsize = 256 * 1024;
    config.getblk = sp_getblk;
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

    if (inp[0] == 1) {
        ELzmaStatus status;
        ISzAlloc my_alloc = { SzAlloc, SzFree };
        size_t comp_size = *(uint32_t*)(inp + 1);
        size_t orig_size = *(uint32_t*)(inp + 1 + sizeof(uint32_t));
        uint8_t *dest = malloc(orig_size);
        if (LzmaDecode(dest, &orig_size,
                       inp + 1 + LZMA_PROPS_SIZE + sizeof(uint32_t) * 2, &comp_size,
                       inp + 1 + sizeof(uint32_t) * 2, LZMA_PROPS_SIZE,
                       LZMA_FINISH_END, &status, &my_alloc) != SZ_OK) {
            free(dest);
            fprintf(stderr, "Error decompress patch data!\n");
            goto end;
        }
        free(inp);
        inp = dest;
        inp_size = orig_size;
        ipos = 0;
    } else {
        ipos = 1;
    }
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
            fwrite(stream.next_out, 1, stream.avail_out, fout);
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
    if (fout) fclose(fout);
    if (fsrc) fclose(fsrc);
    if (inp) free(inp);
    if (blkdata) free(blkdata);
    if (data) free(data);

    return ret;
}
