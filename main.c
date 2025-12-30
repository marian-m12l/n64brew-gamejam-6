#include <string.h>
#include <stdint.h>
#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>

#include "logo.h"
#include "pc64.h"
#include "persistence.h"


#define FB_COUNT 2

T3DViewport viewport;
T3DVec3 camPos = {{ 0.0f, 0.0f, 40.0f }};
T3DVec3 camTarget = {{0,0,0}};
uint8_t colorAmbient[4] = {80, 80, 100, 0xFF};
uint8_t colorDir[4]     = {0xEE, 0xAA, 0xAA, 0xFF};
T3DVec3 lightDirVec = {{-1.0f, 1.0f, 1.0f}};
int frameIdx = 0;

T3DModel* console_model;


#define OFFSCREEN_SIZE 80

surface_t offscreenSurf;
surface_t offscreenSurfZ;
T3DViewport viewportOffscreen;

// TODO Game data structures

// TODO Game state (intro, menu, playing)
// TODO Current level
// TODO Game mechanics:
// 	- attackers gather around each console, destroying it (screen glitches, etc.)
//	- player positions in front of a console and presses A-A-A-... to battle attackers
//	- player can reset the current console (removes a lot of attackers) --> each console once? good timing (gauge, visual hint color/size/movement, ...) ??
// 	- player can power cycle the console <n> times per level max. malus system (stuck console, unusable during <n> seconds) ??
//	- goal: survive a given time? heal all consoles?
//	- position in front of a console by plugging the (sole) controller in the corresponding port?
//		- joypad_is_connected on every frame + use the correct port
//		- is it ok to unplug/plug when console is running / probing?
//		- message if too many controllers plugged (pause game)
//		- 
// TODO Sign IPL3 ??
// TODO High persistence:
//	- Variables in .persistent
// 	- Count for each actor ? --> to know how much was lost during power off ??
//	- Spread across multiple heaps
//	- Target high persistence areas (0x80400000, ...)
// TODO Dev mode (L+R+Z):
//	- Force memory decay manually ?
//	- Select current console with D-Pad
// TODO Need to clear/invalidate replicas when deleting an actor !!

// TODO on reset, animate screen bars ON THE SELECTED CONSOLE --> add bars on the OFFSCREEN surface + remove noise !


typedef enum {
	INTRO = 0,
	IN_GAME,
	LEVEL_CLEARED,
	FINISHED
} game_state_t;

#define TOTAL_LEVELS (4)

typedef struct {
	uint8_t consoles_count;
	uint8_t attack_rate;
	uint8_t max_resets_per_console;
	uint8_t max_power_cycles;
} level_t;

level_t levels[TOTAL_LEVELS] = {
	{ 1, 1, 2, 2 },
	{ 2, 3, 2, 1 },
	{ 3, 4, 1, 1 },
	{ 4, 5, 1, 1 }
};

#define CONSOLE_MAGIC (0x11223300)
#define CONSOLE_MASK (0xffffff00)

#define MAX_CONSOLES (4)
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
	float noise_strength;
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
int current_joypad = -1;
uint32_t held_ms;
bool in_reset;
bool wrong_joypads_count = false;
bool paused_wrong_joypads_count = false;

//volatile uint32_t reset_held __attribute__((section(".persistent")));
volatile uint32_t reset_count __attribute__((section(".persistent")));
volatile uint32_t power_cycle_count __attribute__((section(".persistent")));
volatile bool wrong_joypads_count_displayed __attribute__((section(".persistent")));
volatile game_state_t game_state __attribute__((section(".persistent")));
volatile uint8_t current_level __attribute__((section(".persistent")));
volatile uint8_t level_reset_count_per_console[MAX_CONSOLES] __attribute__((section(".persistent")));
volatile uint8_t level_power_cycle_count __attribute__((section(".persistent")));
// FIXME Counters will need heavy replication to resist long power-cycles

