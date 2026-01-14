#include <string.h>
#include <stdint.h>
#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include <t3d/t3dskeleton.h>
#include <t3d/tpx.h>

#include "logo.h"
#include "persistence.h"

#include "pc64.h"

void debug_uart(char* str) {
	strcpy(write_buf, str);
	pc64_uart_write((const uint8_t *)write_buf, strlen(write_buf));
}


#define FB_COUNT 3
#define OFFSCREEN_SIZE 80
#define MUSIC_CHANNEL 4
#define SFX_CHANNEL 0
#define FONT_HALODEK 2

T3DViewport viewport;
T3DVec3 camPos = {{ 0.0f, 10.0f, 60.0f }};
T3DVec3 camTarget = {{0,0,0}};
uint8_t colorAmbient[4] = {80, 80, 100, 0xFF};
uint8_t colorDir[4]     = {0xEE, 0xAA, 0xAA, 0xFF};
T3DVec3 lightDirVec = {{-1.0f, 1.0f, 1.0f}};
int frameIdx = 0;

T3DModel* console_model;
T3DModel* n64_model;

static sprite_t* bg_pattern;
static sprite_t* bg_gradient;

static float frametime;
static float gtime;

static xm64player_t music;
static wav64_t sfx_blip;


static sprite_t* logo_n64;
static sprite_t* logo_saturn;
static sprite_t* logo_playstation;

static sprite_t* spr_a;
static sprite_t* spr_b;
static sprite_t* spr_c_up;
static sprite_t* spr_c_down;
static sprite_t* spr_progress;
static sprite_t* spr_circlemask;

static sprite_t* spr_swirl;

static rdpq_font_t *font_halo_dek;

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
	uint16_t attack_rate;
	float attack_grace_pediod;
	float overheat_pediod;
	uint8_t max_resets_per_console;
	uint8_t max_power_cycles;
	uint8_t duration;
} level_t;

const level_t levels[TOTAL_LEVELS] = {
	{ 1, 9900, 1.0f, 8.0f, 2, 2, 20 },
	{ 2, 9900, 1.0f, 8.0f, 2, 1, 30 },
	{ 3, 9800, 0.8f, 6.0f, 1, 1, 45 },
	{ 4, 9800, 0.8f, 6.0f, 1, 1, 60 }
};

#define CONSOLE_MAGIC (0x11223300)
#define CONSOLE_MASK (0xffffff00)

#define MAX_CONSOLES (4)
#define CONSOLE_REPLICAS (10)

// Struct to hold runtime stuff (model, display list, ...)
typedef struct {
    // CRT model
	T3DModel* model;
    T3DMat4FP* mat_fp;
	T3DSkeleton skel;
	int bone;
    rspq_block_t* dpl;
    // Console model
	T3DModel* model2;
    T3DMat4FP* mat_fp2;
    rspq_block_t* dpl2;
	// CRT screen
	surface_t offscreen_surf;
	surface_t offscreen_surf_z;
} displayable_t;

typedef struct {
	uint32_t id;
    T3DVec3 scale;
    T3DVec3 rotation;
    T3DVec3 position;
	float rot_speed;
	// TODO T3DSkeleton, T3DAnim, wav64_t, c2AABB, ...
	// Exclude remaining fields from replication
	char __exclude;
	displayable_t* displayable;	// Link to the displayable data is stale and must updated when restoring
	void* replicas[CONSOLE_REPLICAS];	// Replica pointers may be stale and must be updated when restoring
} console_t;

#define CONSOLE_PAYLOAD_SIZE (sizeof(console_t)-(sizeof(console_t)-offsetof(console_t, __exclude)))



#define ATTACKER_MAGIC (0x44556600)
#define ATTACKER_MASK (0xffffff00)
#define ATTACKER_REPLICAS (10)

typedef enum {
	SATURN = 0,
	PLAYSTATION
} rival_t;

#define TOTAL_RIVALS (2)

typedef enum {
	BTN_A = 0,
	BTN_B,
	BTN_C_UP,
	BTN_C_DOWN,
} queue_button_t;

#define TOTAL_BUTTONS (4)

#define QUEUE_LENGTH (4)

typedef struct {
	queue_button_t buttons[QUEUE_LENGTH];
	uint8_t start;
	uint8_t end;
} attack_queue_t;

typedef struct {
	uint32_t id;
	bool spawned;
	rival_t rival_type;
	attack_queue_t queue;
	uint8_t level;
	float last_attack;
	// TODO Vary strength (requires longer buttons presses? attacks faster? ...)
	// Exclude remaining fields from replication
	char __exclude;
	void* replicas[ATTACKER_REPLICAS];
} attacker_t;

#define ATTACKER_PAYLOAD_SIZE (sizeof(attacker_t)-(sizeof(attacker_t)-offsetof(attacker_t, __exclude)))



#define OVERHEAT_MAGIC (0x77889900)
#define OVERHEAT_MASK (0xffffff00)
#define OVERHEAT_REPLICAS (10)

typedef struct {
	uint32_t id;
	int overheat_level;
	float last_overheat;
	// Exclude remaining fields from replication
	char __exclude;
	void* replicas[OVERHEAT_REPLICAS];
} overheat_t;

#define OVERHEAT_PAYLOAD_SIZE (sizeof(overheat_t)-(sizeof(overheat_t)-offsetof(overheat_t, __exclude)))


// TODO Static or dynamic allocation ?
// TODO Dynamic arrays for consoles, attackers, overheat, ...
console_t consoles[MAX_CONSOLES];
displayable_t console_displayables[MAX_CONSOLES];

// TODO Need to restore attackers and queues too (but partially ? truncated depending on hold time when using reset ?)
attacker_t console_attackers[MAX_CONSOLES];
overheat_t console_overheat[MAX_CONSOLES];


// One particle system per console
uint32_t particleCountMax = 72;
typedef struct {
	uint32_t particleCount;
	TPXParticleS8* buffer;
	T3DMat4FP* mat_fp;
	float tpx_time;
	float timeTile;
	int currentPart;
} particles_t;
static particles_t console_particles[MAX_CONSOLES];

