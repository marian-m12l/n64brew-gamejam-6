#include <string.h>
#include <stdint.h>
#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>

#include "pc64.h"
#include "persistence.h"


#define FB_COUNT 3

T3DViewport viewport;
T3DVec3 camPos = {{0,10.0f,40.0f}};
T3DVec3 camTarget = {{0,0,0}};
uint8_t colorAmbient[4] = {80, 80, 100, 0xFF};
uint8_t colorDir[4]     = {0xEE, 0xAA, 0xAA, 0xFF};
T3DVec3 lightDirVec = {{-1.0f, 1.0f, 1.0f}};
int frameIdx = 0;

T3DModel* console_model;

// TODO Game data structures
#define CONSOLE_MAGIC (0x11223300)
#define CONSOLE_MASK (0xffffff00)

#define MAX_CONSOLES (10)
#define CONSOLE_REPLICAS (4)

// Struct to hold runtime stuff (model, display list, ...)
typedef struct {
    T3DModel* model;
    T3DMat4FP* mat_fp;
    rspq_block_t* dpl;
} displayable_t;

typedef struct {
	uint32_t id;
    T3DVec3 scale;
    T3DVec3 rotation;
    T3DVec3 position;
	float rot_speed;
	color_t color;
	// TODO T3DSkeleton, T3DAnim, wav64_t, c2AABB, ...
	// Exclude remaining fields from replication
	char __exclude;
	displayable_t* displayable;	// Link to the displayable data is stale and must updated when restoring
	void* replicas[CONSOLE_REPLICAS];	// Replica pointers may be stale and must be updated when restoring
} console_t;

#define CONSOLE_PAYLOAD_SIZE (sizeof(console_t)-(sizeof(console_t)-offsetof(console_t, __exclude)))

// TODO Static or dynamic allocation ?
console_t consoles[MAX_CONSOLES];
displayable_t console_displayables[MAX_CONSOLES];

uint32_t consoles_count = 0;
uint32_t held_ms;

volatile uint32_t reset_held __attribute__((section(".persistent")));

// Callback for NMI/Reset interrupt
/*static void reset_interrupt_callback(void) {
	// There's a minimum guaranteed of 200ms (up to 500ms) before the console actually resets the game
	// Reset does NOT happen if the player holds the reset button
	reset_held = exception_reset_time();

	// TODO Destroy data in heaps ?! in persistent ram ? randomly ?
	// TODO Visual feedback? Continue rendering and count ticks in main loop ??

	// Measure how long the reset button is held
	while (true) {
		reset_held = exception_reset_time();
	}
}*/

console_t* add_console() {
	console_t* console = &consoles[consoles_count];
	debugf("add console #%d (%p)\n", consoles_count, console);
	console->id = consoles_count;
	console->scale = (T3DVec3){{ 0.05f, 0.05f, 0.05f }};
	console->rotation = (T3DVec3){{ 0.0f, 0.0f, 0.0f }};
	console->position = (T3DVec3){{ -45.0f + 10.f * consoles_count, 30.0f - (rand() % 60), 0.0f }};
	console->rot_speed = (5.0f - (rand() % 10)) * 0.02f;
	console->color = RGBA32(rand() % 255, rand() % 255, rand() % 255, 255);
	console->displayable = &console_displayables[consoles_count];
	consoles_count++;
	return console;
}

void setup_console(console_t* console) {
	displayable_t* displayable = console->displayable;
	displayable->model = console_model;
	displayable->mat_fp = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);	// FIXME Need to free !
	rspq_block_begin();
		t3d_matrix_push(displayable->mat_fp);
		rdpq_set_prim_color(console->color);
		//t3d_model_draw_skinned(displayable->model, &displayable->skel);
		t3d_model_draw(displayable->model);
		t3d_matrix_pop(1);
	displayable->dpl = rspq_block_end();
}

void replicate_console(console_t* console) {
	debugf("replicate console #%d\n", console->id);
	replicate(&heap1, CONSOLE_MAGIC | console->id, console, CONSOLE_PAYLOAD_SIZE, CONSOLE_REPLICAS, true, true, console->replicas);
	debugf("replicas: %p %p %p %p\n", console->replicas[0], console->replicas[1], console->replicas[2], console->replicas[3]);
}

void update_console(console_t* console) {
	//debugf("updating console replicas: %p %p %p %p\n", console->replicas[0], console->replicas[1], console->replicas[2], console->replicas[3]);
	update_replicas(console->replicas, console, CONSOLE_PAYLOAD_SIZE, CONSOLE_REPLICAS, true);
}

void update(bool in_reset) {
	// TODO Move camera?
	t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(85.0f), 10.0f, 150.0f);
	t3d_viewport_look_at(&viewport, &camPos, &camTarget, &(T3DVec3){{0,1,0}});

	// Move models
	for (int i=0; i<consoles_count; i++) {
		console_t* console = &consoles[i];
		console->rotation.y -= in_reset ? console->rot_speed * 0.1f : console->rot_speed * 0.2f;
		console->rotation.z -= in_reset ? console->rot_speed * 5.0f : console->rot_speed;
		// Need to update replicas with new values
		update_console(console);
	}
}