// Callback for NMI/Reset interrupt
static void reset_interrupt_callback(void) {
	// There's a minimum guaranteed of 200ms (up to 500ms) before the console actually resets the game
	// Reset does NOT happen if the player holds the reset button
	//reset_held = exception_reset_time();

	// TODO Destroy data in heaps ?! in persistent ram ? randomly ?
	// TODO Visual feedback? Continue rendering and count ticks in main loop ??

	if (current_joypad != -1) {
		// TODO Apply effect to selected console
		level_reset_count_per_console[current_joypad]++;
	}

	// FIXME ticks are zeroe'd when resetting the console ??
	//reset_held = TICKS_READ();
	// TODO Just set hardware counter to 0 now
	C0_WRITE_COUNT(0);
	in_reset = true;

	// Measure how long the reset button is held
	/*while (true) {
		reset_held = exception_reset_time();
	}*/
}

// This is a callback for t3d_model_draw_custom, it is used when a texture in a model is set to dynamic/"reference"
void dynamic_tex_cb(void* userData, const T3DMaterial* material, rdpq_texparms_t *tileParams, rdpq_tile_t tile) {
  if(tile != TILE0)return; // this callback can happen 2 times per mesh, you are allowed to skip calls

  surface_t *offscreenSurf = (surface_t*)userData;
  rdpq_sync_tile();

  int sHalf = OFFSCREEN_SIZE / 2;
  int sFull = OFFSCREEN_SIZE;

  // upload a slice of the offscreen-buffer, the screen in the TV model is split into 4 materials for each section
  // if you are working with a small enough single texture, you can ofc use a normal sprite upload.
  // the quadrant is determined by the texture reference set in fast64, which can be used as an arbitrary value
  switch(material->textureA.texReference) { // Note: TILE1 is used here due to CC shenanigans
    case 1: rdpq_tex_upload_sub(TILE1, offscreenSurf, NULL,  0,     0,     sHalf, sHalf); break;
    case 2: rdpq_tex_upload_sub(TILE1, offscreenSurf, NULL,  sHalf, 0,     sFull, sHalf); break;
    case 3: rdpq_tex_upload_sub(TILE1, offscreenSurf, NULL,  0,     sHalf, sHalf, sFull); break;
    case 4: rdpq_tex_upload_sub(TILE1, offscreenSurf, NULL,  sHalf, sHalf, sFull, sFull); break;
  }
}

void draw_bars(float height) {
  if(height > 0) {
	rdpq_mode_push();
	rdpq_set_mode_fill(RGBA32(0, 0, 0, 0xff));
	rdpq_fill_rectangle(0, 0, 320, height);
	rdpq_fill_rectangle(0, 240 - height, 320, 240);
	rdpq_mode_pop();
  }
}

console_t* add_console() {
	console_t* console = &consoles[consoles_count];
	debugf("add console #%d (%p)\n", consoles_count, console);
	console->id = consoles_count;
	console->scale = (T3DVec3){{ 0.05f, 0.05f, 0.05f }};
	console->rotation = (T3DVec3){{ 0.0f, 0.0f, 0.0f }};
	console->position = (T3DVec3){{ -45.0f + 10.f * consoles_count, 30.0f - (rand() % 60), 0.0f }};
	console->rot_speed = (5.0f - (rand() % 10)) * 0.02f;
	console->noise_strength = (float)rand()/(float)RAND_MAX;;
	console->displayable = &console_displayables[consoles_count];
	consoles_count++;
	return console;
}

void setup_console(console_t* console) {
	displayable_t* displayable = console->displayable;
	displayable->model = console_model;
	displayable->mat_fp = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);	// FIXME Need to free !
	rspq_block_begin();
		t3d_matrix_push(&displayable->mat_fp[frameIdx]);
		//rdpq_set_prim_color(console->color);
		//t3d_model_draw_skinned(displayable->model, &displayable->skel);
		//t3d_model_draw(displayable->model);
		// TODO the model uses the prim. color to blend between the offscreen-texture and white-noise
		uint8_t blend = (uint8_t)(console->noise_strength * 255.4f);
    	rdpq_set_prim_color(RGBA32(blend, blend, blend, 255 - blend));
		/* TODO render offscreen seen: on for each console? */
		t3d_model_draw_custom(displayable->model, (T3DModelDrawConf){
			.userData = &offscreenSurf,
			.dynTextureCb = dynamic_tex_cb,
		});
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