uint32_t consoles_count = 0;
int current_joypad = -1;
float holding = 0.0f;
uint32_t held_ms;
reset_type_t rst;
volatile bool in_reset;
bool wrong_joypads_count = false;
bool paused_wrong_joypads_count = false;
uint32_t tv_type;
uint32_t vi_period;


int restored_consoles_count;
int restored_attackers_count;
int restored_overheat_count;

// TODO game_data_t { consoles, level, current screen, counters, ...} --> replicated with highest persistence !!!

// TODO Replicate game state

//volatile uint32_t reset_held __attribute__((section(".persistent")));
volatile uint32_t reset_count __attribute__((section(".persistent")));
volatile uint32_t power_cycle_count __attribute__((section(".persistent")));
volatile bool wrong_joypads_count_displayed __attribute__((section(".persistent")));
volatile game_state_t game_state __attribute__((section(".persistent")));
volatile uint8_t current_level __attribute__((section(".persistent")));
volatile uint8_t level_reset_count_per_console[MAX_CONSOLES] __attribute__((section(".persistent")));
volatile uint8_t level_power_cycle_count __attribute__((section(".persistent")));
// FIXME Counters will need heavy replication to resist long power-cycles
volatile float level_timer __attribute__((section(".persistent")));
volatile int reset_console __attribute__((section(".persistent")));


/** @brief VI period for showing one NTSC and MPAL picture in ms. */
#define VI_PERIOD_NTSC_MPAL                 ((float)1000/60)
/** @brief VI period for showing one PAL picture in ms. */
#define VI_PERIOD_PAL                       ((float)1000/50)

static void monitor_reset_grace_period(void) {
	// If a reset has occured and its the last VI interrupt before RESET_TIME_LENGTH grace period, stop all work and exit
	if(/*exception_reset_time()*/TICKS_SINCE(0) + vi_period >= RESET_TIME_LENGTH) {
		die();
	}
}

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
	reset_console = current_joypad;

	// Register VI handler to shutdown hardware after grace period
	register_VI_handler((void(*)(void))monitor_reset_grace_period);

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

/*void draw_bars(float height) {
  if(height > 0) {
	rdpq_mode_push();
	rdpq_set_mode_fill(RGBA32(0, 0, 0, 0xff));
	rdpq_fill_rectangle(0, 0, 320, height);
	rdpq_fill_rectangle(0, 240 - height, 320, 240);
	rdpq_mode_pop();
  }
}*/

void replicate_console(console_t* console) {
	debugf("replicate console #%d\n", console->id);
	replicate(&heap1, CONSOLE_MAGIC | console->id, console, CONSOLE_PAYLOAD_SIZE, CONSOLE_REPLICAS, true, true, console->replicas);
	debugf("replicas: %p %p %p %p\n", console->replicas[0], console->replicas[1], console->replicas[2], console->replicas[3]);
}

void update_console(console_t* console) {
	//debugf("updating console replicas: %p %p %p %p\n", console->replicas[0], console->replicas[1], console->replicas[2], console->replicas[3]);
	update_replicas(console->replicas, console, CONSOLE_PAYLOAD_SIZE, CONSOLE_REPLICAS, true);
}

console_t* add_console() {
	console_t* console = &consoles[consoles_count];
	debugf("add console #%d (%p)\n", consoles_count, console);
	console->id = consoles_count;
	console->scale = (T3DVec3){{ 0.05f, 0.05f, 0.05f }};
	console->rotation = (T3DVec3){{ 0.0f, 0.0f, 0.0f }};
	console->position = (T3DVec3){{ -45.0f + 10.f * consoles_count, 30.0f - (rand() % 60), 0.0f }};
	console->rot_speed = (5.0f - (rand() % 10)) * 0.02f;
	console->displayable = &console_displayables[consoles_count];
	consoles_count++;
	replicate_console(console);
	return console;
}

void setup_console(int i, console_t* console) {
	displayable_t* displayable = console->displayable;
	displayable->model = console_model;	//t3d_model_load("rom:/crt.t3dm");	//console_model; // TODO FREE
	displayable->mat_fp = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);
	displayable->skel = t3d_skeleton_create_buffered(displayable->model, 1 /* FIXME FB_COUNT*/);
	displayable->bone = t3d_skeleton_find_bone(&displayable->skel, "console");
	displayable->offscreen_surf = surface_alloc(FMT_RGBA16, OFFSCREEN_SIZE, OFFSCREEN_SIZE);
	displayable->offscreen_surf_z = surface_alloc(FMT_RGBA16, OFFSCREEN_SIZE, OFFSCREEN_SIZE);
	rspq_block_begin();
		//t3d_matrix_push(&displayable->mat_fp[frameIdx]);
		// TODO the model uses the prim. color to blend between the offscreen-texture and white-noise
		t3d_model_draw_custom(displayable->model, (T3DModelDrawConf){
			.userData = &displayable->offscreen_surf,
			.dynTextureCb = dynamic_tex_cb,
			.matrices = displayable->skel.bufferCount == 1
				? displayable->skel.boneMatricesFP
				: (const T3DMat4FP*)t3d_segment_placeholder(T3D_SEGMENT_SKELETON)
		});
		//t3d_matrix_pop(1);
	displayable->dpl = rspq_block_end();

	displayable->model2 = n64_model;
	displayable->mat_fp2 = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);
	rspq_block_begin();
		t3d_model_draw(displayable->model2);
	displayable->dpl2 = rspq_block_end();
	// Particles
	particles_t* particles = &console_particles[i];
	uint32_t allocSize = sizeof(TPXParticleS8) * particleCountMax / 2;
	debugf("allocSize=%d\n", allocSize);
	particles->buffer = malloc_uncached(allocSize);	// TODO memset to 0 ??
	memset(particles->buffer, 0, allocSize);
	particles->mat_fp = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);
}


void replicate_attacker(attacker_t* attacker) {
	debugf("replicate attacker #%d\n", attacker->id);
	replicate(&heap2, ATTACKER_MAGIC | attacker->id, attacker, ATTACKER_PAYLOAD_SIZE, ATTACKER_REPLICAS, true, true, attacker->replicas);
	debugf("replicas: %p %p %p %p\n", attacker->replicas[0], attacker->replicas[1], attacker->replicas[2], attacker->replicas[3]);
}

