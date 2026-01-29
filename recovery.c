#include "recovery.h"
#include "persistence.h"
#include "pc64.h"


global_state_t restored_global_state;
int restored_global_state_count;
int restored_global_state_counts;

console_t restored_consoles[MAX_CONSOLES];
int restored_consoles_count;
int restored_consoles_counts[MAX_CONSOLES];

attacker_t restored_attackers[MAX_CONSOLES];
int restored_attackers_count;
int restored_attackers_counts[MAX_CONSOLES];
int restored_attackers_ignored;

overheat_t restored_overheat[MAX_CONSOLES];
int restored_overheat_count;
int restored_overheat_counts[MAX_CONSOLES];
int restored_overheat_ignored;


bool try_recover() {
    // Restore game data from heap replicas
    restored_global_state_count = restore(&restored_global_state, &restored_global_state_counts, GLOBAL_STATE_PAYLOAD_SIZE, sizeof(global_state_t), 1, GLOBAL_STATE_MAGIC, GLOBAL_STATE_MASK);
    restored_consoles_count = restore(restored_consoles, restored_consoles_counts, CONSOLE_PAYLOAD_SIZE, sizeof(console_t), MAX_CONSOLES, CONSOLE_MAGIC, CONSOLE_MASK);
    restored_attackers_count = restore(restored_attackers, restored_attackers_counts, ATTACKER_PAYLOAD_SIZE, sizeof(attacker_t), MAX_CONSOLES, ATTACKER_MAGIC, ATTACKER_MASK);
    restored_overheat_count = restore(restored_overheat, restored_overheat_counts, OVERHEAT_PAYLOAD_SIZE, sizeof(overheat_t), MAX_CONSOLES, OVERHEAT_MAGIC, OVERHEAT_MASK);

#ifdef DEBUG_MODE
    if (restored_global_state_count > 0) {
        debugf_uart("global_state: %d/%d\n", restored_global_state_counts, GLOBAL_STATE_REPLICAS);
    }
    if (restored_consoles_count > 0) {
        debugf_uart("consoles: ");
        for (int i=0; i<restored_consoles_count; i++) {
            uint32_t id = restored_consoles[i].id;
            debugf_uart("%d/%d ", restored_consoles_counts[id], CONSOLE_REPLICAS);
        }
        debugf_uart("\n");
    }
    if (restored_attackers_count > 0) {
        debugf_uart("attackers: ");
        for (int i=0; i<restored_attackers_count; i++) {
            uint32_t id = restored_attackers[i].id;
            debugf_uart("%d of %d/%d ", restored_attackers_counts[id], restored_attackers[i].min_replicas, ATTACKER_REPLICAS);
        }
        debugf_uart("\n");
    }
    if (restored_overheat_count > 0) {
        debugf_uart("overheat: ");
        for (int i=0; i<restored_overheat_count; i++) {
            uint32_t id = restored_overheat[i].id;
            debugf_uart("%d of %d/%d", restored_overheat_counts[id], restored_overheat[i].min_replicas, OVERHEAT_REPLICAS);
        }
        debugf_uart("\n");
    }

    // TODO Dump heaps over uart ?
#endif
	
    return (restored_global_state_count + restored_consoles_count + restored_attackers_count + restored_overheat_count) > 0;
}


bool validate_recovered() {
    debugf_uart("Restored level %d\n", restored_global_state.current_level);
    bool broken_level = false;
    if (restored_global_state.game_state == IN_GAME && restored_global_state.current_level >= 0 && restored_global_state.current_level < TOTAL_LEVELS) {
        const level_t* level = &levels[restored_global_state.current_level];
        if (restored_consoles_count != level->consoles_count) {
            debugf_uart("FAILED TO RESTORE ALL CONSOLES !!! %d != %d\n", restored_consoles_count, level->consoles_count);
            //debugf_uart("BROKEN: fallback to initial boot sequence\n");
            broken_level = true;
        }
    }
    return !broken_level;
}
