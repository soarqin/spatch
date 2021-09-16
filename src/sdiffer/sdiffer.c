#define UNICODE
#define _UNICODE

#include "xdelta3.h"

#include "LzmaEnc.h"

#include "patch_config.h"
#include "vfs.h"
#include "util.h"
#include "memstream.h"
#include "ini.h"

#include <stdlib.h>
#include <stdint.h>
#include <locale.h>

static void *SzAlloc(ISzAllocPtr p, size_t size) { (void*)p; return malloc(size); }
static void SzFree(ISzAllocPtr p, void *address) { (void*)p; free(address); }

enum {
    DIFF_TYPE_CHANGE = 0,
    DIFF_TYPE_CHANGE_LZMA = 1,
    DIFF_TYPE_ADD_OR_REPLACE = 2,
    DIFF_TYPE_ADD_OR_REPLACE_LZMA = 3,
    DIFF_TYPE_DELETE = 4,
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

typedef struct compress_progress_s {
    ICompressProgress progress;
    uint64_t total;
} compress_progress_t;

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

static SRes compress_progress_callback(const ICompressProgress *p, UInt64 inSize, UInt64 outSize) {
    compress_progress_t *progress = (compress_progress_t*)p;
    fprintf(stdout, "\r    Compressing: %'llu/%'llu(%u%%)   to: %'llu", inSize, progress->total, (uint32_t)(inSize * 100ULL / progress->total), outSize);
    return SZ_OK;
}

static int do_stream_compress(ISeqInStream *stm_in, size_t input_size, seq_out_file_t *stm_out) {
    size_t comp_size;
    uint64_t file_offset, file_offset2;
    int i;
    SRes res;
    uint8_t header[LZMA_PROPS_SIZE + 8] = {0};
    size_t header_size = LZMA_PROPS_SIZE;
    compress_progress_t progress;

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
    props.writeEndMark = 1;
    LzmaEnc_SetProps(enc, &props);

    res = LzmaEnc_WriteProperties(enc, header + sizeof(uint32_t) * 2, &header_size);
    if (res != SZ_OK) {
        return -res;
    }

    *(uint32_t*)&header[sizeof(uint32_t)] = input_size;
    file_offset = vfs.tell(stm_out->fout);
    vfs.write(stm_out->fout, header, header_size + sizeof(uint32_t) * 2);
    progress.total = input_size;
    progress.progress.Progress = compress_progress_callback;
    res = LzmaEnc_Encode(enc, &stm_out->stream, stm_in,
                         &progress.progress, &my_alloc, &my_alloc);
    file_offset2 = vfs.tell(stm_out->fout);
    vfs.seek(stm_out->fout, file_offset, VFS_SEEK_POSITION_START);
    comp_size = file_offset2 - file_offset - sizeof(uint32_t);
    vfs.write(stm_out->fout, &comp_size, sizeof(uint32_t));
    vfs.seek(stm_out->fout, file_offset2, VFS_SEEK_POSITION_START);
    LzmaEnc_Destroy(enc, &my_alloc, &my_alloc);
    fprintf(stdout, "\r    Compressing: %'llu/%'llu(100%%)   to: %'lu\n", progress.total, progress.total, comp_size);
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
    xd3_source source = {0};
    xd3_stream stream = {0};
    xd3_config config = {0};

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

    fprintf(stdout, "  Source file path: %s\n", vfs.get_path(source_file));
    fprintf(stdout, "  Input file path:  %s\n", vfs.get_path(input_file));
    fprintf(stdout, "  Source file size: %'lu\n", src_size);
    fprintf(stdout, "  Input file size:  %'lu\n", inp_size);
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
    fprintf(stdout, "  Patch data size:  %'lu\n", memstream_size(stm));
    if (compress) {
        seq_in_stream_t stm_in;
        seq_out_file_t stm_out;

        uint32_t size = memstream_size(stm);
        uint8_t type = DIFF_TYPE_CHANGE_LZMA;
        vfs.write(output_file, &type, 1);

        stm_in.stream.Read = stream_read;
        stm_in.stm = stm;
        stm_out.stream.Write = stream_write;
        stm_out.fout = output_file;
        do_stream_compress(&stm_in.stream, size, &stm_out);
    } else {
        uint32_t size = memstream_size(stm);

        uint8_t type = DIFF_TYPE_CHANGE;
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
    fprintf(stdout, "  Add file path:    %s\n", vfs.get_path(input_file));
    {
        uint16_t namelen = strlen(relpath);
        vfs.write(output_file, &namelen, 2);
        vfs.write(output_file, relpath, namelen);
    }
    if (compress) {
        seq_in_file_t stm_in;
        seq_out_file_t stm_out;

        uint32_t size = vfs.size(input_file);
        uint8_t type = DIFF_TYPE_ADD_OR_REPLACE_LZMA;
        vfs.write(output_file, &type, 1);

        stm_in.stream.Read = file_read;
        stm_in.fin = input_file;
        stm_out.stream.Write = stream_write;
        stm_out.fout = output_file;
        do_stream_compress(&stm_in.stream, size, &stm_out);
    } else {
        uint32_t size = vfs.size(input_file);

        uint8_t type = DIFF_TYPE_ADD_OR_REPLACE;
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

int make_dir_diff(const char *relpath, const char *source_dir, const char *input_dir, struct vfs_file_handle *output_file, int compress) {
    int ret;
    struct vfs_dir_handle *inp_dir = vfs.opendir(input_dir, false);
    if (!inp_dir) {
        return 0;
    }
    while (vfs.readdir(inp_dir)) {
        char path[1024], source_path[1024], input_path[1024];
        const char *dir_name = vfs.dirent_get_name(inp_dir);
        if (dir_name[0] == '.') {
            continue;
        }
        if (vfs.dirent_is_dir(inp_dir)) {
            if (relpath[0] == 0) {
                snprintf(path, 1024, "%s", dir_name);
            } else {
                snprintf(path, 1024, "%s/%s", relpath, dir_name);
            }
            if (source_dir[0] == 0) {
                snprintf(source_path, 1024, "%s", dir_name);
            } else {
                snprintf(source_path, 1024, "%s/%s", source_dir, dir_name);
            }
            if (input_dir[0] == 0) {
                snprintf(input_path, 1024, "%s", dir_name);
            } else {
                snprintf(input_path, 1024, "%s/%s", input_dir, dir_name);
            }
            ret = make_dir_diff(path, source_path, input_path, output_file, compress);
            if (ret != 0) {
                return ret;
            }
        } else {
            struct vfs_file_handle *fsrc, *finp;
            if (relpath[0] == 0) {
                snprintf(path, 1024, "%s", dir_name);
            } else {
                snprintf(path, 1024, "%s/%s", relpath, dir_name);
            }
            if (source_dir[0] == 0) {
                snprintf(source_path, 1024, "%s", dir_name);
            } else {
                snprintf(source_path, 1024, "%s/%s", source_dir, dir_name);
            }
            if (input_dir[0] == 0) {
                snprintf(input_path, 1024, "%s", dir_name);
            } else {
                snprintf(input_path, 1024, "%s/%s", input_dir, dir_name);
            }
            finp = vfs.open(input_path, VFS_FILE_ACCESS_READ, 0);
            if (!finp) {
                return -1;
            }
            fsrc = vfs.open(source_path, VFS_FILE_ACCESS_READ, 0);
            if (fsrc) {
                ret = make_diff(path, fsrc, finp, output_file, compress);
                vfs.close(fsrc);
            } else {
                ret = make_add_file(path, finp, output_file, compress);
            }
            vfs.close(finp);
            if (ret != 0) {
                return ret;
            }
        }
    }
    return 0;
}

int make_dir_deletes(const char *relpath, const char *source_dir, const char *input_dir, struct vfs_file_handle *output_file) {
    int ret = 0;
    struct vfs_dir_handle *src_dir = vfs.opendir(source_dir, false);
    if (!src_dir) {
        return 0;
    }
    while (vfs.readdir(src_dir)) {
        char path[1024], source_path[1024], input_path[1024];
        const char *dir_name = vfs.dirent_get_name(src_dir);
        if (dir_name[0] == '.') {
            continue;
        }
        if (vfs.dirent_is_dir(src_dir)) {
            if (relpath[0] == 0) {
                snprintf(path, 1024, "%s", dir_name);
            } else {
                snprintf(path, 1024, "%s/%s", relpath, dir_name);
            }
            if (source_dir[0] == 0) {
                snprintf(source_path, 1024, "%s", dir_name);
            } else {
                snprintf(source_path, 1024, "%s/%s", source_dir, dir_name);
            }
            if (input_dir[0] == 0) {
                snprintf(input_path, 1024, "%s", dir_name);
            } else {
                snprintf(input_path, 1024, "%s/%s", input_dir, dir_name);
            }
            ret = make_dir_deletes(path, source_path, input_path, output_file);
            if (ret != 0) {
                return ret;
            }
        } else {
            struct vfs_file_handle *fsrc, *finp;
            if (input_dir[0] == 0) {
                snprintf(input_path, 1024, "%s", dir_name);
            } else {
                snprintf(input_path, 1024, "%s/%s", input_dir, dir_name);
            }
            if (util_file_exists(input_path)) {
                continue;
            }
            if (relpath[0] == 0) {
                snprintf(path, 1024, "%s", dir_name);
            } else {
                snprintf(path, 1024, "%s/%s", relpath, dir_name);
            }
            fprintf(stdout, "  Delete file:  %s\n", path);
            {
                uint16_t namelen = strlen(path);
                uint8_t type = DIFF_TYPE_DELETE;
                vfs.write(output_file, &namelen, 2);
                vfs.write(output_file, path, namelen);
                vfs.write(output_file, &type, 1);
            }
        }
    }
}

struct config {
    char source_path[512];
    char input_path[512];
    char output_path[512];
    char icon_file[512];
    int compress;
};

int sdiffer_ini_handler(void* user, const char* section,
                    const char* name, const char* value) {
    struct config *config = user;
    if (!strcmp(section, "compare")) {
        if (!strcmp(name, "from")) {
            snprintf(config->source_path, 512, "%s", value);
        } else if (!strcmp(name, "to")) {
            snprintf(config->input_path, 512, "%s", value);
        }
    } else if (!strcmp(section, "output")) {
        if (!strcmp(name, "path")) {
            snprintf(config->output_path, 512, "%s", value);
#if defined(_WIN32)
        } else if (!strcmp(name, "icon")) {
            snprintf(config->icon_file, 512, "%s", value);
#endif
        } else if (!strcmp(name, "compress")) {
            config->compress = strcmp(value, "0") != 0 && strcmp(value, "false") != 0;
        }
    }
    return 1;
}

#if defined(_WIN32)

struct ICONDIRENTRY {
    BYTE        bWidth;          // Width, in pixels, of the image
    BYTE        bHeight;         // Height, in pixels, of the image
    BYTE        bColorCount;     // Number of colors in image (0 if >=8bpp)
    BYTE        bReserved;       // Reserved ( must be 0)
    WORD        wPlanes;         // Color Planes
    WORD        wBitCount;       // Bits per pixel
    DWORD       dwBytesInRes;    // How many bytes in this resource?
    DWORD       dwImageOffset;   // Where in the file is this image?
};

struct ICONDIR {
    WORD           idReserved;   // Reserved (must be 0)
    WORD           idType;       // Resource Type (1 for icons)
    WORD           idCount;      // How many images?
    // ICONDIRENTRY   idEntries[1]; // An entry for each image (idCount of 'em)
};

struct MEMICONDIRENTRY {
    BYTE        bWidth;          // Width, in pixels, of the image
    BYTE        bHeight;         // Height, in pixels, of the image
    BYTE        bColorCount;     // Number of colors in image (0 if >=8bpp)
    BYTE        bReserved;       // Reserved ( must be 0)
    WORD        wPlanes;         // Color Planes
    WORD        wBitCount;       // Bits per pixel
    DWORD       dwBytesInRes;    // How many bytes in this resource?
    WORD        nID;             // The ID.
};

struct MEMICONDIR {
    WORD           idReserved;   // Reserved (must be 0)
    WORD           idType;       // Resource Type (1 for icons)
    WORD           idCount;      // How many images?
    // MEMICONDIRENTRY   idEntries[1]; // An entry for each image (idCount of 'em)
};

int setIconByData(LPCWSTR exe_filename, void *iconData) {
    HANDLE fileHandle = BeginUpdateResourceW(exe_filename, FALSE);
    struct ICONDIR* icondir;
    int langId;
    size_t entriesSize;
    size_t memicondirBytes;
    struct MEMICONDIR *memicondir;
    struct MEMICONDIRENTRY *entries;
    int resourceId = 1;
    char *cursor;
    if(!fileHandle) {
        short errorCode = GetLastError();
        fprintf(stdout, "Error: BeginUpdateResource failed with error code %i (0x%x).\n", errorCode, errorCode);
        return -1;
    }
    icondir = iconData;
    langId = MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT);

    entriesSize = sizeof(struct MEMICONDIRENTRY) * icondir->idCount;
    memicondirBytes = sizeof(struct MEMICONDIR) + entriesSize;
    memicondir = (struct MEMICONDIR*)malloc(memicondirBytes);
    memset(memicondir, 0, memicondirBytes);

    entries = (struct MEMICONDIRENTRY*)(((char*)memicondir) + sizeof(struct MEMICONDIR));
    uint8_t *entriesU8 = (uint8_t*)entries;

    memicondir->idType = 1;
    memicondir->idCount = icondir->idCount;

    cursor = iconData + 6;
    for(int i = 0; i < icondir->idCount; i++) {
        struct MEMICONDIRENTRY *destEntry;
        struct ICONDIRENTRY* srcEntry = (struct ICONDIRENTRY*)cursor;
        cursor += 16;
        destEntry = (struct MEMICONDIRENTRY*)(entriesU8 + 14 * i);
        destEntry->bWidth        = srcEntry->bWidth;
        destEntry->bHeight       = srcEntry->bHeight;
        destEntry->bColorCount   = srcEntry->bColorCount;
        destEntry->bReserved     = srcEntry->bReserved;
        destEntry->wPlanes       = srcEntry->wPlanes;
        destEntry->wBitCount     = srcEntry->wBitCount;
        destEntry->dwBytesInRes  = srcEntry->dwBytesInRes;
        destEntry->nID           = (unsigned short)(resourceId + i);

        if(!UpdateResourceW(fileHandle, RT_ICON, MAKEINTRESOURCEW(destEntry->nID), langId, iconData + srcEntry->dwImageOffset, destEntry->dwBytesInRes)) {
            short errorCode = GetLastError();
            fprintf(stderr, "Error: UpdateResource failed with error code %i (0x%x).\n", errorCode, errorCode);
            return -1;
        }
    }

    if(UpdateResourceW(fileHandle, RT_GROUP_ICON, MAKEINTRESOURCE(resourceId), langId, memicondir, (6 + entriesSize))) {
        EndUpdateResourceW(fileHandle, FALSE);
        free(memicondir);
        return 0;
    }

    EndUpdateResourceW(fileHandle, TRUE);
    free(memicondir);
    return -1;
}

int setIconByFilename(LPCWSTR exeFilename, const char* iconFilename) {
    char* iconData;
    struct vfs_file_handle* file = vfs.open(iconFilename, VFS_FILE_ACCESS_READ, 0);
    int size;
    int result;
    if(!file) {
        return -1;
    }
    size = vfs.size(file);
    iconData = malloc(sizeof(char) * size);
    vfs.read(file, iconData, size);
    vfs.close(file);
    result = setIconByData(exeFilename, iconData);
    free(iconData);
    return result;
}

#endif

int main(int argc, char *argv[]) {
    struct vfs_file_handle *source_file = NULL, *input_file = NULL, *output_file = NULL;
    int ret = -1;
    int64_t org_tail_offset = 0;
    struct config config = {{0}};
    setlocale(LC_NUMERIC, "");
    ini_parse(argc > 1 ? argv[1] : "sdiffer.ini", sdiffer_ini_handler, &config);
#if defined(_WIN32)
    util_copy_file("spatcher_header_win32.exe", config.output_path);
    {
        WCHAR outpath[MAX_PATH];
        HANDLE hUpdate;
        util_utf8_to_ucs(config.output_path, outpath, MAX_PATH);
        setIconByFilename(outpath, config.icon_file);
    }
    output_file = vfs.open(config.output_path, VFS_FILE_ACCESS_WRITE | VFS_FILE_ACCESS_UPDATE_EXISTING, 0);
#else
    output_file = vfs.open(config.output_path, VFS_FILE_ACCESS_WRITE, 0);
#endif
    if (!output_file) {
        fprintf(stderr, "Unable to write output file!\n");
        goto end;
    }
#if defined(_WIN32)
    vfs.seek(output_file, 0, VFS_SEEK_POSITION_END);
#endif
    org_tail_offset = vfs.tell(output_file);
    if (!strcmp(config.source_path, "-") || vfs.stat(config.source_path, NULL) & VFS_STAT_IS_DIRECTORY) {
        if (!(vfs.stat(config.input_path, NULL) & VFS_STAT_IS_DIRECTORY)) {
            fprintf(stderr, "Path of `to` is not a directory!\n");
            return -1;
        }
        ret = make_dir_diff("", config.source_path, config.input_path, output_file, config.compress);
        if (ret == 0) {
            ret = make_dir_deletes("", config.source_path, config.input_path, output_file);
        }
        goto end;
    }
    source_file = !strcmp(config.source_path, "-") ? NULL : vfs.open(config.source_path, VFS_FILE_ACCESS_READ, 0);
    input_file = vfs.open(config.input_path, VFS_FILE_ACCESS_READ, 0);
    if (!input_file) {
        fprintf(stderr, "Unable to read from input file!\n");
        goto end;
    }
    if (source_file) {
        ret = make_diff(config.source_path, source_file, input_file, output_file, config.compress);
    } else {
        ret = make_add_file(config.input_path, input_file, output_file, config.compress);
    }

end:
    if (ret == 0) {
        uint64_t tag = 0xBADC0DEDEADBEEFULL;
        int64_t org_config_offset = vfs.tell(output_file);
        patch_config_t patch_config = { SPATCH_FORMAT_VERSION };
        vfs.write(output_file, &patch_config, sizeof(patch_config_t));
        vfs.write(output_file, &org_tail_offset, sizeof(int64_t));
        vfs.write(output_file, &org_config_offset, sizeof(int64_t));
        vfs.write(output_file, &tag, sizeof(uint64_t));
    }
    if (output_file) vfs.close(output_file);
    if (input_file) vfs.close(input_file);
    if (source_file) vfs.close(source_file);

    return ret;
}
