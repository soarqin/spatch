#pragma once

#include <stdint.h>
#include <stddef.h>

uint8_t *read_whole_file(const char *filename, size_t *size);
int do_mkdir(const char *path, int recursive);
int file_exists(const char *path);