void render_3d(bool in_reset) {
	frameIdx = (frameIdx + 1) % FB_COUNT;

	rdpq_attach(display_get(), display_get_zbuf());
	t3d_frame_start();
	t3d_viewport_attach(&viewport);

	t3d_screen_clear_color(RGBA32(100, 80, 80, 0xFF));
	t3d_screen_clear_depth();

	t3d_light_set_ambient(colorAmbient);
	t3d_light_set_directional(0, colorDir, &lightDirVec);
	t3d_light_set_count(1);

	for (int i=0; i<consoles_count; i++) {
		console_t* console = &consoles[i];
		t3d_mat4fp_from_srt_euler(
			&console->displayable->mat_fp[frameIdx],
			console->scale.v,
			console->rotation.v,
			console->position.v
		);
		rdpq_set_prim_color(console->color);
		rspq_block_run(console->displayable->dpl);
	}
}

void render_2d() {
	rdpq_sync_pipe();

	heap_stats_t stats;
	sys_get_heap_stats(&stats);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 210, "    Heap size : %d", stats.total);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 220, "    Allocated : %d", stats.used);

	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 200, "Consoles  : %ld", consoles_count);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 210, "Reset held: %ldms", held_ms);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 220, "FPS   : %.2f", display_get_fps());
}

int main(void) {
	debug_init_isviewer();
	debug_init_usblog();
	asset_init_compression(2);
	dfs_init(DFS_DEFAULT_LOCATION);
	display_init(RESOLUTION_320x240, DEPTH_16_BPP, FB_COUNT, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS);
	rdpq_init();
	joypad_init();

    // Initialize the random number generator, then call rand() every
    // frame so to get random behavior also in emulators.
    uint32_t seed;
    getentropy(&seed, sizeof(seed));
    srand(seed);
    register_VI_handler((void(*)(void))rand);

	t3d_init((T3DInitParams){});
	viewport = t3d_viewport_create_buffered(FB_COUNT);
  	rdpq_text_register_font(FONT_BUILTIN_DEBUG_MONO, rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO));

	t3d_vec3_norm(&lightDirVec);

	frameIdx = 0;

	console_model = t3d_model_load("rom:/model.t3dm");

	debugf("Stop N Swop Test ROM\n");

	reset_type_t rst = sys_reset_type();
	// TODO Treat separately cold, lukewarm (cold with remaining data in ram), and warm boots
	debugf("Boot type: %s\n", rst == RESET_COLD ? "COLD" : "WARM");
	held_ms = (rst == RESET_COLD) ? 0 : TICKS_TO_MS(reset_held);
	debugf("held_ms = %ld\n", held_ms);
	
	// Restore game data from heap replicas
	consoles_count = restore(&heap1, consoles, CONSOLE_PAYLOAD_SIZE, sizeof(console_t), MAX_CONSOLES, CONSOLE_MAGIC, CONSOLE_MASK);

	if (consoles_count == 0) {
		// Initial setup
		debugf("Initializing 2 consoles\n");
		for (int i=0; i<2; i++) {
			console_t* console = add_console();
			replicate_console(console);
		}
	} else {
		// Restored at least once console: keep playing
		for (int i=0; i<consoles_count; i++) {
			console_t* console = &consoles[i];
			debugf("restored: %d\n", console->id);
			debugf("rotation: %f %f %f\n", console->rotation.x, console->rotation.y, console->rotation.z);
			debugf("position: %f %f %f\n", console->position.x, console->position.y, console->position.z);
			debugf("rot_speed: %f\n", console->rot_speed);
			debugf("color: %d %d %d\n", console->color.r, console->color.g, console->color.b);
			console->displayable = &console_displayables[i];
			// Recreate replicas (alternative would be to keep replicas as-is)
			replicate_console(console);
		}
	}

	// Load model for each console
	for (int i=0; i<consoles_count; i++) {
		setup_console(&consoles[i]);
	}
	

	
	// Reset IRQ handler
	//register_RESET_handler(reset_interrupt_callback);
	
	while (true) {
		// TODO check exception_reset_time() to handle reset animation (fade to black? mask to black ? make console models react ??)
		// TODO Alternate fast loop to be able to update reset_held with exception_reset_time() and measure reset button hold time?
		reset_held = exception_reset_time();
		bool in_reset = reset_held != 0;
		if (!in_reset) {
			joypad_poll();
			joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
			if (pressed.a && consoles_count < MAX_CONSOLES) {
				console_t* console = add_console();
				replicate_console(console);
				setup_console(console);
			}
		}

		update(in_reset);
		render_3d(in_reset);
		render_2d();
		rdpq_detach_show();
	}

  	t3d_destroy();
	return 0;
}
