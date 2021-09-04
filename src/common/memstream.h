#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct memstream_s memstream_t;

memstream_t *memstream_create();
size_t memstream_write(memstream_t *stm, const void *data, size_t size);
size_t memstream_read(memstream_t *stm, void *data, size_t size);
size_t memstream_size(memstream_t *stm);
void memstream_destroy(memstream_t *stm);
