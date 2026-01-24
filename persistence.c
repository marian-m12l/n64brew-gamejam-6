#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <libdragon.h>

#include "persistence.h"

#include "pc64.h"

static void debugf_uart(char* format, ...) {
	va_list args;
	va_start(args, format);
	vsnprintf(write_buf, sizeof(write_buf), format, args);
	va_end(args);
	pc64_uart_write((const uint8_t *)write_buf, strlen(write_buf));
	debugf(write_buf);
}


#define CHUNK_SIZE 64
#define CHUNKS_COUNT (((2048-4)*1024)/CHUNK_SIZE)
#define EXPANSION_CHUNKS_COUNT (((4096-4-64)*1024)/CHUNK_SIZE)
#define STEP (31)
#define TOTAL_HEAPS (4)

typedef struct {
	uint8_t (*heap)[CHUNK_SIZE];
	uint8_t (*cache)[CHUNK_SIZE];
	bool allocated[EXPANSION_CHUNKS_COUNT];
	uint16_t len;
	uint16_t used;
} heap_t;

static uint8_t rdram_heap[CHUNKS_COUNT][CHUNK_SIZE] __attribute__((section(".rdram_heap")));
static uint8_t rdram_expansion_heap[EXPANSION_CHUNKS_COUNT][CHUNK_SIZE] __attribute__((section(".rdram_expansion_heap")));

static uint8_t cached_heap[CHUNKS_COUNT][CHUNK_SIZE] __attribute__((section(".cached_heap")));
static uint8_t cached_expansion_heap[EXPANSION_CHUNKS_COUNT][CHUNK_SIZE] __attribute__((section(".cached_expansion_heap")));

static heap_t heaps[TOTAL_HEAPS] = {
	// Highest to lowest retention
	{
		.heap = &rdram_heap[1024],
		.cache = &cached_heap[1024],
		.len = 1024	// No need for such a large memory area
		//.len = (CHUNKS_COUNT-256) & 0xf000	// Make sure heap length works well with our stepping
	},
	{
		.heap = rdram_heap,
		.cache = cached_heap,
		.len = 1024
	},
	{
		.heap = &rdram_expansion_heap[1024],
		.cache = &cached_expansion_heap[1024],
		.len = 1024	// No need for such a large memory area
		//.len = (EXPANSION_CHUNKS_COUNT-1024) & 0xf000	// Make sure heap length works well with our stepping
	},
	{
		.heap = rdram_expansion_heap,
		.cache = cached_expansion_heap,
		.len = 1024
	}
};


static void* alloc_heap(heap_t* heap, int size, bool cached) {
	assert(size <= CHUNK_SIZE);
	int i=0;
	while(heap->allocated[(i*STEP)%heap->len] && i < heap->len) {
		i++;
	}
	i = (i * STEP) % heap->len;
	assert(i < heap->len);	// Fail if no slot available
	heap->allocated[i] = true;
	heap->used++;
	return cached ? &(heap->cache[i]) : &(heap->heap[i]);
}

static void free_heap(heap_t* heap, void* ptr) {
	int i = (ptr - (void*)heap->heap) / CHUNK_SIZE;
	if (i < 0 || i > heap->len) {
		i = (ptr - (void*)heap->cache) / CHUNK_SIZE;
	}
	assert (i < heap->len);	// Fail if no valid slot
	heap->allocated[i] = false;
	heap->used--;
}

static void clear_heap(heap_t* heap) {
	for (int i=0; i < heap->len; i++) {
		heap->allocated[i] = false;
	}
	memset(heap->cache, 0, heap->len * CHUNK_SIZE);
	memset(heap->heap, 0, heap->len * CHUNK_SIZE);	// FIXME Needed ?
	data_cache_hit_writeback(heap->cache, heap->len * CHUNK_SIZE);
	inst_cache_hit_invalidate(heap->cache, heap->len * CHUNK_SIZE);
	heap->used = 0;
}

static const char zeroblock [64];
static void dump_heap(heap_t* heap) {
	/*
	debugf_uart("heap: %p %p %d/%d\n", heap->heap, heap->cache, heap->used, heap->len);
	for (int i=0; i<heap->len; i+=64) {
		if (memcmp(&heap->allocated[i], zeroblock, 64) != 0) {
			debugf_uart("%02x: ", i);
			for (int k=0; k<64; k++) {
				debugf_uart("%s", heap->allocated[i+k] ? "#" : "-");
			}
			debugf_uart("\n");
		}
	}
	*/
}