void load_level() {
	debugf("Loading level %d\n", current_level);
	level_t* level = &levels[current_level];

	debugf("Initializing %d consoles\n", level->consoles_count);
	for (int i=0; i<level->consoles_count; i++) {
		console_t* console = add_console();
		replicate_console(console);
		setup_console(console);
		// Position consoles
		float scale = 0.18f * powf(0.7f, level->consoles_count);
		float x_offset = 20.0f;
		float y_offset = 15.0f;
		console->scale = (T3DVec3){{ scale, scale, scale }};
		console->rotation = (T3DVec3){{ 0.0f, 0.0f, 0.0f }};
		console->rot_speed = 0.0f;
		switch (i) {
			case 0: {
				switch (level->consoles_count) {
					case 1:
						console->position = (T3DVec3){{ 0.0f, 0.0f, 0.0f }};
						break;
					case 2:
						console->position = (T3DVec3){{ -1.0f * x_offset, 0.0f, 0.0f }};
						break;
					case 3:
						console->position = (T3DVec3){{ -1.0f * x_offset, 1.0f * y_offset, 0.0f }};
						break;
					case 4:
						console->position = (T3DVec3){{ -1.0f * x_offset, -1.0f * y_offset, 0.0f }};
						break;
				}
				break;
			}
			case 1: {
				switch (level->consoles_count) {
					case 2:
						console->position = (T3DVec3){{ 1.0f * x_offset, 0.0f, 0.0f }};
						break;
					case 3:
						console->position = (T3DVec3){{ 0.0f, -1.0f * y_offset, 0.0f }};
						break;
					case 4:
						console->position = (T3DVec3){{ 1.0f * x_offset, -1.0f * y_offset, 0.0f }};
						break;
				}
				break;
			}
			case 2: {
				switch (level->consoles_count) {
					case 3:
						console->position = (T3DVec3){{ 1.0f * x_offset, 1.0f * y_offset, 0.0f }};
						break;
					case 4:
						console->position = (T3DVec3){{ -1.0f * x_offset, 1.0f * y_offset, 0.0f }};
						break;
				}
				break;
			}
			case 3: {
				switch (level->consoles_count) {
					case 4:
						console->position = (T3DVec3){{ 1.0f * x_offset, 1.0f * y_offset, 0.0f }};
						break;
				}
				break;
			}
		}
		update_console(console);
	}

	for (int i=0; i<MAX_CONSOLES; i++) {
		level_reset_count_per_console[i] = 0;
	}
	level_power_cycle_count = 0;
}

void clear_level() {
	debugf("Clearing level %d\n", current_level);
	level_t* level = &levels[current_level];
	debugf("Erasing %d/%d consoles\n", consoles_count, level->consoles_count);
	int count = consoles_count;	// FIXME We may have lost consoles during a reset !!
	for (int i=0; i<count; i++) {
		console_t* console = &consoles[i];
		erase_and_free_replicas(&heap1, console->replicas, CONSOLE_REPLICAS);
		displayable_t* displayable = console->displayable;
		free_uncached(displayable->mat_fp);
		rspq_block_free(displayable->dpl);
		memset(displayable, 0, sizeof(displayable_t));
		memset(console, 0, sizeof(console_t));
		consoles_count--;
	}
}

