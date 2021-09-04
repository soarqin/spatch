#include "memstream.h"

#include <stdlib.h>
#include <string.h>

struct memstream_entry_s {
    struct memstream_entry_s *next;
    uint8_t *data;
    size_t size;
};

struct memstream_s {
    struct memstream_entry_s *head, *tail;
    size_t read_pos;
    size_t total;
};

memstream_t *memstream_create() {
    memstream_t *stm = malloc(sizeof(memstream_t));
    memset(stm, 0, sizeof(memstream_t));
    return stm;
}

size_t memstream_write(memstream_t *stm, const void *data, size_t size) {
    struct memstream_entry_s *blk = malloc(sizeof(struct memstream_entry_s));
    if (!blk) { return 0; }
    blk->next = NULL;
    blk->data = malloc(size);
    if (!blk->data) { free(blk); return 0; }
    memcpy(blk->data, data, size);
    blk->size = size;
    if (stm->tail == NULL) {
        stm->head = stm->tail = blk;
    } else {
        stm->tail->next = blk;
        stm->tail = blk;
    }
    stm->total += size;
    return size;
}

size_t memstream_read(memstream_t *stm, void *data, size_t size) {
    uint8_t *output = data;
    size_t left = size;
    struct memstream_entry_s *entry = stm->head;
    if (!entry) {
        return 0;
    }
    while (left) {
        size_t to_read;
        to_read = entry->size - stm->read_pos;
        if (to_read > left) { to_read = left; }
        memcpy(output, entry->data + stm->read_pos, to_read);
        stm->read_pos += to_read;
        output += to_read;
        left -= to_read;
        if (stm->read_pos == entry->size) {
            struct memstream_entry_s *last = entry;
            entry = entry->next;
            stm->read_pos = 0;
            stm->head = entry;
            if (entry == NULL) {
                stm->tail = NULL;
                break;
            }
        }
    }
    size -= left;
    stm->total -= size;
    return size;
}

size_t memstream_size(memstream_t *stm) {
    return stm->total;
}

void memstream_destroy(memstream_t *stm) {
    struct memstream_entry_s *entry = stm->head;
    while (entry) {
        struct memstream_entry_s *last = entry;
        entry = entry->next;
        free(last);
    }
    free(stm);
}
