#pragma once

#include "game_state.h"


extern global_state_t restored_global_state;
extern int restored_global_state_count;
extern int restored_global_state_counts;

extern console_t restored_consoles[MAX_CONSOLES];
extern int restored_consoles_count;
extern int restored_consoles_counts[MAX_CONSOLES];

extern attacker_t restored_attackers[MAX_CONSOLES];
extern int restored_attackers_count;
extern int restored_attackers_counts[MAX_CONSOLES];
extern int restored_attackers_ignored;

extern overheat_t restored_overheat[MAX_CONSOLES];
extern int restored_overheat_count;
extern int restored_overheat_counts[MAX_CONSOLES];
extern int restored_overheat_ignored;


bool try_recover();
bool validate_recovered();
