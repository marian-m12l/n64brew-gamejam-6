#include <string.h>
#include <stdint.h>
#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>

#include "pc64.h"
#include "persistence.h"


#define FB_COUNT 3

// Keep rotation angle in persistent ram to survive reset without explicit backup/restore
float rotAngle __attribute__((section(".persistent")));

// TODO Game data structures
#define CONSOLE_MAGIC (0x11223300)
#define CONSOLE_MASK (0xffffff00)

typedef struct {
	uint32_t id;
	// TODO Actual data
	uint16_t x;
	uint16_t y;
	uint16_t z;
	uint16_t speed_x;
	uint16_t speed_y;
	uint16_t speed_z;
	uint8_t health;
	uint8_t state;
	bool selected;
	// TODO t3d vector, animation state ?
} console_t;

uint32_t stored = 0;

volatile uint32_t reset_held __attribute__((section(".persistent")));

// Callback for NMI/Reset interrupt
static void reset_interrupt_callback(void) {
	// There's a minimum guaranteed of 200ms (up to 500ms) before the console actually resets the game
	// Reset does NOT happen if the player holds the reset button
	reset_held = exception_reset_time();

	// TODO Destroy data in heaps ?! in persistent ram ? randomly ?
	// TODO Visual feedback? Continue rendering and count ticks in main loop ??

	// Measure how long the reset button is held
	while (true) {
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

	rspq_block_t *dplDraw = NULL;
	int frameIdx = 0;

	debugf("Stop N Swop Test ROM\n");

	reset_type_t rst = sys_reset_type();
	// TODO Treat separately cold, lukewarm (cold with remaining data in ram), and warm boots
	debugf("Boot type: %s\n", rst == RESET_COLD ? "COLD" : "WARM");
	if (rst == RESET_COLD) {
		rotAngle = 0.0f;
	}
	uint32_t held_ms = (rst == RESET_COLD) ? 0 : TICKS_TO_MS(reset_held);
	debugf("held_ms = %ld\n", held_ms);
	
	// Restore game data from heap replicas
	console_t consoles[10];
	stored = restore(&heap1, consoles, sizeof(console_t), 10, CONSOLE_MAGIC, CONSOLE_MASK);

	
	// Reset IRQ handler
	register_RESET_handler(reset_interrupt_callback);
	
	while (true) {
		joypad_poll();
		joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
		if (pressed.a && stored < heap1.len) {
			console_t* console = malloc(sizeof(console_t));
			console->id = stored;
			replicate(&heap1, CONSOLE_MAGIC | console->id, console, sizeof(console_t), 4, true, true);
			stored++;
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
		rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 210, "Reset held: %ldms", held_ms);
		rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 220, "FPS   : %.2f", display_get_fps());


		rdpq_detach_show();
	}

  	t3d_destroy();
	return 0;
}
