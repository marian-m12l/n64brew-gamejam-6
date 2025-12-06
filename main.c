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

uint32_t stored = 0;
void* last_addr = RAM_START_ADDR;

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

// Callback for NMI/Reset interrupt
static void reset_interrupt_callback(void) {
	// There's a minimum guaranteed of 200ms (up to 500ms) before the console actually resets the game
	// Reset does NOT happen if the player holds the reset button
	printf("RESET handler: remove stuff from RAM ??\n");

	uint32_t reset_pressed_since = exception_reset_time();	// TODO in ticks count

	int i = 2;
	while (stored > 0 && i > 0 && last_addr >= RAM_START_ADDR) {
		memset((void*) last_addr, 0, sizeof(banjo_data));
		printf("[ OK ] REMOVED DATA at address %p...\n", last_addr);
		last_addr -= STEP;
		stored--;
		i--;
	}
	printf("[ OK ] Removed a total of %d occurrences.\n\n", 2 - i);
}

int main(void)
{
	display_init(RESOLUTION_320x240, DEPTH_32_BPP, 2, GAMMA_NONE, ANTIALIAS_RESAMPLE);
	console_init();

	printf("Stop N Swop Test ROM\n\n");

	///////////////////////////////////////////////////////////////////////////

	// Scan for previous data on every 128-bytes boundary past the first 1MB of memory
	printf("[ -- ] Scanning for previous data...\n");
	for (void* addr = RAM_START_ADDR; addr < RAM_END_ADDR; addr += STEP) {
		if (*((uint32_t*)addr) == 0xC908C52F) {
			//printf("[ OK ] FOUND magic number at address %p...\n", addr);
			if (memcmp(addr, banjo_data, sizeof(banjo_data)) == 0) {
				stored++;
				last_addr = addr;
				if (stored < 5) {
					printf("[ OK ] FOUND DATA at address %p...\n", addr);
				} else if (stored == 5) {
					printf("[ OK ] ...\n");
				}
			}
		}
	}
	printf("[ OK ] Found %ld occurrences. Last item @ %p\n\n", stored, last_addr);

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

	// Reset IRQ handler
	register_RESET_handler(reset_interrupt_callback);
	
	printf("[ -- ] Press A to store data...\n");

	console_render();

	controller_init();
	while (true) {
		controller_scan();
		struct controller_data keys = get_keys_pressed();
		if (keys.c[0].A) {
			//printf("A pressed.\n");
			if (last_addr + STEP < RAM_END_ADDR) {
				last_addr += STEP;
				memcpy((void*) last_addr, banjo_data, sizeof(banjo_data));
				stored++;
			}
			printf("[ OK ] Stored a total of %ld occurrences.\n", stored);
			//break;
		} else if (keys.c[0].B) {
			reset_interrupt_callback();
		}
	}
}