static uint16_t crc16(const uint8_t * data, size_t len, uint16_t init) {
    uint8_t x;
    uint16_t crc = init;

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

static uint16_t calculate_crc16(const uint32_t id, const uint8_t * data, size_t len) {
	uint16_t crc = crc16((uint8_t*) &id, sizeof(uint32_t), 0xffff);
	return crc16(data, len, crc);
}

void replicate(persistence_level_t level, uint32_t id, void* data, int len, int replicas, bool cached, bool flush, void** addresses) {
	// FIXME Persistence level should also determine cached / flush behaviour
	// FIXME Should also determine replicas count ??
	int min_heap = 0;
	int max_heap = TOTAL_HEAPS-1;
	switch (level) {
		case HIGHEST:
			break;
		case LOW:
			min_heap = 2;
			break;
		case LOWEST:
			min_heap = TOTAL_HEAPS-1;
			break;
	}
	int heaps_count = (1 + max_heap - min_heap);
	int replicas_per_heap = replicas / heaps_count;
	int replicas_remainder = replicas % heaps_count;
	debugf_uart("replicate: min=%d max=%d per_heap=%d remainder=%d\n", min_heap, max_heap, replicas_per_heap, replicas_remainder);
	assert(replicas == replicas_per_heap * heaps_count + replicas_remainder);

    int stored_len = sizeof(uint32_t) + len + sizeof(uint16_t);
	assert(stored_len <= CHUNK_SIZE);
	uint16_t crc16 = calculate_crc16(id, data, len);
	int replica = 0;
	for (int j=min_heap; j<=max_heap; j++) {
		heap_t* heap = &heaps[j];

		dump_heap(heap);

		int rounds = (j == min_heap) ? replicas_per_heap + replicas_remainder : replicas_per_heap;
		for (int i=0; i<rounds; i++) {
			//debugf_uart("alloc_heap(%d, %d, %d);\n", j, stored_len, cached);
			void* ptr = alloc_heap(heap, stored_len, cached);
			memcpy(ptr, &id, sizeof(uint32_t));
			memcpy(ptr+sizeof(uint32_t), data, len);
			memcpy(ptr+sizeof(uint32_t)+len, &crc16, sizeof(crc16));
			// FIXME assert
			if (memcmp(ptr, &id, sizeof(uint32_t)) != 0 || memcmp(ptr+sizeof(uint32_t), data, len) != 0 || memcmp(ptr+sizeof(uint32_t)+len, &crc16, sizeof(uint16_t)) != 0) {
				debugf_uart("Copy failed\n");
			}
			//debugf_uart(">>> stored object with id 0x%08x @ %p\n", id, ptr);

			// Optionally flush cache to RDRAM
			if (cached && flush) {
				data_cache_hit_writeback(ptr, stored_len);
				inst_cache_hit_invalidate(ptr, stored_len);
			}
			addresses[replica++] = ptr;
		}
		
		dump_heap(heap);
	}
	assert(replica == replicas);
}

void update_replicas(void** addresses, void* data, int len, int replicas, bool flush) {
    int stored_len = sizeof(uint32_t) + len + sizeof(uint16_t);
	assert(stored_len <= CHUNK_SIZE);
	uint32_t id = *(uint32_t*) addresses[0];	// TODO ??
	uint16_t crc16 = calculate_crc16(id, data, len);
	for (int i=0; i<replicas; i++) {
		uint8_t* ptr = addresses[i];
		memcpy(ptr+sizeof(uint32_t), data, len);
		memcpy(ptr+sizeof(uint32_t)+len, &crc16, sizeof(crc16));
		// FIXME assert
		if (memcmp(ptr+sizeof(uint32_t), data, len) != 0 || memcmp(ptr+sizeof(uint32_t)+len, &crc16, sizeof(uint16_t)) != 0) {
			debugf_uart("Update failed\n");
		}
		// Optionally flush cache to RDRAM
		if (((uintptr_t) ptr & 0x80000000) == 0x80000000 && flush) {
			data_cache_hit_writeback(ptr, stored_len);
			inst_cache_hit_invalidate(ptr, stored_len);
		}
	}
}

void erase_and_free_replicas(void** addresses, int replicas) {
    for (int i=0; i<replicas; i++) {
		uint8_t* ptr = addresses[i];
		if (ptr != NULL) {
			memset(ptr, 0, CHUNK_SIZE);
			// Flush cache to RDRAM
			if (((uintptr_t) ptr & 0x80000000) == 0x80000000) {
				data_cache_hit_writeback(ptr, CHUNK_SIZE);
				inst_cache_hit_invalidate(ptr, CHUNK_SIZE);
				memset((ptr + 0x20000000), 0, CHUNK_SIZE);
			}
			// Determine heap from ptr
			bool cleared = false;
			for (int j=0; j<TOTAL_HEAPS; j++) {
				heap_t* heap = &heaps[j];
				void* uncached_ptr = (void*) ((uintptr_t) ptr | 0x20000000);
				//debugf_uart("%p %p %p\n", uncached_ptr, (void*) heap->heap, (void*) &heap->heap[heap->len]);
				if (uncached_ptr >= (void*) heap->heap && uncached_ptr < (void*) &heap->heap[heap->len]) {
					//debugf_uart("address %p / %p in heap %d [%p-%p]\n", ptr, uncached_ptr, j, heap->heap, &heap->heap[heap->len]);
					free_heap(heap, ptr);
					cleared = true;
					break;
				}
			}
			if (!cleared) {
				debugf_uart("no match in heaps for ptr: %p\n", ptr);
			}
			assert(cleared);
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

int restore(void* dest, int* counts, int len, int stride, int max, uint32_t magic, uint32_t mask) {
	// Restore from ALL HEAPS
    int restored = 0;
    uint32_t* ids = malloc(max * sizeof(uint32_t));
	for (int j=0; j<TOTAL_HEAPS; j++) {
		heap_t* heap = &heaps[j];
		for (int i=0; i<heap->len; i++) {
			// Cached
			uint8_t* ptr = (uint8_t*) &(heap->cache[i]);
			uint32_t id = *(uint32_t*) ptr;
			bool found = contains_id(ids, max, id);
			if ((id & mask) == magic) {
				uint16_t crc16 = calculate_crc16(id, ptr+sizeof(uint32_t), len);
				if (memcmp(ptr+sizeof(uint32_t)+len, &crc16, 2) == 0) {
					// FIXME heap->allocated[i] = true;
					uint32_t index = *((uint32_t*) (ptr+sizeof(uint32_t)));
					assert(index < max);
					counts[index]++;
					if (!found) {
						//debugf_uart("<<< restored object with id 0x%08x @ %p\n", id, ptr);
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
				uint16_t crc16 = calculate_crc16(id, ptr+sizeof(uint32_t), len);
				if (memcmp(ptr+sizeof(uint32_t)+len, &crc16, 2) == 0) {
					// FIXME heap->allocated[i] = true;
					uint32_t index = *((uint32_t*) (ptr+sizeof(uint32_t)));
					assert(index < max);
					counts[index]++;
					if (!found) {
						//debugf_uart("<<< restored object with id 0x%08x @ %p\n", id, ptr);
						memcpy(dest+restored*stride, ptr+sizeof(uint32_t), len);
						ids[restored] = id;
						restored++;
						found = true;
					}
				}
			}
		}
		debugf_uart("restored replicas with index 0 in heap %d: %d\n", j, counts[0]);
	}
    // TODO Need to keep references to valid replicas in the struct itself ?
	debugf_uart("Found %d instances\n", restored);
	for (int i=0; i<restored; i++) {
		debugf_uart("id=0x%08x ", ids[i]);
	}
	debugf_uart("\n");
    free(ids);
	return restored;
}

void clear_heaps() {
	// For each heap, clear and free allocated chunks
	for (int j=0; j<TOTAL_HEAPS; j++) {
		heap_t* heap = &heaps[j];
		clear_heap(heap);
	}
}

void heaps_stats(char* buffer, int len) {
	snprintf(buffer, len, "%d/%d %d/%d %d/%d %d/%d",
		heaps[0].used, heaps[0].len,
		heaps[1].used, heaps[1].len,
		heaps[2].used, heaps[2].len,
		heaps[3].used, heaps[3].len
	);
}
