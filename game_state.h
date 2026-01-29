#pragma once

#include <t3d/t3dmodel.h>
#include <t3d/t3dskeleton.h>


// Constants

#define BUTTON_HOLD_THRESHOLD (0.4f)
#define OVERHEAT_PERIOD (8.0f)
#define LONG_RESET_THRESHOLD (5000)
#define LONG_RESET_GRACE_PERIOD (2000)


// Levels

#define TOTAL_LEVELS (10)

typedef struct {
	uint8_t consoles_count;
	float attack_rate;					// Average attacks per second
	float attack_grace_pediod;			// Grace period after each attack/shrink
	float attacker_restore_threshold;	// Baseline proportion of replicas required for a successful restoration (lower proportion == more persistent)
	float overheat_restore_threshold;	// Baseline proportion of replicas required for a successful restoration (lower proportion == more persistent)
	float high_persistence_threshold;	// Proportion of high-persistence attackers/overheat
	uint8_t max_resets_per_console;
	bool allow_long_reset;				// Whether a long-press of the reset button decreases overheat even more
	uint8_t max_power_cycles;
	uint8_t duration;					// In seconds
	char* description;
} level_t;


// Consoles

#define CONSOLE_MAGIC (0x11223300)
#define CONSOLE_MASK (0xffffff00)
#define CONSOLE_REPLICAS (200)
#define MAX_CONSOLES (4)

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
	// Exclude remaining fields from replication
	char __exclude;
	displayable_t* displayable;	// Link to the displayable data is stale and must updated when restoring
	void* replicas[CONSOLE_REPLICAS];	// Replica pointers may be stale and must be updated when restoring
} console_t;

#define CONSOLE_PAYLOAD_SIZE (sizeof(console_t)-(sizeof(console_t)-offsetof(console_t, __exclude)))


// Attackers

#define ATTACKER_MAGIC (0x44556600)
#define ATTACKER_MASK (0xffffff00)
#define ATTACKER_REPLICAS (100)
#define TOTAL_RIVALS (2)
#define TOTAL_BUTTONS (4)
#define QUEUE_LENGTH (4)

typedef enum {
	SATURN = 0,
	PLAYSTATION
} rival_t;

typedef enum {
	BTN_A = 0,
	BTN_B,
	BTN_C_UP,
	BTN_C_DOWN,
} queue_button_t;

typedef struct {
	queue_button_t buttons[QUEUE_LENGTH];
	uint8_t start;
	uint8_t end;
} attack_queue_t;

typedef struct {
	uint32_t id;
	bool spawned;
	rival_t rival_type;		// Logo
	attack_queue_t queue;	// Queue of buttons to be held
	uint8_t level;			// Buttons in queue
	float last_attack;		// Time of the latest attack or shrink
	int min_replicas;		// Actual (partly random) number of replicas required for a successful restoration (lower == more persistent)
	// TODO Random persistence level
	// TODO Vary strength (requires longer buttons presses? attacks faster? ...)
	// Exclude remaining fields from replication
	char __exclude;
	void* replicas[ATTACKER_REPLICAS];
} attacker_t;

#define ATTACKER_PAYLOAD_SIZE (sizeof(attacker_t)-(sizeof(attacker_t)-offsetof(attacker_t, __exclude)))


// Overheat

#define OVERHEAT_MAGIC (0x77889900)
#define OVERHEAT_MASK (0xffffff00)
#define OVERHEAT_REPLICAS (100)

typedef struct {
	uint32_t id;
	int overheat_level;		// 3 levels of smoke
	float last_overheat;	// Time of the latest level change
	int min_replicas;		// Actual (partly random) number of replicas required for a successful restoration (lower == more persistent)
	// TODO Random persistence level
	// Exclude remaining fields from replication
	char __exclude;
	void* replicas[OVERHEAT_REPLICAS];
} overheat_t;

#define OVERHEAT_PAYLOAD_SIZE (sizeof(overheat_t)-(sizeof(overheat_t)-offsetof(overheat_t, __exclude)))


// Global game state

#define GLOBAL_STATE_MAGIC (0xaabbcc00)
#define GLOBAL_STATE_MASK (0xffffff00)
#define GLOBAL_STATE_REPLICAS (200)

typedef enum {
	INTRO = 0,
	IN_GAME,
	NEXT_LEVEL,
	FINISHED,
	GAME_OVER
} game_state_t;

typedef enum {
	OVERHEATED = 0,
	TOO_MANY_RESETS,
	TOO_MANY_POWER_CYCLES,
	PARTIAL_RESTORATION
} game_over_t;

typedef struct {
	uint32_t id;
	game_state_t game_state;
	game_over_t game_over;
	uint8_t current_level;
	uint32_t reset_count;
	uint32_t power_cycle_count;
	uint8_t level_reset_count_per_console[MAX_CONSOLES];
	uint8_t level_power_cycle_count;
	float level_timer;
	// Exclude remaining fields from replication
	char __exclude;
	void* replicas[GLOBAL_STATE_REPLICAS];
} global_state_t;

#define GLOBAL_STATE_PAYLOAD_SIZE (sizeof(global_state_t)-(sizeof(global_state_t)-offsetof(global_state_t, __exclude)))


// Levels configuration

extern const level_t levels[TOTAL_LEVELS];


// Actual game state

extern console_t consoles[MAX_CONSOLES];
extern displayable_t console_displayables[MAX_CONSOLES];
extern attacker_t console_attackers[MAX_CONSOLES];
extern overheat_t console_overheat[MAX_CONSOLES];
extern global_state_t global_state;

extern uint32_t consoles_count;


// Functions for global game state

void dump_game_state();

void replicate_global_state();
void update_global_state();
void init_global_state();
void reset_level_global_state(int next_level);
void reset_global_state ();
void set_game_state(game_state_t state);
void set_game_over(game_over_t reason);
void inc_reset_count();
void inc_power_cycle_count();
void inc_level_reset_count_per_console(int idx);
void inc_level_power_cycle_count();
void set_level_timer(float t);


// Functions for consoles

void replicate_console(console_t* console);
void update_console(console_t* console);
console_t* add_console();


// Functions for overheat

void replicate_overheat(overheat_t* overheat);
void update_overheat(overheat_t* overheat);
void persist_overheat(overheat_t* overheat);
void increase_overheat(int idx);
void decrease_overheat(int idx);
void reset_overheat_timer(int idx);


// Functions for attackers

void replicate_attacker(attacker_t* attacker);
void update_attacker(attacker_t* attacker);
void shrink_attacker(int idx);
void grow_attacker(int idx);
void spawn_attacker(int idx);
queue_button_t get_attacker_button(int idx, int i);
