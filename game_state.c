#include <libdragon.h>
#include "game_state.h"
#include "persistence.h"
#include "pc64.h"


const level_t levels[TOTAL_LEVELS] = {
	// cons.	att/s	att.grace	%att	%heat	%hipersist	rst/c	longrst		power	timer	desc
	{ 1,		1.5f,	0.8f,		0,		0,		0,			0,		false,		0,		20,		"Your competitors are after you!\n\nPress and hold buttons to defend against attacks, don't let them overheat your console!" },
	{ 2,		0.7f,	1.5f,		0,		0,		0,			0,		false,		0,		30,		"To defend multiple consoles, plug your controller into the correct slot.\n\nBetter get closer!" },
	{ 2,		1.0f,	1.5f,		0,		0,		0,			1,		false,		0,		60,		"You are now allowed to reset each console once to mitigate overheat. Hitting the RESET button will help you cool the console you're plugged into.\n\nTold ya to get closer!" },
	{ 3,		0.7f,	1.5f,		0.1f,	0,		0,			1,		true,		0,		60,		"Up next: long reset (holding 5 seconds) can cool your consoles even more.\n\nBeware: attacks will keep coming at your consoles!" },
	{ 3,		0.7f,	1.5f,		0.9f,	0.9f,	0,			0,		false,		1,		60,		"One last trick in your bag: if things go out of hand, go hit that big POWER switch! Keep the console off for a few seconds, let your enemies feel the slow decay of that dear RDRAM!\n\nBut remember: don't let the memory decay to the point where you'll lose your consoles..." },
	{ 3,		1.5f,	0.5f,		0.8f,	0.8f,	0.25f,		1,		true,		1,		60,		"Let's make it a bit harder..." },
	{ 4,		1.0f,	1.5f,		0.7f,	0.8f,	0.25f,		2,		true,		1,		60,		"How about one more console?\n\nOK, you get 2 resets per console this time." },
	{ 4,		1.2f,	1.0f,		0.5f,	0.5f,	0.35f,		0,		true,		2,		60,		"You're doing good, keep going!\n\nHow about a challenge: no reset, but 2 power offs allowed!" },
	{ 4,		1.5f,	1.0f,		0.2f,	0.5f,	0.5f,		1,		true,		2,		60,		"Almost there..." },
	{ 4,		1.5f,	0.5f,		0.1f,	0.5f,	0.75f,		0,		true,		3,		90,		"One last effort!" }
};


console_t consoles[MAX_CONSOLES];
displayable_t console_displayables[MAX_CONSOLES];
attacker_t console_attackers[MAX_CONSOLES];
overheat_t console_overheat[MAX_CONSOLES];
global_state_t global_state;

uint32_t consoles_count = 0;


// Global game state

void dump_game_state() {
#if 0	
	debugf_uart("==========\n");
	// Global state
	debugf_uart("\tGLOB: %d | %d || %02d | %02d | %02d %02d %02d %02d | %02d || %06.3f || %d\n",
		global_state.game_state, global_state.current_level,
		global_state.reset_count, global_state.power_cycle_count,
		global_state.level_reset_count_per_console[0], global_state.level_reset_count_per_console[1], global_state.level_reset_count_per_console[2], global_state.level_reset_count_per_console[3],
		global_state.level_power_cycle_count,
		global_state.level_timer,
		global_state.wrong_joypads_count_displayed
	);
	// Consoles
	debugf_uart("\tCONS: %d || %d %04.3f || %d %04.3f || %d %04.3f || %d %04.3f\n",
		consoles_count,
		consoles[0].id, consoles[0].scale.x,
		consoles[1].id, consoles[1].scale.x,
		consoles[2].id, consoles[2].scale.x,
		consoles[3].id, consoles[3].scale.x
	);
	// Attackers
	for (int i=0; i<MAX_CONSOLES; i++) {
		attacker_t* attacker = &console_attackers[i];
		debugf_uart("\tATTK: %d | %d || %d || %d %d %d %d || %d | %d || %d | %06.3f\n",
			attacker->id, attacker->spawned,
			attacker->rival_type,
			attacker->queue.buttons[0], attacker->queue.buttons[1], attacker->queue.buttons[2], attacker->queue.buttons[3],
			attacker->queue.start, attacker->queue.end,
			attacker->level, attacker->last_attack
		);
	}
	// Overheat
	debugf_uart("\tHEAT: %d %d %06.3f || %d %d %06.3f || %d %d %06.3f || %d %d %06.3f\n",
		console_overheat[0].id, console_overheat[0].overheat_level, console_overheat[0].last_overheat,
		console_overheat[1].id, console_overheat[1].overheat_level, console_overheat[1].last_overheat,
		console_overheat[2].id, console_overheat[2].overheat_level, console_overheat[2].last_overheat,
		console_overheat[3].id, console_overheat[3].overheat_level, console_overheat[3].last_overheat
	);
	debugf_uart("==========\n");
#endif
}