void update_attacker(attacker_t* attacker) {
	//debugf("updating attacker replicas: %p %p %p %p\n", attacker->replicas[0], attacker->replicas[1], attacker->replicas[2], attacker->replicas[3]);
	update_replicas(attacker->replicas, attacker, ATTACKER_PAYLOAD_SIZE, ATTACKER_REPLICAS, true);
}

void shrink_attacker(int idx) {
	attacker_t* attacker = &console_attackers[idx];
	if (attacker->spawned && attacker->level > 0) {
		// If level was QUEUE_LENGTH, avoid immediate reaction
		if (attacker->level == QUEUE_LENGTH) {
			attacker->last_attack = gtime;
			console_overheat[idx].last_overheat = gtime;
		}
		attacker->level--;
		attacker->queue.start = (attacker->queue.start + 1) % QUEUE_LENGTH;
		debugf("shrink %d: level=%d start=%d\n", idx, attacker->level, attacker->queue.start);
		if (attacker->level == 0) {
			attacker->spawned = false;
			debugf("shrink %d: despawn\n", idx);
		}
		update_attacker(attacker);
	}
}

void grow_attacker(int idx) {
	attacker_t* attacker = &console_attackers[idx];
	if (attacker->spawned && attacker->level < QUEUE_LENGTH) {
		attacker->level++;
		attacker->queue.buttons[attacker->queue.end] = (rand() % TOTAL_BUTTONS);
		attacker->queue.end = (attacker->queue.end + 1) % QUEUE_LENGTH;
		attacker->last_attack = gtime;
		console_overheat[idx].last_overheat = gtime;
		debugf("grow %d: level=%d end=%d\n", idx, attacker->level, attacker->queue.end);
		update_attacker(attacker);
	}
}

void spawn_attacker(int idx) {
	attacker_t* attacker = &console_attackers[idx];
	attacker->id = idx;
	attacker->spawned = true;
	attacker->rival_type = (rand() % TOTAL_RIVALS);
	attacker->level = 0;
	attacker->last_attack = 0.0f;
	attacker->queue.start = 0;
	attacker->queue.end = 0;
	debugf("spawn %d: level=%d start=%d end=%d\n", idx, attacker->level, attacker->queue.start, attacker->queue.end);
	replicate_attacker(attacker);
	grow_attacker(idx);
}

void reset_attacker(int idx) {
	attacker_t* attacker = &console_attackers[idx];
	attacker->spawned = false;
	attacker->level = 0;
	attacker->last_attack = 0.0f;
	attacker->queue.start = 0;
	attacker->queue.end = 0;
	debugf("reset %d: level=%d start=%d end=%d\n", idx, attacker->level, attacker->queue.start, attacker->queue.end);
}

queue_button_t get_attacker_button(int idx, int i) {
	attack_queue_t* queue = &console_attackers[idx].queue;
	if (queue->start < queue->end) {
		return queue->buttons[queue->start + i];
	} else if (i <= (TOTAL_BUTTONS-1-queue->start)) {
		return queue->buttons[queue->start + i];
	} else {
		return queue->buttons[i - (TOTAL_BUTTONS-queue->start)];
	}
}


void replicate_overheat(overheat_t* overheat) {
	debugf("replicate overheat #%d\n", overheat->id);
	replicate(&heap2, OVERHEAT_MAGIC | overheat->id, overheat, OVERHEAT_PAYLOAD_SIZE, OVERHEAT_REPLICAS, true, true, overheat->replicas);
	debugf("replicas: %p %p %p %p\n", overheat->replicas[0], overheat->replicas[1], overheat->replicas[2], overheat->replicas[3]);
}

void update_overheat(overheat_t* overheat) {
	//debugf("updating overheat replicas: %p %p %p %p\n", overheat->replicas[0], overheat->replicas[1], overheat->replicas[2], overheat->replicas[3]);
	update_replicas(overheat->replicas, overheat, OVERHEAT_PAYLOAD_SIZE, OVERHEAT_REPLICAS, true);
}

void increase_overheat(int idx) {
	overheat_t* overheat = &console_overheat[idx];
	if (console_attackers[idx].spawned && console_attackers[idx].level == QUEUE_LENGTH) {
		overheat->id = idx;
		overheat->overheat_level++;
		overheat->last_overheat = gtime;
		debugf("increase heat %d: level=%d\n", idx, overheat->overheat_level);
		// TODO Game over if reached level 4 ?
		if (overheat->overheat_level > 3) {
			debugf("GAME OVER %d\n", idx);
			overheat->overheat_level = 3;	// TODO To avoid crashing particles
		}
		// TODO replicate on first spawn ? update otherwise
		if (overheat->replicas[0] == NULL) {
			replicate_overheat(overheat);
		} else {
			update_overheat(overheat);
		}
	}
}

void decrease_overheat(int idx) {
	overheat_t* overheat = &console_overheat[idx];
	if (console_overheat[idx].overheat_level > 0) {
		overheat->overheat_level--;
		overheat->last_overheat = gtime;	// To avoid immediate increase (TODO Add grace period of a few additional seconds?)
		debugf("decrease heat %d: level=%d\n", idx, overheat->overheat_level);
		// TODO replicate on first spawn ? update otherwise
		if (overheat->replicas[0] == NULL) {
			replicate_overheat(overheat);
		} else {
			update_overheat(overheat);
		}
	}
}

void reset_overheat(int idx) {
	overheat_t* overheat = &console_overheat[idx];
	overheat->overheat_level = 0;
	overheat->last_overheat = 0.0f;
	debugf("reset %d: level=%d\n", idx, overheat->overheat_level);
}


