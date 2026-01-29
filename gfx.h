#pragma once

#include <libdragon.h>
#include <t3d/t3d.h>
#include "game_state.h"

#define PARTICLE_COUNT_MAX (72)

// One particle system per console
typedef struct {
	uint32_t particleCount;
	TPXParticleS8* buffer;
	T3DMat4FP* mat_fp;
	float tpx_time;
	float timeTile;
	int currentPart;
} particles_t;
static particles_t console_particles[MAX_CONSOLES];

void draw_bg(sprite_t* pattern, sprite_t* gradient, float offset, color_t base_color);
void drawprogress(int x, int y, float scale, float progress, color_t col, sprite_t* spr_progress, sprite_t* spr_circlemask);
void draw_bars(float height);
void draw_gauge(int x, int y, int height, int item_width, int spacing, int border, int item_count, int item_max, color_t color, color_t border_color);
void drawsmoke(particles_t* particles, T3DVec3 position, float console_scale, int frameIdx, float frametime, int heat_level, sprite_t* spr_swirl);