void replicate_global_state() {
	debugf_uart("replicate global state\n");
	replicate(HIGHEST, GLOBAL_STATE_MAGIC, &global_state, GLOBAL_STATE_PAYLOAD_SIZE, GLOBAL_STATE_REPLICAS, true, true, global_state.replicas);
	debugf_uart("replicas: %p - %p\n", global_state.replicas[0], global_state.replicas[GLOBAL_STATE_REPLICAS-1]);
	//dump_game_state();
}

void update_global_state() {
	//debugf_uart("updating global state replicas: %p %p %p %p\n", global_state.replicas[0], global_state.replicas[1], global_state.replicas[2], global_state.replicas[3]);
	update_replicas(global_state.replicas, &global_state, GLOBAL_STATE_PAYLOAD_SIZE, GLOBAL_STATE_REPLICAS, true);
	//dump_game_state();
}

void init_global_state() {
	global_state.id = 0;
	global_state.game_state = INTRO;
	global_state.current_level = 0;
	global_state.reset_count = 0;
	global_state.power_cycle_count = 0;
	memset(&global_state.level_reset_count_per_console, 0, sizeof(global_state.level_reset_count_per_console));
	global_state.level_power_cycle_count = 0;
	global_state.level_timer = 0;
	replicate_global_state();
}

void reset_level_global_state(int next_level) {
	global_state.current_level = next_level;
	memset(&global_state.level_reset_count_per_console, 0, sizeof(global_state.level_reset_count_per_console));
	global_state.level_power_cycle_count = 0;
	global_state.level_timer = levels[next_level].duration;
	update_global_state();
}

void reset_global_state () {
	global_state.id = 0;
	global_state.game_state = INTRO;
	global_state.current_level = 0;
	global_state.reset_count = 0;
	global_state.power_cycle_count = 0;
	memset(&global_state.level_reset_count_per_console, 0, sizeof(global_state.level_reset_count_per_console));
	global_state.level_power_cycle_count = 0;
	global_state.level_timer = 0;
	global_state.games_count++;
	global_state.practice = false;
	update_global_state();
}

void set_game_state(game_state_t state) {
	global_state.game_state = state;
	update_global_state();
}

void set_game_over(game_over_t reason) {
	global_state.game_over = reason;
	update_global_state();
}

void inc_reset_count() {
	global_state.reset_count++;
	update_global_state();
}

void inc_power_cycle_count() {
	global_state.power_cycle_count++;
	update_global_state();
}

void inc_level_reset_count_per_console(int idx) {
	global_state.level_reset_count_per_console[idx]++;
	update_global_state();
}

void inc_level_power_cycle_count() {
	global_state.level_power_cycle_count++;
	update_global_state();
}

void set_level_timer(float t) {
	global_state.level_timer = t;
	update_global_state();
}

void set_practice(bool p) {
	global_state.practice = p;
	update_global_state();
}


// Consoles

void replicate_console(console_t* console) {
	debugf_uart("replicate console #%d\n", console->id);
	replicate(HIGHEST, CONSOLE_MAGIC | console->id, console, CONSOLE_PAYLOAD_SIZE, CONSOLE_REPLICAS, true, true, console->replicas);
	debugf_uart("replicas: %p - %p\n", console->replicas[0], console->replicas[CONSOLE_REPLICAS-1]);
	//dump_game_state();
}

void update_console(console_t* console) {
	//debugf_uart("updating console replicas: %p %p %p %p\n", console->replicas[0], console->replicas[1], console->replicas[2], console->replicas[3]);
	update_replicas(console->replicas, console, CONSOLE_PAYLOAD_SIZE, CONSOLE_REPLICAS, true);
	//dump_game_state();
}

console_t* add_console() {
	console_t* console = &consoles[consoles_count];
	debugf_uart("add console #%d (%p)\n", consoles_count, console);
	console->id = consoles_count;
	console->displayable = &console_displayables[consoles_count];
	consoles_count++;
	return console;
}


// Overheat

void replicate_overheat(overheat_t* overheat) {
	debugf_uart("replicate overheat #%d min_replicas=%d count=%d\n", overheat->id, overheat->min_replicas, OVERHEAT_REPLICAS);
	float r = rand() / (float) RAND_MAX;
	persistence_level_t persistence = r < levels[global_state.current_level].high_persistence_threshold ? HIGHEST : LOWEST;
	replicate(persistence, OVERHEAT_MAGIC | overheat->id, overheat, OVERHEAT_PAYLOAD_SIZE, OVERHEAT_REPLICAS, true, true, overheat->replicas);
	debugf_uart("replicas: %p - %p\n", overheat->replicas[0], overheat->replicas[OVERHEAT_REPLICAS-1]);
	//dump_game_state();
}

void update_overheat(overheat_t* overheat) {
	//debugf_uart("updating overheat replicas: %p %p %p %p\n", overheat->replicas[0], overheat->replicas[1], overheat->replicas[2], overheat->replicas[3]);
	update_replicas(overheat->replicas, overheat, OVERHEAT_PAYLOAD_SIZE, OVERHEAT_REPLICAS, true);
	//dump_game_state();
}