void load_level() {
	debugf("Loading level %d\n", current_level);
	const level_t* level = &levels[current_level];

	debugf("Initializing %d consoles\n", level->consoles_count);
	for (int i=0; i<level->consoles_count; i++) {
		console_t* console = add_console();
		setup_console(i, console);
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
	level_timer = level->duration;
	gtime = 0;
}

void clear_level() {
	debugf("Clearing level %d\n", current_level);
	const level_t* level = &levels[current_level];
	debugf("Erasing %d/%d consoles\n", consoles_count, level->consoles_count);
	int count = consoles_count;	// FIXME We may have lost consoles during a reset !!
	for (int i=0; i<count; i++) {
		console_t* console = &consoles[i];
		erase_and_free_replicas(&heap1, console->replicas, CONSOLE_REPLICAS);
		displayable_t* displayable = console->displayable;
		// FIXME ! t3d_model_free(displayable->model);
		free_uncached(displayable->mat_fp);
		t3d_skeleton_destroy(&displayable->skel);
		surface_free(&displayable->offscreen_surf);
		surface_free(&displayable->offscreen_surf_z);
		rspq_block_free(displayable->dpl);
		free_uncached(displayable->mat_fp2);
		rspq_block_free(displayable->dpl2);
		memset(displayable, 0, sizeof(displayable_t));
		memset(console, 0, sizeof(console_t));
		particles_t* particles = &console_particles[i];
		free_uncached(particles->buffer);
		free_uncached(particles->mat_fp);
		memset(particles, 0, sizeof(particles_t));

		attacker_t* attacker = &console_attackers[i];
		reset_attacker(i);
		erase_and_free_replicas(&heap2, attacker->replicas, ATTACKER_REPLICAS);
		memset(attacker, 0, sizeof(attacker_t));

		overheat_t* overheat = &console_overheat[i];
		reset_overheat(i);
		erase_and_free_replicas(&heap2, overheat->replicas, OVERHEAT_REPLICAS);
		memset(overheat, 0, sizeof(overheat_t));

		consoles_count--;
	}
}

static void stop_music() {
	if (music.playing) {
		xm64player_stop(&music);
		xm64player_close(&music);
	}
}

static void play_ingame_music() {
	stop_music();
	xm64player_open(&music, "rom:/flyaway.xm64");
    xm64player_set_loop(&music, true);
    xm64player_set_vol(&music, 0.55);	// FIXME
	xm64player_play(&music, MUSIC_CHANNEL);
}

static void play_menu_music() {
	stop_music();
    xm64player_open(&music, "rom:/inmemory.xm64");
    xm64player_set_loop(&music, true);
    xm64player_set_vol(&music, 0.55);	// FIXME
	xm64player_play(&music, MUSIC_CHANNEL);
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
					play_ingame_music();
					game_state = IN_GAME;
				}
			}
			break;
		}
		case IN_GAME: {
			level_timer -= frametime;
			bool cleared = (level_timer < 0.0f);

			// TODO Play model animation --> shake when attacked ? shaking grows with attacker level?
			// Move models TODO
			/*for (int i=0; i<consoles_count; i++) {
				console_t* console = &consoles[i];
				console->rotation.x -= console->rot_speed * 0.2f;
				console->rotation.y -= console->rot_speed;
				// Need to update replicas with new values
				update_console(console);
			}*/

			// Spawn attackers and add attacks
			const level_t* level = &levels[current_level];
			for (int i=0; i<consoles_count; i++) {
				console_t* console = &consoles[i];
				attacker_t* attacker = &console_attackers[i];
				overheat_t* overheat = &console_overheat[i];
				if ((rand() % 10000) > level->attack_rate) {
					if (!attacker->spawned) {
						spawn_attacker(i);
					} else if (attacker->last_attack + level->attack_grace_pediod <= gtime) {
						grow_attacker(i);
					}
				}
				if (attacker->spawned && attacker->level == QUEUE_LENGTH && overheat->last_overheat + level->overheat_pediod <= gtime) {
					increase_overheat(i);
				}
				// TODO Decrease current console's heat on reset (the one that was selected when pressing reset!)
			}

			// Handle inputs
			if (current_joypad != -1) {
				joypad_buttons_t pressed = joypad_get_buttons_pressed(current_joypad);
				attacker_t* attacker = &console_attackers[current_joypad];
				if (attacker->spawned) {
					queue_button_t btn = get_attacker_button(current_joypad, 0);
					joypad_buttons_t down = joypad_get_buttons(current_joypad);
					bool held;
					switch (btn) {
						case BTN_A:
							held = down.a && !down.b && !down.c_up && !down.c_down;
							break;
						case BTN_B:
							held = down.b && !down.a && !down.c_up && !down.c_down;
							break;
						case BTN_C_UP:
							held = down.c_up && !down.a && !down.b && !down.c_down;
							break;
						case BTN_C_DOWN:
							held = down.c_down && !down.a && !down.b && !down.c_up;
							break;
					}
					if (held) {
						holding += frametime;
						if (holding >= 1.0f) {	// TODO Threshold depending on enemy strength
							shrink_attacker(current_joypad);
							holding = 0;
						}
					} else {
						holding = 0;
					}
				}

				if (pressed.r) {
					// Spawn attacker
					int idx = current_joypad;
					if (!console_attackers[idx].spawned) {
						spawn_attacker(idx);
					} else {
						grow_attacker(idx);
					}
				}
				if (pressed.l) {
					// Shrink attacker
					int idx = current_joypad;
					if (console_attackers[idx].spawned) {
						shrink_attacker(idx);
					}
				}
				if (pressed.d_up) {
					// Increase heat
					int idx = current_joypad;
					increase_overheat(idx);
				}
				if (pressed.d_down) {
					// Decrease heat
					int idx = current_joypad;
					if (console_overheat[idx].overheat_level > 0) {
						decrease_overheat(idx);
					}
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
				// TODO Keep level displayed for a few seconds, clear when loading the next level
				clear_level();
				play_menu_music();
				wav64_play(&sfx_blip, SFX_CHANNEL);
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
						play_ingame_music();
						game_state = IN_GAME;
					} else {
						wav64_play(&sfx_blip, SFX_CHANNEL);
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
					wav64_play(&sfx_blip, SFX_CHANNEL);
				}
			}
			break;
		}
	}
}