void update() {
	// TODO Move camera?
	t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(85.0f), 10.0f, 150.0f);
	t3d_viewport_look_at(&viewport, &camPos, &camTarget, &(T3DVec3){{0,1,0}});

	switch (game_state) {
		case INTRO: {
			if (current_joypad != -1) {
				joypad_buttons_t pressed = joypad_get_buttons_pressed(current_joypad);
				if (pressed.a || pressed.start) {
					// Load level 1
					load_level();
					game_state = IN_GAME;
				}
			}
			break;
		}
		case IN_GAME: {
			// Move models TODO
			/*for (int i=0; i<consoles_count; i++) {
				console_t* console = &consoles[i];
				console->rotation.x -= console->rot_speed * 0.2f;
				console->rotation.y -= console->rot_speed;
				// Need to update replicas with new values
				update_console(console);
			}*/

			bool cleared = false;

			// Handle inputs
			if (current_joypad != -1) {
				joypad_buttons_t pressed = joypad_get_buttons_pressed(current_joypad);
				if (pressed.a) {
					// TODO Heal current console
				}
				if (pressed.start) {
					// FIXME remove
					cleared = true;
				}
			}

			// TODO Handle end condition and change level
			if (cleared) {
				game_state = LEVEL_CLEARED;
				// TODO unload level immediately ?
				clear_level();
			}
			break;
		}
		case LEVEL_CLEARED: {
			if (current_joypad != -1) {
				joypad_buttons_t pressed = joypad_get_buttons_pressed(current_joypad);
				if (pressed.a || pressed.start) {
					// Load next level
					//clear_level();
					current_level++;
					if (current_level < TOTAL_LEVELS) {
						load_level();
						game_state = IN_GAME;
					} else {
						game_state = FINISHED;
					}
				}
			}
			break;
		}
		case FINISHED: {
			if (current_joypad != -1) {
				joypad_buttons_t pressed = joypad_get_buttons_pressed(current_joypad);
				if (pressed.start) {
					// TODO Reinitialize game data
					current_level = 0;
					game_state = INTRO;
				}
			}
			break;
		}
	}
}

void render_3d() {
	t3d_frame_start();
	t3d_viewport_attach(&viewport);

	t3d_screen_clear_color(RGBA32(80, 80, 80, 255));
	t3d_screen_clear_depth();

	t3d_light_set_ambient(colorAmbient);
	t3d_light_set_directional(0, colorDir, &lightDirVec);
	t3d_light_set_count(1);

	switch (game_state) {
		case INTRO:
			break;
		case IN_GAME: {
			for (int i=0; i<consoles_count; i++) {
				console_t* console = &consoles[i];
				t3d_mat4fp_from_srt_euler(
					&console->displayable->mat_fp[frameIdx],
					console->scale.v,
					console->rotation.v,
					console->position.v
				);
				// FIXME ??? rdpq_set_prim_color(console->color);
				rspq_block_run(console->displayable->dpl);
			}
			break;
		}
		case LEVEL_CLEARED:
			break;
		case FINISHED:
			break;
	}
}

void render_2d() {
	rdpq_sync_pipe();

	heap_stats_t stats;
	sys_get_heap_stats(&stats);
	int heap_size = stats.total;
	if (heap_size > 4*1024*1024) {
		heap_size -= 4*1024*1024;
	}
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 190, "       Resets : %ld/%d-%d-%d-%d", reset_count, level_reset_count_per_console[0], level_reset_count_per_console[1], level_reset_count_per_console[2], level_reset_count_per_console[3]);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 200, " Power cycles : %ld/%d", power_cycle_count, level_power_cycle_count);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 210, "    Heap size : %d", heap_size);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 220, "    Allocated : %d", stats.used);

	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 160, "State     : %d", game_state);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 170, "Level     : %d", current_level);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 180, "Port      : %d", current_joypad);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 190, "Joypads   : %d/%d/%d/%d", joypad_is_connected(JOYPAD_PORT_1), joypad_is_connected(JOYPAD_PORT_2), joypad_is_connected(JOYPAD_PORT_3), joypad_is_connected(JOYPAD_PORT_4));
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 200, "Consoles  : %ld", consoles_count);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 210, "Reset held: %ldms", held_ms);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 220, "FPS   : %.2f", display_get_fps());

	switch (game_state) {
		case INTRO:
			rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 40, 100, "Bla bla bla... explain game");
			break;
		case IN_GAME: {
			if (paused_wrong_joypads_count) {
				rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 40, 100, "Please plug a single joypad");
			} else if (wrong_joypads_count) {
				rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 40, 100, "NO NO, plug a single joypad");
			}
			break;
		}
		case LEVEL_CLEARED:
			rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 40, 100, "Congrats !!");
			break;
		case FINISHED:
			rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 40, 100, "FINISHED !!");
			break;
	}
}

