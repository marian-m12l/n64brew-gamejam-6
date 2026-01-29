#include <t3d/tpx.h>
#include "gfx.h"


// Checkered background

void draw_bg(sprite_t* pattern, sprite_t* gradient, float offset, color_t base_color) {
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

  rdpq_set_prim_color(base_color);

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


// Button press progress

void drawprogress(int x, int y, float scale, float progress, color_t col, sprite_t* spr_progress, sprite_t* spr_circlemask) {
	rdpq_mode_push();
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
	rdpq_sync_tile();
	rdpq_mode_push();
}


// Black bars on console reset

void draw_bars(float height) {
  if(height > 0) {
	if (height > 39)	height = 39;
	// White line in the middle
	uint8_t intensity = (int) (height * 0xff / 39);
	rdpq_set_mode_standard();
	rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
	rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
	rdpq_set_prim_color(RGBA32(0xff, 0xff, 0xff, intensity));
	rdpq_fill_rectangle(0, 0, 80, 80);
	// Black bars top and bottom
	rdpq_set_prim_color(RGBA32(0, 0, 0, 0xff));
	rdpq_fill_rectangle(0, 0, 80, height);
	rdpq_fill_rectangle(0, 80 - height, 80, 80);
  }
}


// Gauges for overheat, reset count, power cycle count

void draw_gauge(int x, int y, int height, int item_width, int spacing, int border, int item_count, int item_max, color_t color, color_t border_color) {
	rdpq_set_mode_standard();
	rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
	rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
	rdpq_set_prim_color(border_color);
	rdpq_fill_rectangle(x, y, x + 2*border + item_width*item_max + spacing*(item_max-1), y + height);
	rdpq_set_prim_color(color);
	for (int i=0; i<item_max; i++) {
		if (i == item_count) {
			rdpq_set_prim_color(RGBA32(0x33, 0x33, 0x33, 0xff));
		}
		rdpq_fill_rectangle(x + border + item_width*i + spacing*i, y + border, x + border + item_width*(i+1) + spacing*i, y + height - border);
	}
}


// Smoke particle system

static void gradient_smoke(uint8_t *color, float t, int heat_level) {
    t = fminf(1.0f, fmaxf(0.0f, t));
	// Gray to red-ish
	color[0] = (uint8_t)(50 * heat_level + 100 * t);
	color[1] = (uint8_t)(50 + 100 * t);
	color[2] = (uint8_t)(50 + 100 * t);
}

/**
 * Particle system for a smoke effect.
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
    *size = 118 + (rand() % 10);
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

void drawsmoke(particles_t* particles, T3DVec3 position, float console_scale, int frameIdx, float frametime, int heat_level, sprite_t* spr_swirl) {
	particles->tpx_time += frametime * 1.0f;
	particles->timeTile += frametime * 25.1f;
	particles->particleCount = 24 * heat_level;
	// Move a little around console position ?
	float posX = position.x + fm_cosf(particles->tpx_time) * 5.0f;
	float posZ = position.z + fm_sinf(2*particles->tpx_time) * 5.0f;

	rdpq_mode_push();

	simulate_particles_smoke(particles, heat_level, posX, posZ);
	rdpq_set_env_color((color_t){0xFF, 0xFF, 0xFF, 0xFF});

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
		(float[3]){ 6, 6, 6 },
		(float[3]){ 0.0f, 0.0f, 0.0f },
		(float[3]){ 0.0f, 850.0f, 0.0f }
	);
	tpx_matrix_push(&particles->mat_fp[frameIdx]);
	
	float scale = (0.5f + 0.167f * heat_level) * 5 * console_scale;
    tpx_state_set_scale(scale, scale);

    float tileIdx = fm_floorf(particles->timeTile) * 32;
    if(tileIdx >= 512)	particles->timeTile = 0;

	tpx_state_set_tex_params((int16_t)tileIdx, 8);

    tpx_particle_draw_tex_s8(particles->buffer, particles->particleCount);

	tpx_matrix_pop(1);

	rdpq_sync_pipe();
	
	rdpq_mode_pop();
}
