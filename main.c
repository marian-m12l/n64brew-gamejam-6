#include <string.h>
#include <stdint.h>
#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>

#include "pc64.h"

#define FB_COUNT 3

// Keep rotation angle in persistent ram to survive reset without explicit backup/restore
float rotAngle __attribute__((section(".persistent")));

// TODO Game data structures + heap allocator
// TODO volatile ?
uint8_t rdram_heap[2044][1024] __attribute__((section(".rdram_heap")));
uint8_t rdram_expansion_heap[4032][1024] __attribute__((section(".rdram_expansion_heap")));

uint8_t cached_heap[2044][1024] __attribute__((section(".cached_heap")));
uint8_t cached_expansion_heap[4032][1024] __attribute__((section(".cached_expansion_heap")));

#define RAM_START_ADDR ((void*)0x80200000)
#define RAM_END_ADDR ((void*)0x80300000)
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
	//printf("RESET handler: remove stuff from RAM ??\n");

	ms_counter = 0;
	//printf("exception_reset_time() = %ld\n", exception_reset_time());

	//uint32_t reset_pressed_since = exception_reset_time();	// TODO in ticks count
	reset_held = exception_reset_time();
	//printf("reset_held = %ld ticks\n\n", reset_held);

	int i = 2;
	while (stored > 0 && i > 0 && last_addr >= RAM_START_ADDR) {
		memset((void*) last_addr, 0, sizeof(banjo_data));
		//printf("[ OK ] REMOVED DATA at address %p...\n", last_addr);
		last_addr -= STEP;
		stored--;
		i--;
	}
	//printf("[ OK ] Removed a total of %d occurrences.\n\n", 2 - i);

	// Increment counter until actual reset --> measure how long the button is held
	uint64_t ticks = get_ticks();
	while (true) {
		if (get_ticks() - ticks > TICKS_PER_SECOND/1000) {
			ms_counter++;
			ticks = get_ticks();
		}
		reset_held = exception_reset_time();
	}
}