int main(void) {
	debug_init_isviewer();
	debug_init_usblog();
	asset_init_compression(2);
    wav64_init_compression(3);
	dfs_init(DFS_DEFAULT_LOCATION);
	rdpq_init();
	joypad_init();
    timer_init();
    audio_init(32000, 3);
    mixer_init(32);

    // Initialize the random number generator, then call rand() every
    // frame so to get random behavior also in emulators.
    uint32_t seed;
    getentropy(&seed, sizeof(seed));
    srand(seed);
    register_VI_handler((void(*)(void))rand);

	debugf("Stop N Swop Test ROM\n");

	reset_type_t rst = sys_reset_type();
	// TODO Treat separately cold, lukewarm (cold with remaining data in ram), and warm boots
	debugf("Boot type: %s\n", rst == RESET_COLD ? "COLD" : "WARM");
	held_ms = (rst == RESET_COLD) ? 0 : TICKS_TO_MS(TICKS_READ());
	debugf("held_ms = %ld\n", held_ms);

	// TODO Skip restoration / force cold boot behaviour by holding R+A during startup ?
	bool forceColdBoot;
	joypad_poll();
	JOYPAD_PORT_FOREACH(port) {
		joypad_buttons_t held = joypad_get_buttons_held(port);
		if (held.a && held.r) {
			debugf("Forcing cold boot and skipping restoration\n");
			forceColdBoot = true;
		}
	}

	// TODO Also CLEAR the memory heaps ??
	if (!forceColdBoot) {
		// Restore game data from heap replicas
		consoles_count = restore(&heap1, consoles, CONSOLE_PAYLOAD_SIZE, sizeof(console_t), MAX_CONSOLES, CONSOLE_MAGIC, CONSOLE_MASK);
	} else {
		// TODO Clean all variables (held_ms, ...) and memory heaps ?!
	}

	if (consoles_count == 0) {
		//n64brew_logo();
		//libdragon_logo();

		// Initial setup
		reset_count = 0;
		power_cycle_count = 0;
		wrong_joypads_count_displayed = false;
		current_level = 0;
		game_state = INTRO;
	} else {
		// TODO Make sure enough data was recovered: current_level, game_state, ... --> REPLICAS + RESTORE !!

		// Restored at least once console: keep playing
		for (int i=0; i<consoles_count; i++) {
			console_t* console = &consoles[i];
			debugf("restored: %d\n", console->id);
			debugf("rotation: %f %f %f\n", console->rotation.x, console->rotation.y, console->rotation.z);
			debugf("position: %f %f %f\n", console->position.x, console->position.y, console->position.z);
			debugf("rot_speed: %f\n", console->rot_speed);
			debugf("noise_strength: %f\n", console->noise_strength);
			console->displayable = &console_displayables[i];
			// Recreate replicas (alternative would be to keep replicas as-is)
			replicate_console(console);
		}
		if (rst == RESET_COLD) {
			power_cycle_count++;
			level_power_cycle_count++;
			// TODO Handle too many power cycles in level --> game over
		} else {
			reset_count++;
			//level_reset_count_per_console++;
			// Counter was already incremented in reset IRQ handler
			// TODO Handle too many resets in level --> game over? penalty?
		}
	}
	
	display_init(RESOLUTION_320x240, DEPTH_16_BPP, FB_COUNT, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS);
	
	offscreenSurf = surface_alloc(FMT_RGBA16, OFFSCREEN_SIZE, OFFSCREEN_SIZE);
	offscreenSurfZ = surface_alloc(FMT_RGBA16, OFFSCREEN_SIZE, OFFSCREEN_SIZE);

	t3d_init((T3DInitParams){});
	viewport = t3d_viewport_create_buffered(FB_COUNT);
  	rdpq_text_register_font(FONT_BUILTIN_DEBUG_MONO, rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO));

	t3d_vec3_norm(&lightDirVec);

	frameIdx = 0;

	console_model = t3d_model_load("rom:/crt.t3dm");

	viewportOffscreen = t3d_viewport_create_buffered(FB_COUNT);
  	t3d_viewport_set_area(&viewportOffscreen, 0, 0, OFFSCREEN_SIZE, OFFSCREEN_SIZE);

	T3DModel *brew = t3d_model_load("rom:/brew_logo.t3dm");
	
	rspq_block_begin();
	t3d_model_draw(brew);
	rspq_block_t *dplBrew = rspq_block_end();

    T3DVec3 offscreenCamPos = {{0, 0.0f, 50.0f}};
    T3DVec3 offscreenCamTarget = {{0, 0.0f, 0}};
    uint8_t colorAmbient[4] = {0xff, 0xff, 0xff, 0xFF};
    float scale = 0.08f;

    T3DMat4FP *mtx = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);

	// Happens only on reset
	// Load model for each console
	for (int i=0; i<consoles_count; i++) {
		setup_console(&consoles[i]);
	}
	
	// Reset IRQ handler
	register_RESET_handler(reset_interrupt_callback);

	update();
	while (true) {
		frameIdx = (frameIdx + 1) % FB_COUNT;

		// Identify the current joypad port and make sure only one is plugged
		int ports = 0;
		JOYPAD_PORT_FOREACH(port) {
			if (joypad_is_connected(port)) {
				ports++;
			}
		}
		wrong_joypads_count = ports > 1;
		/*if (wrong_joypads_count) {
			current_joypad = -1;
			// TODO Pause game and display message
			//debugf("Please plug only one joypad\n");
			if (!wrong_joypads_count_displayed) {
				paused_wrong_joypads_count = true;
				wrong_joypads_count_displayed = true;
			}
		} else {*/
			paused_wrong_joypads_count = false;
			current_joypad = -1;
			JOYPAD_PORT_FOREACH(port) {
				if (current_joypad == -1 && joypad_is_connected(port)) {
					current_joypad = port;
				}
			}
		//}

		joypad_poll();
		if (!paused_wrong_joypads_count) {
			update();
		}

		// ======== Draw (Offscreen) ======== //
    	// Render the offscreen-scene first, for that we attach the extra buffer instead of the screen one
		rdpq_attach_clear(&offscreenSurf, &offscreenSurfZ);
		t3d_frame_start();
		t3d_viewport_attach(&viewportOffscreen);
		
		// TODO Draw only for the current console !!
		if (in_reset) {
			// TODO Also remove noise quickly !
			//draw_bars(TICKS_TO_MS(TICKS_READ()));
			rdpq_clear(RGBA32(0xff, 0, 0, 0xff));
		} else {
			t3d_viewport_set_projection(&viewportOffscreen, T3D_DEG_TO_RAD(85.0f), 20.0f, 160.0f);
			t3d_viewport_look_at(&viewportOffscreen, &offscreenCamPos, &offscreenCamTarget, &(T3DVec3){{0,1,0}});
			t3d_viewport_attach(&viewportOffscreen);
			t3d_light_set_ambient(colorAmbient);
			t3d_light_set_count(0);
			
			t3d_mat4fp_from_srt_euler(&mtx[frameIdx],
				(float[3]){scale, scale, scale},
				(float[3]){0.0f, 0, 0},
				(float[3]){0, 0, 0}
			);
			t3d_matrix_push(&mtx[frameIdx]);
			t3d_model_draw(brew);
			t3d_matrix_pop(1);
		}

		rdpq_detach();

		rdpq_attach(display_get(), display_get_zbuf());
		render_3d();
		render_2d();
		rdpq_detach_show();
	}

	// TODO 
	//rdpq_text_unregister_font(FONT_BILLBOARD);
    //rdpq_font_free(fontbill);
	//t3d_model_free(model);

  	t3d_destroy();

	surface_free(&offscreenSurf);
	surface_free(&offscreenSurfZ);

	display_close();
	return 0;
}
