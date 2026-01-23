#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	HIGHEST = 0,
	LOW,
	LOWEST
} persistence_level_t;

void replicate(persistence_level_t level, uint32_t id, void* data, int len, int replicas, bool cached, bool flush, void** addresses);
void update_replicas(void** addresses, void* data, int len, int replicas, bool flush);
void erase_and_free_replicas(void** addresses, int replicas);
int restore(void* dest, int* counts, int len, int stride, int max, uint32_t magic, uint32_t mask);
void clear_heaps();
void heaps_stats(char* buffer, int len);