int main(void) {
	debug_init_isviewer();
	debug_init_usblog();
	asset_init_compression(2);
	dfs_init(DFS_DEFAULT_LOCATION);
	display_init(RESOLUTION_320x240, DEPTH_16_BPP, FB_COUNT, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS);
	rdpq_init();
	joypad_init();
	t3d_init((T3DInitParams){});

	T3DMat4FP* modelMatFP = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);
	T3DViewport viewport = t3d_viewport_create_buffered(FB_COUNT);
  	rdpq_text_register_font(FONT_BUILTIN_DEBUG_MONO, rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO));

	const T3DVec3 camPos = {{0,10.0f,40.0f}};
	const T3DVec3 camTarget = {{0,0,0}};

	uint8_t colorAmbient[4] = {80, 80, 100, 0xFF};
	uint8_t colorDir[4]     = {0xEE, 0xAA, 0xAA, 0xFF};

	T3DVec3 lightDirVec = {{-1.0f, 1.0f, 1.0f}};
	t3d_vec3_norm(&lightDirVec);

	T3DModel *model = t3d_model_load("rom:/model.t3dm");

	//float rotAngle = 0.0f;
	rspq_block_t *dplDraw = NULL;
	int frameIdx = 0;

	//printf("Stop N Swop Test ROM\n\n");

	reset_type_t rst = sys_reset_type();
	// TODO Treat separately cold, lukewarm (cold with remaining data in ram), and warm boots
	//printf("Boot type: %s\n\n", rst == RESET_COLD ? "COLD" : "WARM");
	if (rst == RESET_COLD) {
		rotAngle = 0.0f;
	}

	//printf("reset_held = %ld ticks / %ld ms\n\n", reset_held, TICKS_TO_MS(reset_held));
	//printf("ms_counter = %ld ms\n\n", ms_counter);

	// Print Hello from the N64
	/*strcpy(write_buf, "Hello from the N64!\r\n");
	pc64_uart_write((const uint8_t *)write_buf, strlen(write_buf));
	sprintf(write_buf, "ms_counter: %ld\r\n", ms_counter);
	pc64_uart_write((const uint8_t *)write_buf, strlen(write_buf));
	printf("[ -- ] Wrote hello over UART.\n");*/

	// Clear ms_counter so we know if the next boot is reset or cold boot
	uint32_t last_ms_counter = ms_counter;
	ms_counter = 0;

	// TODO Read cold boot / hot boot flag (from IPL) ??

	///////////////////////////////////////////////////////////////////////////

	// Scan for previous data on every 128-bytes boundary past the first 1MB of memory
	//printf("[ -- ] Scanning for previous data...\n");
	for (void* addr = RAM_START_ADDR; addr < RAM_END_ADDR; addr += STEP) {
		if (*((uint32_t*)addr) == 0xC908C52F) {
			//printf("[ OK ] FOUND magic number at address %p...\n", addr);
			if (memcmp(addr, banjo_data, sizeof(banjo_data)) == 0) {
				stored++;
				last_addr = addr;
				/*if (stored < 5) {
					printf("[ OK ] FOUND DATA at address %p...\n", addr);
				} else if (stored == 5) {
					printf("[ OK ] ...\n");
				}*/
			}
		}
	}
	//printf("[ OK ] Found %ld occurrences. Last item @ %p\n\n", stored, last_addr);

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
	
	//printf("[ -- ] Press A to store data...\n");

	//console_render();

	while (true) {
		joypad_poll();
		joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
		joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
		if (pressed.a) {
			// TODO Only on first press?
			//printf("A pressed.\n");
			// TODO Use heap allocation instead
			if (last_addr + STEP < RAM_END_ADDR) {
				last_addr += STEP;
				memcpy((void*) last_addr, banjo_data, sizeof(banjo_data));
				stored++;
			}
			//printf("[ OK ] Stored a total of %ld occurrences.\n", stored);
			//break;
		} else if (pressed.b) {
			reset_interrupt_callback();
		}


    	// ======== Update ======== //
		frameIdx = (frameIdx + 1) % FB_COUNT;

		rotAngle -= 0.02f;
		float modelScale = 0.1f;

		t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(85.0f), 10.0f, 150.0f);
		t3d_viewport_look_at(&viewport, &camPos, &camTarget, &(T3DVec3){{0,1,0}});

		t3d_mat4fp_from_srt_euler(
			&modelMatFP[frameIdx],
			(float[3]){modelScale, modelScale, modelScale},
			(float[3]){0.0f, rotAngle*0.2f, rotAngle},
			(float[3]){0,0,0}
		);

    	// ======== Draw (3D) ======== //
		rdpq_attach(display_get(), display_get_zbuf());
		t3d_frame_start();
		t3d_viewport_attach(&viewport);

		t3d_screen_clear_color(RGBA32(100, 80, 80, 0xFF));
		t3d_screen_clear_depth();

		t3d_light_set_ambient(colorAmbient);
		t3d_light_set_directional(0, colorDir, &lightDirVec);
		t3d_light_set_count(1);

		//rdpq_set_prim_color(get_rainbow_color(rotAngle * 0.42f));

		if(!dplDraw) {
			rspq_block_begin();
			t3d_model_draw(model);
			t3d_matrix_pop(1);
			dplDraw = rspq_block_end();
		}

		t3d_matrix_push(&modelMatFP[frameIdx]);
		rspq_block_run(dplDraw);

		// ======== Draw (2D) ======== //
		rdpq_sync_pipe();

		heap_stats_t stats;
		sys_get_heap_stats(&stats);
    	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 210, "    Heap size : %d", stats.total);
    	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 220, "    Allocated : %d", stats.used);

		rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 200, "Stored  : %ld", stored);
		rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 210, "Reset held: %ldms", rst == RESET_WARM ? last_ms_counter : 0);
		rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 220, "FPS   : %.2f", display_get_fps());


		rdpq_detach_show();
	}

  	t3d_destroy();
	return 0;
}
