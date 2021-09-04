#include "xdelta3.h"

#include "LzmaEnc.h"

#include "util.h"
#include "memstream.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static void *SzAlloc(ISzAllocPtr p, size_t size) { (void*)p; return malloc(size); }
static void SzFree(ISzAllocPtr p, void *address) { (void*)p; free(address); }

typedef struct seq_in_stream_s {
    ISeqInStream stream;
    memstream_t *stm;
} seq_in_stream_t;

typedef struct seq_out_stream_s {
    ISeqOutStream stream;
    FILE *fout;
} seq_out_stream_t;

SRes stream_read(const ISeqInStream *p, void *buf, size_t *size) {
    const seq_in_stream_t *stm = (const seq_in_stream_t*)p;
    if (!stm || !stm->stm) { return SZ_ERROR_DATA; }
    *size = memstream_read(stm->stm, buf, *size);
    return SZ_OK;
}

size_t stream_write(const ISeqOutStream *p, const void *buf, size_t size) {
    const seq_out_stream_t *stm = (const seq_out_stream_t*)p;
    return fwrite(buf, 1, size, stm->fout);
}

int do_stream_compress(memstream_t *input, FILE *fout) {
    size_t comp_size;
    uint64_t file_offset, file_offset2;
    int i;
    SRes res;
    uint8_t header[LZMA_PROPS_SIZE + 8] = {};
    size_t header_size = LZMA_PROPS_SIZE;

    CLzmaEncHandle enc;
    CLzmaEncProps props;
    ISzAlloc my_alloc = { SzAlloc, SzFree };

    seq_in_stream_t stm_in;
    seq_out_stream_t stm_out;
    stm_in.stream.Read = stream_read;
    stm_in.stm = input;
    stm_out.stream.Write = stream_write;
    stm_out.fout = fout;
    enc = LzmaEnc_Create(&my_alloc);
    LzmaEncProps_Init(&props);
    props.level = 9;
    LzmaEncProps_Normalize(&props);
    LzmaEnc_SetProps(enc, &props);

    res = LzmaEnc_WriteProperties(enc, header + sizeof(uint32_t) * 2, &header_size);
    if (res != SZ_OK) {
        return -res;
    }
    *(uint32_t*)&header[sizeof(uint32_t)] = memstream_size(input);
    file_offset = ftello64(fout);
    stream_write(&stm_out.stream, header, header_size + sizeof(uint32_t) * 2);
    res = LzmaEnc_Encode(enc, &stm_out.stream, &stm_in.stream,
                         NULL, &my_alloc, &my_alloc);
    file_offset2 = ftello64(fout);
    fseek(fout, file_offset, SEEK_SET);
    comp_size = file_offset2 - file_offset - header_size - sizeof(uint32_t) * 2;
    fwrite(&comp_size, 1, sizeof(uint32_t), fout);
    fseek(fout, file_offset2, SEEK_SET);
    LzmaEnc_Destroy(enc, &my_alloc, &my_alloc);
    comp_size += header_size + sizeof(uint32_t) * 2;
    fprintf(stdout, "Compressed size:  %lu\n", comp_size);
    return -res;
}

int main(int argc, char *argv[]) {
    int ret = 0;
    uint8_t *src = NULL, *inp = NULL;
    size_t src_size = 0, inp_size = 0;
    usize_t n = 0, ipos = 0;
    xd3_source source = {};
    xd3_stream stream = {};
    xd3_config config = {};
    FILE *fout = NULL;
    src = read_whole_file(argv[1], &src_size);
    if (!src) {
        fprintf(stderr, "Unable to read from source file!\n");
        goto end;
    }
    inp = read_whole_file(argv[2], &inp_size);
    if (!inp) {
        fprintf(stderr, "Unable to read from input file!\n");
        goto end;
    }
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

    fout = fopen(argv[3], "wb");
    fprintf(stdout, "Patch file size:  %lu\n", memstream_size(stm));
    if (argc > 4 && argv[4][0] == '1') {
        uint8_t type = 1;
        fwrite(&type, 1, 1, fout);
        do_stream_compress(stm, fout);
    } else {
        uint8_t type = 0;
        fwrite(&type, 1, 1, fout);
        while (1) {
            uint8_t buf[256 * 1024];
            size_t rd = memstream_read(stm, buf, 256 * 1024);
            fwrite(buf, 1, rd, fout);
            if (rd < 256 * 1024) {
                break;
            }
        }
    }
    fclose(fout);
    memstream_destroy(stm);

    return 0;
}
