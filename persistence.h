#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CHUNK_SIZE 128
#define HEAP_COUNT (((2048-4)*1024)/CHUNK_SIZE)
#define EXPANSION_HEAP_COUNT (((4096-64)*1024)/CHUNK_SIZE)

typedef struct {
	uint8_t (*heap)[CHUNK_SIZE];
	uint8_t (*cache)[CHUNK_SIZE];
	bool allocated[HEAP_COUNT];
	uint16_t len;
} heap_t;

extern heap_t heap1;
extern heap_t heap2;
extern heap_t heap3;
extern heap_t heap4;

void replicate(heap_t* heap, uint32_t id, void* data, int len, int replicas, bool cached, bool flush);
int restore(heap_t* heap, void* dest, int len, int max, uint32_t magic, uint32_t mask);