void persist_overheat(overheat_t* overheat) {
	// Replicate on first spawn, update otherwise
	if (overheat->replicas[0] == NULL) {
		overheat->min_replicas = (int) OVERHEAT_REPLICAS * levels[global_state.current_level].overheat_restore_threshold;
		if (overheat->min_replicas > 0) {
			overheat->min_replicas += rand() % ((OVERHEAT_REPLICAS - overheat->min_replicas) / 3);
		}
		replicate_overheat(overheat);
	} else {
		update_overheat(overheat);
	}
}

void increase_overheat(int idx) {
	overheat_t* overheat = &console_overheat[idx];
	overheat->id = idx;
	overheat->overheat_level++;
	overheat->last_overheat = global_state.level_timer;
	debugf_uart("increase heat %d: level=%d\n", idx, overheat->overheat_level);
	persist_overheat(overheat);
}

void decrease_overheat(int idx) {
	overheat_t* overheat = &console_overheat[idx];
	if (console_overheat[idx].overheat_level > 0) {
		overheat->overheat_level--;
		overheat->last_overheat = global_state.level_timer;	// To avoid immediate increase (TODO Add grace period of a few additional seconds?)
		debugf_uart("decrease heat %d: level=%d\n", idx, overheat->overheat_level);
		persist_overheat(overheat);
	}
}

void reset_overheat_timer(int idx) {
	overheat_t* overheat = &console_overheat[idx];
	overheat->id = idx;
	overheat->last_overheat = global_state.level_timer;
	persist_overheat(overheat);
}


// Attackers

void replicate_attacker(attacker_t* attacker) {
	debugf_uart("replicate attacker #%d min_replicas=%d count=%d\n", attacker->id, attacker->min_replicas, ATTACKER_REPLICAS);
	float r = rand() / (float) RAND_MAX;
	persistence_level_t persistence = r < levels[global_state.current_level].high_persistence_threshold ? HIGHEST : LOW;
	replicate(persistence, ATTACKER_MAGIC | attacker->id, attacker, ATTACKER_PAYLOAD_SIZE, ATTACKER_REPLICAS, true, true, attacker->replicas);
	debugf_uart("replicas: %p - %p\n", attacker->replicas[0], attacker->replicas[ATTACKER_REPLICAS-1]);
	//dump_game_state();
}

void update_attacker(attacker_t* attacker) {
	//debugf_uart("updating attacker replicas: %p %p %p %p\n", attacker->replicas[0], attacker->replicas[1], attacker->replicas[2], attacker->replicas[3]);
	update_replicas(attacker->replicas, attacker, ATTACKER_PAYLOAD_SIZE, ATTACKER_REPLICAS, true);
	//dump_game_state();
}

void shrink_attacker(int idx) {
	attacker_t* attacker = &console_attackers[idx];
	if (attacker->spawned && attacker->level > 0) {
		// If level was QUEUE_LENGTH, avoid immediate reaction
		if (attacker->level == QUEUE_LENGTH) {
			attacker->last_attack = global_state.level_timer;
			reset_overheat_timer(idx);
		}
		attacker->level--;
		attacker->queue.start = (attacker->queue.start + 1) % QUEUE_LENGTH;
		debugf_uart("shrink %d: level=%d start=%d\n", idx, attacker->level, attacker->queue.start);
		update_attacker(attacker);
	}
}

void grow_attacker(int idx) {
	attacker_t* attacker = &console_attackers[idx];
	//debugf_uart("grow_attacker: %f\n", attacker->last_attack - global_state.level_timer);
	if (attacker->spawned && attacker->level < QUEUE_LENGTH) {
		if (attacker->level == 0) {
			// Re-spawning
			attacker->rival_type = (rand() % TOTAL_RIVALS);
			// TODO Should have a new random level of persistence?
		}
		attacker->level++;
		attacker->queue.buttons[attacker->queue.end] = (rand() % TOTAL_BUTTONS);
		attacker->queue.end = (attacker->queue.end + 1) % QUEUE_LENGTH;
		attacker->last_attack = global_state.level_timer;
		reset_overheat_timer(idx);
		debugf_uart("grow %d: level=%d end=%d\n", idx, attacker->level, attacker->queue.end);
		update_attacker(attacker);
	}
}

void spawn_attacker(int idx) {
	//debugf_uart("spawn_attacker: %f\n", levels[global_state.current_level].duration - global_state.level_timer);
	attacker_t* attacker = &console_attackers[idx];
	attacker->id = idx;
	attacker->spawned = true;
	attacker->rival_type = (rand() % TOTAL_RIVALS);
	attacker->level = 0;
	attacker->last_attack = global_state.level_timer;
	attacker->queue.start = 0;
	attacker->queue.end = 0;
	attacker->min_replicas = (int) ATTACKER_REPLICAS * levels[global_state.current_level].attacker_restore_threshold;
	if (attacker->min_replicas > 0) {
		attacker->min_replicas += rand() % ((ATTACKER_REPLICAS - attacker->min_replicas) / 3);
	}
	debugf_uart("spawn %d: level=%d start=%d end=%d\n", idx, attacker->level, attacker->queue.start, attacker->queue.end);
	replicate_attacker(attacker);
	grow_attacker(idx);
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