static void draw_bg(sprite_t* pattern, sprite_t* gradient, float offset) {
  rdpq_mode_push();
  
  rdpq_set_mode_standard();
  rdpq_mode_begin();
    rdpq_mode_blender(0);
    rdpq_mode_alphacompare(0);
    rdpq_mode_combiner(RDPQ_COMBINER2(
      (TEX0,0,TEX1,0), (0,0,0,1),
      (COMBINED,0,PRIM,0), (0,0,0,1)
    ));
    rdpq_mode_dithering(DITHER_BAYER_BAYER);
    rdpq_mode_filter(FILTER_BILINEAR);
  rdpq_mode_end();

  float brightness = 0.75f;
  rdpq_set_prim_color((color_t){0xcc*brightness, 0xcc*brightness, 0xff, 0xff});

  offset = fmodf(offset, 64.0f);
  rdpq_texparms_t param_pattern = {
    .s = {.repeats = REPEAT_INFINITE, .mirror = true, .translate = offset, .scale_log = -1},
    .t = {.repeats = REPEAT_INFINITE, .mirror = true, .translate = offset, .scale_log = -1},
  };
  rdpq_texparms_t param_grad = {
    .s = {.repeats = REPEAT_INFINITE},
    .t = {.repeats = 1, .scale_log = 2},
  };
  rdpq_tex_multi_begin();
    rdpq_sprite_upload(TILE0, pattern, &param_pattern);
    rdpq_sprite_upload(TILE1, gradient, &param_grad);
  rdpq_tex_multi_end();

  rdpq_texture_rectangle(TILE0, 0,0, display_get_width(), display_get_height(), 0, 0);

  rdpq_mode_pop();
}

static void drawprogress(int x, int y, float scale, float progress, color_t col)
{
    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_combiner(RDPQ_COMBINER2(
        (TEX1,0,PRIM,0),  (0,0,0,TEX0),
        (0,0,0,COMBINED), (0,0,0,TEX1)
    ));
    rdpq_set_prim_color(col);
    rdpq_mode_alphacompare((1.0f-progress)*255.0f);
    rdpq_tex_multi_begin();
        rdpq_sprite_upload(TILE0, spr_circlemask, NULL);
        rdpq_sprite_upload(TILE1, spr_progress, NULL);
    rdpq_tex_multi_end();
    rdpq_texture_rectangle_scaled(TILE0, x, y, x+(32*scale), y+(32*scale), 0, 0, 32, 32);
    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    rdpq_mode_combiner(RDPQ_COMBINER1((TEX0,0,PRIM,0), (TEX0,0,PRIM,0)));
}


static void gradient_smoke(uint8_t *color, float t, int heat_level) {
    t = fminf(1.0f, fmaxf(0.0f, t));
	// Dark gray to light gray
	color[0] = (uint8_t)(50 * heat_level + 100 * t);
	color[1] = (uint8_t)(50 + 100 * t);
	color[2] = (uint8_t)(50 + 100 * t);
}


/**
 * Particle system for a fire effect.
 * This will simulate particles over time by moving them up and changing their color.
 * The current position is used to spawn new particles, so it can move over time leaving a trail behind.
 */
static void simulate_particles_smoke(particles_t* particles, int heat_level, float posX, float posZ) {
  int p = particles->currentPart / 2;
  if(particles->currentPart % (1+(rand() % 3)) == 0) {
    int8_t *ptPos  = tpx_buffer_s8_get_pos(particles->buffer, p);
    int8_t *size   = tpx_buffer_s8_get_size(particles->buffer, p);
    uint8_t *color = tpx_buffer_s8_get_rgba(particles->buffer, p);

    ptPos[0] = posX + (rand() % 16) - 8;
    ptPos[1] = -126;
    gradient_smoke(color, 0, heat_level);
    color[3] = ((PhysicalAddr(ptPos) % 8) * 32);

    ptPos[2] = posZ + (rand() % 16) - 8;
    *size = 60 + (rand() % 10);
  }
  particles->currentPart = (particles->currentPart + 1) % particles->particleCount;

  // move all up by one unit
  for (int i = 0; i < particles->particleCount/2; i++) {
    gradient_smoke(particles->buffer[i].colorA, (particles->buffer[i].posA[1] + 127) / 150.0f, heat_level);
    gradient_smoke(particles->buffer[i].colorB, (particles->buffer[i].posB[1] + 127) / 150.0f, heat_level);

    particles->buffer[i].posA[1] += 1;
    particles->buffer[i].posB[1] += 1;
    if(particles->currentPart % 4 == 0) {
      particles->buffer[i].sizeA -= 2;
      particles->buffer[i].sizeB -= 2;
      if(particles->buffer[i].sizeA < 0)	particles->buffer[i].sizeA = 0;
      if(particles->buffer[i].sizeB < 0)	particles->buffer[i].sizeB = 0;
    }
  }
}

