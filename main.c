/**
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2022 Konrad Beckmann <konrad.beckmann@gmail.com>
 * Copyright (c) 2022 Christopher Bonhage <me@christopherbonhage.com>
 * Copyright (c) 2023 Marian Muller Rebeyrol
 *
 * Based on https://github.com/meeq/SaveTest-N64
 */

#include <string.h>
#include <stdint.h>
#include <libdragon.h>

#define RAM_START_ADDR ((void*)0x80100000)
#define RAM_END_ADDR ((void*)0x80400000)
#define STEP 65536

static const uint32_t banjo_data[32] = {
	0xC908C52F, 0x00000108, 0x00000109, 0x0000010A,
	0x0000010B, 0x0000010C, 0x0000010D, 0x0000010E,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x638AE93A, 0x22A1C3FD
};

int main(void)
{
	display_init(RESOLUTION_320x240, DEPTH_32_BPP, 2, GAMMA_NONE, ANTIALIAS_RESAMPLE);
	console_init();

	printf("Stop N Swop Test ROM\n\n");

	///////////////////////////////////////////////////////////////////////////

	// Scan for previous data on every 128-bytes boundary past the first 1MB of memory
	printf("[ -- ] Scanning for previous data...\n");
	uint32_t found = 0;
	for (void* addr = RAM_START_ADDR; addr < RAM_END_ADDR; addr += STEP) {
		if (*((uint32_t*)addr) == 0xC908C52F) {
			//printf("[ OK ] FOUND magic number at address %p...\n", addr);
			if (memcmp(addr, banjo_data, sizeof(banjo_data)) == 0) {
				if (found < 4) {
					printf("[ OK ] FOUND DATA at address %p...\n", addr);
				} else if (found == 5) {
					printf("[ OK ] ...\n");
				}
				found++;
			}
		}
	}
	printf("[ OK ] Done scanning. Found %ld occurrences.\n\n", found);

	///////////////////////////////////////////////////////////////////////////

	// Store data on every 128-bytes boundary past the first 1MB of memory
	/*printf("[ -- ] Storing data...\n");
	uint32_t stored = 0;
	for (void* addr = RAM_START_ADDR; addr < RAM_END_ADDR; addr += STEP) {
		memcpy((void*) addr, banjo_data, sizeof(banjo_data));
		stored++;
	}
	printf("[ OK ] Done storing. Stored %ld occurrences.\n\n", stored);*/

	///////////////////////////////////////////////////////////////////////////
	
	printf("[ -- ] Press A to store data...\n");
	uint32_t stored = 0;
	void* addr = RAM_START_ADDR;

	console_render();

	controller_init();
	while (true) {
		controller_scan();
		struct controller_data keys = get_keys_pressed();
		if (keys.c[0].A) {
			//printf("A pressed.\n");
			if (addr < RAM_END_ADDR) {
				memcpy((void*) addr, banjo_data, sizeof(banjo_data));
				stored++;
				addr += STEP;
			}
			printf("[ OK ] Stored a total of %ld occurrences.\n\n", stored);
			//break;
		}
	}
}
