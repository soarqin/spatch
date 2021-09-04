#include <stdio.h>
#include <stdlib.h>

#include "util.h"

uint8_t *read_whole_file(const char *filename, size_t *size) {
    static uint8_t empty[1];
    FILE *fin = fopen(filename, "rb");
    if (!fin) { *size = 0; return empty; }
    fseeko64(fin, 0, SEEK_END);
    int64_t fsize = ftello64(fin);
    uint8_t *result = malloc(fsize);
    if (!result) { fclose(fin); *size = 0; return empty; }
    fseeko64(fin, 0, SEEK_SET);
    uint8_t *buffer = result;
    int64_t left = fsize;
    while(left > 0) {
        int rsize = left > (INT_MAX >> 1) ? (INT_MAX >> 1) : left;
        rsize = fread(buffer, 1, rsize, fin);
        if (rsize <= 0) { break; }
        buffer += rsize;
        left -= rsize;
    }
    fclose(fin);
    *size = fsize - left;
    return result;
}