static void drawsmoke(particles_t* particles, T3DVec3 position, int heat_level) {
	//particleRot = (T3DVec3){{0,0,0}};
	particles->tpx_time += frametime * 1.0f;
	particles->timeTile += frametime * 25.1f;
	particles->particleCount = 24 * heat_level;	// TODO Change scale too? FIXME Handle size change ??
	// FIXME console position !
	float posX = position.x;// fm_cosf(tpx_time) * 80.0f;
	float posZ = position.z;//fm_sinf(2*tpx_time) * 40.0f;

	rdpq_mode_push();

	simulate_particles_smoke(particles, heat_level, posX, posZ);
	//particleMatScale = (T3DVec3){{0.9f, partMatScaleVal, 0.9f}};
	//particlePos.y = partMatScaleVal * 130.0f;
	rdpq_set_env_color((color_t){0xFF, 0xFF, 0xFF, 0xFF});
	//isSpriteRot = true;


    rdpq_sync_pipe();
    rdpq_sync_tile();
    rdpq_sync_load();
    rdpq_set_mode_standard();
    rdpq_mode_antialias(AA_NONE);
    rdpq_mode_zbuf(true, true);
    rdpq_mode_zoverride(true, 0, 0);
    rdpq_mode_filter(FILTER_POINT);
    rdpq_mode_alphacompare(10);

    rdpq_mode_combiner(RDPQ_COMBINER1((PRIM,0,TEX0,0), (0,0,0,TEX0)));


    // Upload texture for the following particles.
    // The ucode itself never loads or switches any textures,
    // so you can only use what you have uploaded before in a single draw call.
    rdpq_texparms_t p = {};
    p.s.repeats = REPEAT_INFINITE;
    p.t.repeats = REPEAT_INFINITE;
    // Texture UVs are internally always mapped to 8x8px tiles across the entire particle.
    // Even with non-uniform scaling, it is squished into that space.
    // In order to use differently sized textures (or sections thereof) adjust the scale here.
    // E.g.: scale_log = 4px=1, 8px=0, 16px=-1, 32px=-2, 64px=-3
    // This also means that you are limited to power-of-two sizes for a section of a texture.
    // You can however still have textures with a multiple of a power-of-two size on one axis.
    int logScale =  - __builtin_ctz(spr_swirl->height / 8);
    p.s.scale_log = logScale;
    p.t.scale_log = logScale;

    // For sprite sheets that animate a rotation, we can activate mirroring.
    // to only require half rotation to be animated.
    // This works by switching over to the double-mirrored section and repeating the animation,
    // which is handled internally in the ucode for you if enabled.
    p.s.mirror = true;
    p.t.mirror = true;
    rdpq_sprite_upload(TILE0, spr_swirl, &p);

    tpx_state_from_t3d();
    
	t3d_mat4fp_from_srt_euler(&particles->mat_fp[frameIdx],
		(float[3]){ 3, 3, 3 },
		(float[3]){ 0.0f, 0.0f, 0.0f },
		(float[3]){ 0.0f, 500.0f, 0.0f }
	);
	tpx_matrix_push(&particles->mat_fp[frameIdx]);
	
	float scale = 0.5f * heat_level / consoles_count;
    tpx_state_set_scale(scale, scale);

    float tileIdx = fm_floorf(particles->timeTile) * 32;
    if(tileIdx >= 512)	particles->timeTile = 0;

	tpx_state_set_tex_params((int16_t)tileIdx, 8);

    tpx_particle_draw_tex_s8(particles->buffer, particles->particleCount);

	tpx_matrix_pop(1);
	
	rdpq_mode_pop();
}

