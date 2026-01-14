#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <libdragon.h>
#include "persistence.h"

uint8_t rdram_heap[HEAP_COUNT][CHUNK_SIZE] __attribute__((section(".rdram_heap")));
uint8_t rdram_expansion_heap[EXPANSION_HEAP_COUNT][CHUNK_SIZE] __attribute__((section(".rdram_expansion_heap")));

uint8_t cached_heap[HEAP_COUNT][CHUNK_SIZE] __attribute__((section(".cached_heap")));
uint8_t cached_expansion_heap[EXPANSION_HEAP_COUNT][CHUNK_SIZE] __attribute__((section(".cached_expansion_heap")));

heap_t heap1 = {
	.heap = rdram_heap,
	.cache = cached_heap,
	.len = 128
};
heap_t heap2 = {
	.heap = &rdram_heap[128],
	.cache = &cached_heap[128],
	.len = HEAP_COUNT-128
};

heap_t heap3 = {
	.heap = rdram_expansion_heap,
	.cache = cached_expansion_heap,
	.len = 1024
};

heap_t heap4 = {
	.heap = &rdram_expansion_heap[1024],
	.cache = &cached_expansion_heap[1024],
	.len = EXPANSION_HEAP_COUNT-1024
};

void* alloc_heap(heap_t* heap, int size, bool cached) {
	assert(size <= CHUNK_SIZE);
	int i=0;
	while(heap->allocated[i] && i < heap->len) {
		i++;
	}
	heap->allocated[i] = true;
	return cached ? &(heap->cache[i]) : &(heap->heap[i]);
}

void free_heap(heap_t* heap, void* ptr) {
	int i = (ptr - (void*)&heap->heap) / CHUNK_SIZE;
	if (i < 0 || i > 1015) {
		i = (ptr - (void*)&heap->cache) / CHUNK_SIZE;
	}
	heap->allocated[i] = false;
}

static uint16_t calculate_crc16(const uint8_t * data, size_t len) {
    uint8_t x;
    uint16_t crc = 0xFFFF;

    while ( len-- )
    {
        x = crc >> 8 ^ *(data++);
        x ^= x>>4;
        crc = (
            (crc << 8) ^ 
            ((uint16_t)(x << 12)) ^ 
            ((uint16_t)(x << 5)) ^ 
            ((uint16_t)x)
        );
    }

    return crc;
}

// TODO Distribute replicas more evenly ? Over multiple heaps ?
void replicate(heap_t* heap, uint32_t id, void* data, int len, int replicas, bool cached, bool flush, void** addresses) {
    int stored_len = sizeof(uint32_t) + len + sizeof(uint16_t);
	assert(stored_len <= CHUNK_SIZE);
	uint16_t crc16 = calculate_crc16(data, len);
	for (int i=0; i<replicas; i++) {
		void* ptr = alloc_heap(heap, stored_len, cached);
		memcpy(ptr, &id, sizeof(uint32_t));
		memcpy(ptr+sizeof(uint32_t), data, len);
		memcpy(ptr+sizeof(uint32_t)+len, &crc16, sizeof(crc16));
		if (memcmp(ptr, &id, sizeof(uint32_t)) != 0 || memcmp(ptr+sizeof(uint32_t), data, len) != 0 || memcmp(ptr+sizeof(uint32_t)+len, &crc16, sizeof(uint16_t)) != 0) {
			debugf("Copy failed\n");
		}
        //debugf(">>> stored object with id 0x%08x @ %p\n", id, ptr);

		// Optionally flush cache to RDRAM
		if (cached && flush) {
			data_cache_hit_writeback(ptr, stored_len);
			inst_cache_hit_invalidate(ptr, stored_len);
		}
        addresses[i] = ptr;
	}
}

void update_replicas(void** addresses, void* data, int len, int replicas, bool flush) {
    int stored_len = sizeof(uint32_t) + len + sizeof(uint16_t);
	assert(stored_len <= CHUNK_SIZE);
	uint16_t crc16 = calculate_crc16(data, len);
	for (int i=0; i<replicas; i++) {
		uint8_t* ptr = addresses[i];
		memcpy(ptr+sizeof(uint32_t), data, len);
		memcpy(ptr+sizeof(uint32_t)+len, &crc16, sizeof(crc16));
		if (memcmp(ptr+sizeof(uint32_t), data, len) != 0 || memcmp(ptr+sizeof(uint32_t)+len, &crc16, sizeof(uint16_t)) != 0) {
			debugf("Update failed\n");
		}
		// Optionally flush cache to RDRAM
		if (((uintptr_t) ptr & 0x80000000) == 0x80000000 && flush) {
			data_cache_hit_writeback(ptr, stored_len);
			inst_cache_hit_invalidate(ptr, stored_len);
		}
	}
}

void erase_and_free_replicas(heap_t* heap, void** addresses, int replicas) {
    for (int i=0; i<replicas; i++) {
		uint8_t* ptr = addresses[i];
		if (ptr != NULL) {
			memset(ptr, 0, CHUNK_SIZE);
			// Flush cache to RDRAM
			if (((uintptr_t) ptr & 0x80000000) == 0x80000000) {
				data_cache_hit_writeback(ptr, CHUNK_SIZE);
				inst_cache_hit_invalidate(ptr, CHUNK_SIZE);
			}
			free_heap(heap, ptr);
		}
	}
}

bool contains_id(uint32_t* ids, int len, uint32_t id) {
    for (int i=0; i<len; i++) {
        if (ids[i] == id) {
            return true;
        }
    }
    return false;
}

int restore(heap_t* heap, void* dest, int len, int stride, int max, uint32_t magic, uint32_t mask) {
    int restored = 0;
    uint32_t* ids = malloc(max * sizeof(uint32_t));
	for (int i=0; i<heap->len; i++) {
        // Cached
		uint8_t* ptr = (uint8_t*) &(heap->cache[i]);
        uint32_t id = *(uint32_t*) ptr;
        bool found = contains_id(ids, max, id);
        if ((id & mask) == magic) {
			uint16_t crc16 = calculate_crc16(ptr+sizeof(uint32_t), len);
			if (memcmp(ptr+sizeof(uint32_t)+len, &crc16, 2) == 0) {
                // FIXME heap->allocated[i] = true;
                if (!found) {
                    //debugf("<<< restored object with id 0x%08x @ %p\n", id, ptr);
                    memcpy(dest+restored*stride, ptr+sizeof(uint32_t), len);
                    ids[restored] = id;
                    restored++;
                    found = true;
                }
			}
		}
        // Uncached
        ptr = (uint8_t*) &(heap->heap[i]);
        id = *(uint32_t*) ptr;
        if ((id & mask) == magic) {
            uint16_t crc16 = calculate_crc16(ptr+sizeof(uint32_t), len);
            if (memcmp(ptr+sizeof(uint32_t)+len, &crc16, 2) == 0) {
                // FIXME heap->allocated[i] = true;
                if (!found) {
                    //debugf("<<< restored object with id 0x%08x @ %p\n", id, ptr);
                    memcpy(dest+restored*stride, ptr+sizeof(uint32_t), len);
                    ids[restored] = id;
                    restored++;
                    found = true;
                }
            }
        }
	}
    // TODO Need to keep references to valid replicas in the struct itself ?
	debugf("Found %d instances\n", restored);
    free(ids);
	return restored;
}