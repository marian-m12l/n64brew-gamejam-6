#include <string.h>
#include <stdint.h>
#include <libdragon.h>

#include "pc64.h"

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

volatile uint32_t reset_held __attribute__((section(".persistent")));
volatile uint32_t ms_counter __attribute__((section(".persistent")));

// Callback for NMI/Reset interrupt
static void reset_interrupt_callback(void) {
	// There's a minimum guaranteed of 200ms (up to 500ms) before the console actually resets the game
	// Reset does NOT happen if the player holds the reset button
	printf("RESET handler: remove stuff from RAM ??\n");

	ms_counter = 0;
	printf("exception_reset_time() = %ld\n", exception_reset_time());

	//uint32_t reset_pressed_since = exception_reset_time();	// TODO in ticks count
	reset_held = exception_reset_time();
	printf("reset_held = %ld ticks\n\n", reset_held);

	int i = 2;
	while (stored > 0 && i > 0 && last_addr >= RAM_START_ADDR) {
		memset((void*) last_addr, 0, sizeof(banjo_data));
		printf("[ OK ] REMOVED DATA at address %p...\n", last_addr);
		last_addr -= STEP;
		stored--;
		i--;
	}
	printf("[ OK ] Removed a total of %d occurrences.\n\n", 2 - i);

	// Increment counter until actual reset --> measure how long the button is held
	uint64_t ticks = get_ticks();
	while (1) {
		if (get_ticks() - ticks > (CPU_FREQUENCY/2)/1000) {
			ms_counter++;
			ticks = get_ticks();
			printf("ms_counter = %ld ms\n\n", ms_counter);
		}

		reset_held = exception_reset_time();
		printf("reset_held = %ld ticks\n\n", reset_held);
	}
}

int main(void)
{
	display_init(RESOLUTION_320x240, DEPTH_32_BPP, 2, GAMMA_NONE, ANTIALIAS_RESAMPLE);
	console_init();

	printf("Stop N Swop Test ROM\n\n");

	printf("reset_held = %ld ticks / %ld ms\n\n", reset_held, reset_held/((CPU_FREQUENCY/2)/1000));
	printf("ms_counter = %ld ms\n\n", ms_counter);

	// Print Hello from the N64
	strcpy(write_buf, "Hello from the N64!\r\n");
	pc64_uart_write((const uint8_t *)write_buf, strlen(write_buf));
	sprintf(write_buf, "ms_counter: %ld\r\n", ms_counter);
	pc64_uart_write((const uint8_t *)write_buf, strlen(write_buf));
	printf("[ -- ] Wrote hello over UART.\n");

	// Clear ms_counter so we know if the next boot is reset or cold boot
	ms_counter = 0;

	// TODO Read cold boot / hot boot flag (from IPL) ??

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