void render_offscreen() {
	if (game_state == IN_GAME) {
		for (int i=0; i<consoles_count; i++) {
			console_t* console = &consoles[i];
			// ======== Draw (Offscreen) ======== //
			// Render the offscreen-scene first, for that we attach the extra buffer instead of the screen one
			rdpq_attach_clear(&console->displayable->offscreen_surf, &console->displayable->offscreen_surf_z);

			// TODO Draw only for the current console !!
			if (in_reset && current_joypad == i) {
				// TODO Also remove noise quickly !
				//draw_bars(TICKS_TO_MS(TICKS_READ()));
				rdpq_clear(RGBA32(0xff, 0, 0, 0xff));
			} else {
				attacker_t* attacker = &console_attackers[i];
				int x = 0;
				int y = 0;
				float s = 1.0f - (.25f * attacker->level);
				if (s > 0.0f) {
					rdpq_sprite_blit(logo_n64, x, y, &(rdpq_blitparms_t) {
						.scale_x = s, .scale_y = s,
					});
				}
				if (attacker->spawned) {
					// Draw attacker logo (size grows with attacker level)
					x = 80 - 20 * attacker->level;
					y = 80 - 20 * attacker->level;
					s = .25f * attacker->level;
					sprite_t* spr;
					switch (attacker->rival_type) {
						case SATURN:
							spr = logo_saturn;
							break;
						case PLAYSTATION:
							spr = logo_playstation;
							break;
					}
					rdpq_sprite_blit(spr, x, y, &(rdpq_blitparms_t) {
						.scale_x = s, .scale_y = s,
					});
				}
			}

			rdpq_detach();
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
			draw_bg(bg_pattern, bg_gradient, gtime * 4.0f);
			for (int i=0; i<consoles_count; i++) {
				//rdpq_sync_pipe();
				console_t* console = &consoles[i];
				t3d_skeleton_update(&console->displayable->skel);
				t3d_mat4fp_from_srt_euler(
					&console->displayable->mat_fp[frameIdx],
					console->scale.v,
					console->rotation.v,
					//(float[3]){ 0.0f, M_PI/4.0f, 0.0f },
					console->position.v
				);
				// FIXME ??? rdpq_set_prim_color(console->color);
				t3d_matrix_push(&console->displayable->mat_fp[frameIdx]);
      			t3d_skeleton_use(&console->displayable->skel);
				//rdpq_set_prim_color(RGBA32(0, 255, 0, 255));

				overheat_t* overheat = &console_overheat[i];
				float noise_strength = 0.2f * (1 + overheat->overheat_level);
				uint8_t blend = (uint8_t)(noise_strength * 255.4f);
    			rdpq_set_prim_color(RGBA32(blend, blend, blend, 255 - blend));
				rspq_block_run(console->displayable->dpl);
				
				if(console->displayable->bone >= 0) {
					//rdpq_sync_pipe();
  					rdpq_mode_push();
					//rdpq_sync_pipe();
					float s = 8.0f;
					t3d_mat4fp_from_srt_euler(
						&console->displayable->mat_fp2[frameIdx],
						//console->scale.v,
						(float[3]){ console->scale.x * s, console->scale.y * s, console->scale.z * s },
						(float[3]){ 0.0f, 0.0f, 0.0f },
						(float[3]){ 0.0f, 0.0f, 0.0f }
					);
					// to attach another model, simply use a bone form the skeleton:
					t3d_matrix_push(&console->displayable->skel.boneMatricesFP[console->displayable->bone]);
					t3d_matrix_push(&console->displayable->mat_fp2[frameIdx]); // apply local matrix of the model
					//rdpq_set_prim_color(RGBA32(255, 0, 0, 255));
					//rdpq_sync_pipe();
					if (current_joypad == i) {
						rdpq_set_prim_color(RGBA32(0, 200, 0, 255));
					}
					rspq_block_run(console->displayable->dpl2);
					rdpq_sync_pipe();

					// Particles
					if (overheat->overheat_level > 0) {
						drawsmoke(&console_particles[i], console->position, overheat->overheat_level);
					}
					rdpq_sync_pipe();

					t3d_matrix_pop(2);
  					rdpq_mode_pop();
					//rdpq_sync_pipe();
				}
				t3d_matrix_pop(1);
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
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 160, "Reset console : %ld", reset_console);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 170, "    Boot type : %s", rst == RESET_COLD ? "COLD" : "WARM");
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 180, "     Restored : %ld/%ld/%ld", restored_consoles_count, restored_attackers_count, restored_overheat_count);
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

			for (int i=0; i<consoles_count; i++) {
				console_t* console = &consoles[i];
				attacker_t* attacker = &console_attackers[i];
				if (attacker->spawned) {
					// Draw queue
					T3DVec3 billboardPos = (T3DVec3){{
						console->position.v[0] - 140 * console->scale.x,
						console->position.v[1] + 270 * console->scale.x,
						console->position.v[2]
					}};
					T3DVec3 billboardScreenPos;
					t3d_viewport_calc_viewspace_pos(&viewport, &billboardScreenPos, &billboardPos);
					int x = floorf(billboardScreenPos.v[0]);
					int y = floorf(billboardScreenPos.v[1]);
					const level_t* level = &levels[current_level];
					float s = powf(0.7f, (level->consoles_count - 1));
					for (int j=0; j<attacker->level; j++) {
						queue_button_t btn = get_attacker_button(i, j);
						if (i == current_joypad && j == 0) {
							drawprogress(x - (8*s), y - (8*s), s, holding, RGBA32(255, 0, 0, 255));
						}
						sprite_t* spr;
						switch (btn) {
							case BTN_A:
								spr = spr_a;
								break;
							case BTN_B:
								spr = spr_b;
								break;
							case BTN_C_UP:
								spr = spr_c_up;
								break;
							case BTN_C_DOWN:
								spr = spr_c_down;
								break;
						}
						rdpq_sprite_blit(spr, x, y, &(rdpq_blitparms_t) {
							.scale_x = s, .scale_y = s,
						});
						x += 32 / consoles_count;
					}
				}
			}

			// Print timer
			rdpq_sync_pipe();
			rdpq_textparms_t textparms = { .align = ALIGN_CENTER, .width = 320, };
        	rdpq_text_printf(&textparms, FONT_HALODEK, 0, 30, "%d", (int) ceilf(level_timer));
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
	held_ms = TICKS_TO_MS(TICKS_READ());
	rst = sys_reset_type();
	// TODO Treat separately cold, lukewarm (cold with remaining data in ram), and warm boots
	if (rst == RESET_COLD) {
		held_ms = 0;
	}

	debug_uart("================= Hello from the N64 =================\n");

	debug_init_isviewer();
	debug_init_usblog();

	debugf("Boot type: %s\n", rst == RESET_COLD ? "COLD" : "WARM");
	debugf("held_ms = %ld\n", held_ms);

	asset_init_compression(2);
    wav64_init_compression(3);
	dfs_init(DFS_DEFAULT_LOCATION);
	rdpq_init();
	joypad_init();
    timer_init();
    audio_init(32000, 4);
    mixer_init(32);

	debug_uart("Init OK\n");

	//rdpq_debug_start();	// TODO Debug only

    // Initialize the random number generator, then call rand() every
    // frame so to get random behavior also in emulators.
    uint32_t seed;
    getentropy(&seed, sizeof(seed));
    srand(seed);
    register_VI_handler((void(*)(void))rand);

	debug_uart("Seed OK\n");

	tv_type = get_tv_type();
	vi_period = TICKS_FROM_MS(tv_type == TV_PAL ? VI_PERIOD_PAL : VI_PERIOD_NTSC_MPAL);
    //register_VI_handler((void(*)(void))monitor_reset_grace_period);

	debugf("Console War\n");

	debug_uart("Boot type OK\n");

	display_init(RESOLUTION_320x240, DEPTH_16_BPP, FB_COUNT, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS);

	debug_uart("Display init OK\n");

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

	debug_uart("Joypad poll OK\n");

	console_t restored_consoles[MAX_CONSOLES];
	attacker_t restored_attackers[MAX_CONSOLES];
	overheat_t restored_overheat[MAX_CONSOLES];

	// TODO Also CLEAR the memory heaps ??
	if (!forceColdBoot) {
		// Restore game data from heap replicas
		// FIXME Restoration is BROKEN
		// FIXME Don't restore directly in rrays --> use a dedicated array, then update item in-place !!

		restored_consoles_count = restore(&heap1, restored_consoles, CONSOLE_PAYLOAD_SIZE, sizeof(console_t), MAX_CONSOLES, CONSOLE_MAGIC, CONSOLE_MASK);
		restored_attackers_count = restore(&heap2, restored_attackers, ATTACKER_PAYLOAD_SIZE, sizeof(attacker_t), MAX_CONSOLES, ATTACKER_MAGIC, ATTACKER_MASK);
		restored_overheat_count = restore(&heap2, restored_overheat, OVERHEAT_PAYLOAD_SIZE, sizeof(overheat_t), MAX_CONSOLES, OVERHEAT_MAGIC, OVERHEAT_MASK);
	} else {
		// TODO Clean all variables (held_ms, ...) and memory heaps ?!
	}

	debug_uart("Restoration OK\n");

	if (restored_consoles_count == 0) {
		debug_uart("Entering initial boot sequence\n");
		//n64brew_logo();
		//libdragon_logo();

		// Initial setup
		consoles_count = 0;
		reset_count = 0;
		power_cycle_count = 0;
		wrong_joypads_count_displayed = false;
		current_level = 0;
		game_state = INTRO;
		debug_uart("Cold initial sequence OK\n");
	} else {
		debug_uart("Entering followup boot sequence\n");
		// TODO Make sure enough data was recovered: current_level, game_state, ... --> REPLICAS + RESTORE !!

		debugf("restored: %d consoles / %d attackers / %d overheat\n", restored_consoles_count, restored_attackers_count, restored_overheat_count);

		debugf("Restored level %d\n", current_level);
		if (current_level >= 0 && current_level < TOTAL_LEVELS) {
			const level_t* level = &levels[current_level];
			if (restored_consoles_count != level->consoles_count) {
				debugf("FAILED TO RESTORE ALL CONSOLES !!! %d != %d\n", restored_consoles_count, level->consoles_count);
				// TODO Should show game over ?
			}
		}

		// TODO Support restarting to level cleared screen ??

		// Restored at least once console: keep playing
		consoles_count = restored_consoles_count;
		for (int i=0; i<restored_consoles_count; i++) {
			uint32_t id = restored_consoles[i].id;
			console_t* console = &consoles[id];
			*console = restored_consoles[i];
			debugf("restored: %d\n", console->id);
			debugf("rotation: %f %f %f\n", console->rotation.x, console->rotation.y, console->rotation.z);
			debugf("position: %f %f %f\n", console->position.x, console->position.y, console->position.z);
			debugf("rot_speed: %f\n", console->rot_speed);
			console->displayable = &console_displayables[i];
			// Recreate replicas (alternative would be to keep replicas as-is)
			replicate_console(console);
		}
		debug_uart("Consoles restored\n");

		for (int i=0; i<restored_attackers_count; i++) {
			uint32_t id = restored_attackers[i].id;
			attacker_t* attacker = &console_attackers[id];
			*attacker = restored_attackers[i];
			debugf("restored attacker: %d\n", attacker->id);
			// TODO instead of replicating, just keep track of the resored _valid_ replicas ?
			replicate_attacker(attacker);
		}

		// TODO restored attackers MAY NOT BE ORDERED
		for (int i=0; i<restored_overheat_count; i++) {
			uint32_t id = restored_overheat[i].id;
			overheat_t* overheat = &console_overheat[id];
			*overheat = restored_overheat[i];
			debugf("restored overheat: %d\n", overheat->id);
			// TODO instead of replicating, just keep track of the resored _valid_ replicas ?
			replicate_overheat(overheat);
			
			// TODO Decrease overheat level of console depending on held_ms
			if (/*rst == RESET_WARM && */overheat->id == reset_console && overheat->overheat_level > 0) {
				debugf("DECREASE overheat of RESET CONSOLE: %d\n", overheat->id);
				decrease_overheat(id);
				if (held_ms >= 5000) {
					debugf("DECREASE AGAIN overheat of RESET CONSOLE: %d held=%d\n", overheat->id, held_ms);
					decrease_overheat(id);
				}
			}
		}

		reset_console = -1;

		if (rst == RESET_COLD) {
			debug_uart("Cold\n");
			power_cycle_count++;
			level_power_cycle_count++;
			// TODO Handle too many power cycles in level --> game over
		} else {
			debug_uart("Warm\n");
			reset_count++;
			//level_reset_count_per_console++;
			// Counter was already incremented in reset IRQ handler
			// TODO Handle too many resets in level --> game over? penalty?
		}
		debug_uart("Followup boot sequence OK\n");
	}
	
	//display_init(RESOLUTION_320x240, DEPTH_16_BPP, FB_COUNT, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS);

	t3d_init((T3DInitParams){});
	viewport = t3d_viewport_create_buffered(FB_COUNT);
  	rdpq_text_register_font(FONT_BUILTIN_DEBUG_MONO, rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO));

	t3d_vec3_norm(&lightDirVec);

	frameIdx = 0;
	
	debug_uart("T3D init OK\n");


	tpx_init((TPXInitParams){});
	
	debug_uart("TPX init OK\n");

	//audio_init(44100, 4);
	//mixer_init(20);
    
	if (game_state == IN_GAME) {
		play_ingame_music();
	} else {
		play_menu_music();
	}
	
	debug_uart("Music playing OK\n");

	wav64_open(&sfx_blip, "rom:/blip.wav64");

	console_model = t3d_model_load("rom:/crt.t3dm");
	n64_model = t3d_model_load("rom:/console.t3dm");
	
	bg_pattern = sprite_load("rom:/pattern.i8.sprite");
	bg_gradient = sprite_load("rom:/gradient.i8.sprite");
	
	logo_n64 = sprite_load("rom:/n64.sprite");
	logo_saturn = sprite_load("rom:/saturn.sprite");
	logo_playstation = sprite_load("rom:/playstation.sprite");

    spr_a = sprite_load("rom:/AButton.sprite");
    spr_b = sprite_load("rom:/BButton.sprite");
    spr_c_up = sprite_load("rom:/CUp.sprite");
    spr_c_down = sprite_load("rom:/CDown.sprite");
    spr_progress = sprite_load("rom:/CircleProgress.i8.sprite");
    spr_circlemask = sprite_load("rom:/CircleMask.i8.sprite");

	spr_swirl = sprite_load("rom://swirl.i4.sprite");

    font_halo_dek = rdpq_font_load("rom:/HaloDek.font64");
    rdpq_text_register_font(FONT_HALODEK, font_halo_dek);
    rdpq_font_style(font_halo_dek, 0, &(rdpq_fontstyle_t){.color = RGBA32(0xFF, 0xFF, 0xFF, 0xFF) });
	
	debug_uart("Resources load OK\n");

	// Happens only on reset
	// Load model for each console
	for (int i=0; i<consoles_count; i++) {
		setup_console(i, &consoles[i]);
	}
	
	debug_uart("Consoles setup OK\n");
	
	// Reset IRQ handler
	register_RESET_handler(reset_interrupt_callback);
	
	debug_uart("NMI handler register OK\n");

	update();
	
	debug_uart("First update OK\n");
	
	debug_uart("Entering main loop\n");

	while (true /*!in_reset*/) {
		frameIdx = (frameIdx + 1) % FB_COUNT;
		frametime = display_get_delta_time();
		gtime += frametime;

		mixer_try_play();

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

		render_offscreen();

		rdpq_attach(display_get(), display_get_zbuf());
		render_3d();
		render_2d();
		rdpq_detach_show();
	}
	
	debug_uart("Out of main loop\n");
	
	return 0;
}
